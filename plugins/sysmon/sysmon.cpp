/**
 * loggerd, a daemon for recording logs and telemetry
 *
 * Copyright (c) 2017-2018 Parrot Drones SAS.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *   * Redistributions of source code must retain the above copyright
 *     notice, this list of conditions and the following disclaimer.
 *   * Redistributions in binary form must reproduce the above copyright
 *     notice, this list of conditions and the following disclaimer in the
 *     documentation and/or other materials provided with the distribution.
 *   * Neither the name of the Parrot Company nor the
 *     names of its contributors may be used to endorse or promote products
 *     derived from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE PARROT COMPANY BE LIABLE FOR ANY
 * DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 * ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>

#include <dirent.h>
#include <fcntl.h>
#include <unistd.h>

#include <map>
#include <set>
#include <string>

#include <loggerd-plugin.hpp>

#include <futils/futils.h>
#include <libpomp.h>

#define ULOG_TAG loggerd_sysmon
#include <ulog.h>
ULOG_DECLARE_TAG(ULOG_TAG);

#define SOURCE_NAME	"sysmon"

#define FLUSH_PERIOD_MS	200
#define ACQ_PERIOD_MS	1000

/* To be changed whenever the layout of data of source is changed */
#define VERSION		2

enum {
	TAG_SYSTEM_CONFIG = 0,
	TAG_SYSTEM_STAT = 1,
	TAG_SYSTEM_MEM = 2,
	TAG_SYSTEM_DISK = 3,
	TAG_SYSTEM_NET = 4,
	TAG_PROCESS_STAT = 5,
	TAG_THREAD_STAT = 6,
	TAG_RESERVED = 7,
};

static const std::string PLUGIN_NAME = "sysmon";

class DataFile {
public:
	inline DataFile() : mFd(-1), mPending(false), mDataLen(0)
	{
		memset(mData, 0, sizeof(mData));
		memset(&mTsAcqBegin, 0, sizeof(mTsAcqBegin));
		memset(&mTsAcqEnd, 0, sizeof(mTsAcqEnd));
	}

	inline ~DataFile()
	{
		close();
	}

	inline int open(const char *path)
	{
		int res = 0;
		if (mFd < 0) {
			mFd = ::open(path, O_RDONLY | O_CLOEXEC);
			if (mFd < 0) {
				res = -errno;
				if (errno != ESRCH && errno != ENOENT)
					ULOG_ERRNO("open('%s')", -res, path);
			}
			mPath = path;
		}
		return res;
	}

	inline bool isOpen() const
	{
		return mFd >= 0;
	}

	inline void close()
	{
		if (mFd >= 0) {
			::close(mFd);
			mFd = -1;
		}
	}

	inline bool read()
	{
		int ret;
		ssize_t readlen = 0;

		mDataLen = 0;
		if (mFd < 0) {
			ret = open(mPath.c_str());
			if (ret < 0)
				return false;
		}

		/* Read at offset 0, computing acquisition times,
		 * keep room for final null character */
		time_get_monotonic(&mTsAcqBegin);
		readlen = ::pread(mFd, mData, sizeof(mData) - 1, 0);
		time_get_monotonic(&mTsAcqEnd);

		/* Close file in case of error */
		if (readlen < 0) {
			if (errno != ESRCH && errno != ENOENT)
				ULOG_ERRNO("pread", errno);
			close();
			return false;
		}

		/* Finish with a final null character */
		mData[readlen] = '\0';
		mDataLen = readlen;
		mPending = true;
		return true;
	}

	inline bool dump(loggerd::LogData &data, uint8_t tag,
			const void *header, size_t headerLen)
	{
		bool ok = true;
		if (!mPending)
			return true;

		size_t pos = data.used();
		ok = ok && data.push(tag);
		ok = ok && data.pushBuffer(header, headerLen);
		ok = ok && data.push((uint32_t)mTsAcqBegin.tv_sec);
		ok = ok && data.push((uint32_t)mTsAcqBegin.tv_nsec);
		ok = ok && data.push((uint32_t)mTsAcqEnd.tv_sec);
		ok = ok && data.push((uint32_t)mTsAcqEnd.tv_nsec);
		ok = ok && data.pushString(mData, mDataLen);

		if (!ok) {
			/* Revert what was already pushed */
			data.rewind(data.used() - pos);
		} else {
			mPending = false;
			mDataLen = 0;
		}
		return ok;
	}

private:
	int		mFd;
	bool		mPending;
	struct timespec	mTsAcqBegin;
	struct timespec	mTsAcqEnd;
	char		mData[32768];
	size_t		mDataLen;
	std::string	mPath;
};

class Thread {
public:
	inline Thread(int pid, int tid) : mPid(pid), mTid(tid), mAlive(true)
	{
		ULOGD("Add thread: pid=%d tid=%d", mPid, mTid);
		char path[128] = "";
		snprintf(path, sizeof(path), "/proc/%d/task/%d/stat", pid, tid);
		mStat.open(path);
	}

	inline ~Thread()
	{
		ULOGD("Remove thread: pid=%d tid=%d", mPid, mTid);
	}

	inline bool isAlive() const
	{ return mAlive; }

	inline void setAlive(bool alive)
	{ mAlive = alive; }

	inline bool read()
	{
		mAlive = mStat.read();
		return mAlive;
	}

	inline bool dump(loggerd::LogData &data)
	{
		uint8_t hdr[2 * sizeof(uint32_t)];
		memcpy(hdr, &mPid, sizeof(uint32_t));
		memcpy(hdr + sizeof(uint32_t), &mTid, sizeof(uint32_t));
		if (!mStat.dump(data, TAG_THREAD_STAT, hdr, sizeof(hdr)))
			return false;
		return true;
	}

private:
	int		mPid;
	int		mTid;
	bool		mAlive;
	DataFile	mStat;
};

class Process {
public:
	inline Process(int pid) : mPid(pid), mAlive(true)
	{
		ULOGD("Add process: pid=%d", mPid);
		char path[128] = "";
		snprintf(path, sizeof(path), "/proc/%d/stat", pid);
		mStat.open(path);
	}

	inline ~Process()
	{
		for (auto &entry : mThreads)
			delete entry.second;
		mThreads.clear();
		ULOGD("Remove process: pid=%d", mPid);
	}

	inline bool isAlive() const
	{ return mAlive; }

	inline void setAlive(bool alive)
	{ mAlive = alive; }

	inline bool read(bool monitorThreads)
	{
		mAlive = mStat.read();
		if (monitorThreads && updateThreads()) {
			for (auto &entry : mThreads)
				entry.second->read();
		}
		return mAlive;
	}

	inline bool dump(loggerd::LogData &data)
	{
		uint8_t hdr[sizeof(uint32_t)];
		memcpy(hdr, &mPid, sizeof(uint32_t));
		if (!mStat.dump(data, TAG_PROCESS_STAT, hdr, sizeof(hdr)))
			return false;

		for (auto &entry : mThreads) {
			if (!entry.second->dump(data))
				return false;
		}

		return true;
	}

private:
	inline bool updateThreads()
	{
		char path[128] = "";
		DIR *dir = nullptr;
		struct dirent *entry = nullptr;
		int tid = -1;

		/* Clear alive flag for all threads */
		for (auto &entry : mThreads)
			entry.second->setAlive(false);

		/* Try to open task sub-directory */
		snprintf(path, sizeof(path), "/proc/%d/task", mPid);
		dir = ::opendir(path);
		if (dir == nullptr) {
			if (errno != ESRCH && errno != ENOENT)
				ULOG_ERRNO("opendir('%s')", errno, path);
			mAlive = false;
			return false;
		}

		/* Read threads ids */
		while ((entry = ::readdir(dir)) != nullptr) {
			if (entry->d_type != DT_DIR || entry->d_name[0] == '.')
				continue;

			tid = atoi(entry->d_name);
			if (tid == 0)
				continue;

			/* Add if not known, otherwise set back alive flag */
			auto it = mThreads.find(tid);
			if (it == mThreads.end()) {
				Thread *thread = new Thread(mPid, tid);
				if (thread != nullptr)
					mThreads.insert({ tid, thread });
			} else {
				it->second->setAlive(true);
			}
		}

		/* Remove dead threads */
		auto it = mThreads.begin();
		while (it != mThreads.end()) {
			Thread *thread = it->second;
			if (!thread->isAlive()) {
				delete thread;
				it = mThreads.erase(it);
			} else {
				++it;
			}
		}

		::closedir(dir);
		return true;
	}

private:
	typedef std::map<int, Thread *> ThreadMap;

private:
	int		mPid;
	bool		mAlive;
	DataFile	mStat;
	ThreadMap	mThreads;
};

class System
{
public:
	inline System()
	{
		mBitField = 0xfffffffff;

		mFileByTag[TAG_SYSTEM_STAT].open("/proc/stat");
		mFileByTag[TAG_SYSTEM_MEM].open("/proc/meminfo");
		mFileByTag[TAG_SYSTEM_DISK].open("/proc/diskstats");
		mFileByTag[TAG_SYSTEM_NET].open("/proc/net/dev");
	}

	inline ~System()
	{
	}

	inline void read()
	{
		for (auto it = mFileByTag.begin(); it != mFileByTag.end(); ++it) {
			if (mBitField & (1 << it->first))
				it->second.read();
		}
	}

	inline bool dump(loggerd::LogData &data)
	{
		for (auto it = mFileByTag.begin(); it != mFileByTag.end(); ++it) {
			if (mBitField & (1 << it->first)) {
				if (!it->second.dump(data, it->first, nullptr, 0))
					return false;
			}
		}
		return true;
	}

	inline void setSystemConfig(uint64_t bitField)
	{ mBitField = bitField; }

private:
	uint64_t 		    mBitField;
	std::map<uint8_t, DataFile> mFileByTag;
};

static bool getProcessName(int pid, char *name, size_t nameSize)
{
	int fd = -1;
	char path[128] = "";
	ssize_t readLen = 0;

	/* Try to open 'comm' file of process */
	snprintf(path, sizeof(path), "/proc/%d/comm", pid);
	fd = ::open(path, O_RDONLY | O_CLOEXEC);
	if (fd < 0)
		return false;

	/* Read name, add final null character (and remove new line) */
	readLen = ::read(fd, name, nameSize - 1);
	if (readLen > 0) {
		if (name[readLen - 1] == '\n')
			name[readLen - 1] = '\0';
		else
			name[readLen] = '\0';
	}

	::close(fd);
	return readLen > 0;
}

class Monitor {
public:
	typedef std::set<std::string>	NameSet;

	struct Config {
		bool	monitorThreads;
		NameSet	includedNames;
		NameSet	excludedNames;

		inline Config() : monitorThreads(true) {}


		inline bool isProcessMonitored(int pid)
		{
			char name[32] = "";

			/* If no filter, assume yes */
			if (includedNames.empty() && excludedNames.empty())
				return true;

			/* Get name of process, no monitoring if it fails */
			if (!getProcessName(pid, name, sizeof(name)))
				return false;

			/* Exclusion prioritary over inclusion */
			if (excludedNames.find(name) != excludedNames.end())
				return false;
			if (includedNames.find(name) != includedNames.end())
				return true;

			/* If inclusion list specified, exclude others */
			if (!includedNames.empty())
				return false;

			/* If exclusion list specified, exclude others */
			if (!excludedNames.empty())
				return true;

			return false;
		}
	};

public:
	inline Monitor() : mSystemConfigDumped(false)
	{
	}

	inline ~Monitor()
	{
		for (auto &entry : mProcesses)
			delete entry.second;
		mProcesses.clear();
	}

	inline const Config &getConfig(const Config &config) const
	{ return mConfig; }

	inline void setConfig(const Config &config)
	{ mConfig = config; }

	inline void setSystemConfig(uint64_t bitField)
	{ mSystem.setSystemConfig(bitField); }

	inline void setSystemConfigDumped(bool systemConfigDumped)
	{ mSystemConfigDumped = systemConfigDumped; }

	inline void read()
	{
		mSystem.read();
		updateProcesses();
		for (auto &entry : mProcesses)
			entry.second->read(mConfig.monitorThreads);
	}

	inline bool dump(loggerd::LogData &data)
	{
		/* Dump system config if needed */
		if (!mSystemConfigDumped) {
			uint32_t clkTck = sysconf(_SC_CLK_TCK);
			uint32_t pagesize = getpagesize();
			if (data.remaining() < 5)
				return false;
			bool ok = true;
			ok = ok && data.push((uint8_t)TAG_SYSTEM_CONFIG);
			ok = ok && data.push(clkTck);
			ok = ok && data.push(pagesize);
			mSystemConfigDumped = ok;
		}

		if (!mSystem.dump(data))
			return false;

		for (auto &entry : mProcesses) {
			if (!entry.second->dump(data))
				return false;
		}

		return true;
	}

private:
	inline void updateProcesses()
	{
		DIR *dir = nullptr;
		struct dirent *entry = nullptr;
		int pid = -1;

		/* Clear alive flag for all processes */
		for (auto &entry : mProcesses)
			entry.second->setAlive(false);

		/* Try to open process sub-directory */
		dir = ::opendir("/proc");
		if (dir == nullptr) {
			if (errno != ESRCH && errno != ENOENT)
				ULOG_ERRNO("opendir('/proc')", errno);
			return;
		}

		/* Read process ids */
		while ((entry = ::readdir(dir)) != nullptr) {
			if (entry->d_type != DT_DIR || entry->d_name[0] == '.')
				continue;

			/* Get pid, check if it should be monitored */
			pid = atoi(entry->d_name);
			if (pid == 0)
				continue;
			if (!mConfig.isProcessMonitored(pid))
				continue;

			/* Add if not known, otherwise set back alive flag */
			auto it = mProcesses.find(pid);
			if (it == mProcesses.end()) {
				Process *process = new Process(pid);
				if (process != nullptr)
					mProcesses.insert({ pid, process });
			} else {
				it->second->setAlive(true);
			}
		}

		/* Remove dead processes */
		auto it = mProcesses.begin();
		while (it != mProcesses.end()) {
			Process *process = it->second;
			if (!process->isAlive()) {
				delete process;
				it = mProcesses.erase(it);
			} else {
				++it;
			}
		}

		::closedir(dir);
	}

private:
	typedef std::map<int, Process *> ProcessMap;

private:
	Config		mConfig;
	System		mSystem;
	ProcessMap	mProcesses;
	bool		mSystemConfigDumped;
};

class SysMonLogSource : public loggerd::LogSource {
public:
	SysMonLogSource(struct pomp_loop *loop);
	virtual ~SysMonLogSource();

public:
	virtual size_t readData(loggerd::LogData &data) override;
	virtual uint32_t getPeriodMs() override;
	virtual void startSession() override;

	inline void setConfig(const Monitor::Config &config)
	{ mMonitor.setConfig(config); }

	inline void setSystemConfig(uint64_t bitField)
	{ mMonitor.setSystemConfig(bitField); }

private:
	inline static void timerCb(struct pomp_timer *timer, void *userdata)
	{
		SysMonLogSource *self =
			reinterpret_cast<SysMonLogSource *>(userdata);
		self->onTimer();
	}

	void onTimer();

private:
	struct pomp_loop	*mLoop;
	struct pomp_timer	*mTimer;
	Monitor			mMonitor;
};

class SysMonPlugin : public loggerd::LogPlugin {
public:
	SysMonPlugin(loggerd::LogManager *manager, struct pomp_loop *loop);
	~SysMonPlugin();

public:
	virtual const std::string &getName() const override;
	virtual void setSettings(const std::string &val) override;

private:
	void parseConfig(const std::string &key, const std::string &value);

private:
	SysMonLogSource	*mLogSource;
};

void SysMonLogSource::startSession()
{
	mMonitor.setSystemConfigDumped(false);
}

SysMonLogSource::SysMonLogSource(struct pomp_loop *loop)
	: mLoop(loop)
{
	mTimer = pomp_timer_new(mLoop, &SysMonLogSource::timerCb, this);
	pomp_timer_set_periodic(mTimer, ACQ_PERIOD_MS, ACQ_PERIOD_MS);
}

SysMonLogSource::~SysMonLogSource()
{
	pomp_timer_destroy(mTimer);
	mTimer = nullptr;
	mLoop = nullptr;
}

size_t SysMonLogSource::readData(loggerd::LogData &data)
{
	mMonitor.dump(data);
	return data.used();
}

uint32_t SysMonLogSource::getPeriodMs()
{
	return FLUSH_PERIOD_MS;
}

void SysMonLogSource::onTimer()
{
	mMonitor.read();
}

SysMonPlugin::SysMonPlugin(loggerd::LogManager *manager, struct pomp_loop *loop)
{
	mLogSource = new SysMonLogSource(loop);
	manager->addLogSource(mLogSource, PLUGIN_NAME, SOURCE_NAME, VERSION);

}

SysMonPlugin::~SysMonPlugin()
{
	/* Log source automatically destroyed by manager */
	mLogSource = nullptr;
}

const std::string &SysMonPlugin::getName() const
{
	return PLUGIN_NAME;
}

void SysMonPlugin::parseConfig(const std::string &key, const std::string &value)
{
	uint64_t bitField;
	Monitor::Config config;
	size_t start = 0, end = 0;

	if (key == "monitor") {
		do {
			end = value.find('|', start);
			std::string sItem = (end != std::string::npos ?
					value.substr(start, end - start) :
					value.substr(start));

			if (sItem == "#NOTHREADS")
				config.monitorThreads = false;
			else if (sItem.length() > 1 && sItem[0] == '!')
				config.excludedNames.insert(sItem.substr(1));
			else if (!sItem.empty() && sItem[0] != '!')
				config.includedNames.insert(sItem);

			start = end + 1;
		} while (end != std::string::npos);

		mLogSource->setConfig(config);
	} else if (key == "module") {
		bitField = std::stol(value, nullptr, 0);
		mLogSource->setSystemConfig(bitField);
	} else {
		ULOGE("Unknown key %s", key.c_str());
	}
}

void SysMonPlugin::setSettings(const std::string &val)
{
	size_t start = 0, end = 0;

	/* Parse monitor and module settings. */
	do {
		end = val.find(';', start);
		std::string item = (end != std::string::npos ?
				val.substr(start, end - start) :
				val.substr(start));

		size_t pos = item.find('=');
		if (pos != std::string::npos) {
			std::string item_key = item.substr(0, pos);
			std::string item_val = (end != std::string::npos ?
					item.substr(pos + 1, end - pos + 1) :
					item.substr(pos + 1));

			parseConfig(item_key, item_val);
		}

		start = end + 1;
	} while (end != std::string::npos);
}

extern "C" void loggerd_plugin_init(loggerd::LogManager *manager,
		struct pomp_loop *loop, loggerd::LogPlugin **plugin)
{
	SysMonPlugin *_plugin = new SysMonPlugin(manager, loop);
	*plugin = _plugin;
}

extern "C" void loggerd_plugin_shutdown(loggerd::LogManager *manager,
		struct pomp_loop *loop, loggerd::LogPlugin *plugin)
{
	SysMonPlugin *_plugin = static_cast<SysMonPlugin *>(plugin);
	delete _plugin;
}
