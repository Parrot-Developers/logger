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
#include <assert.h>
#include <unistd.h>
#include <dirent.h>
#include <fcntl.h>
#include <sys/stat.h>

#include <map>
#include <set>
#include <vector>

#include <libpomp.h>
#include <futils/inotify.h>

#include <loggerd-plugin.hpp>

#define ULOG_TAG loggerd_file
#include "ulog.h"
ULOG_DECLARE_TAG(loggerd_file);

#define SOURCE_NAME	"file"
#define PERIOD_MS	1000

static const std::string PLUGIN_NAME = "file";

enum FILE_TAG {
	FILE_TAG_HEADER = 0,
	FILE_TAG_CHUNK = 1,
	FILE_TAG_STATUS = 2,
};

enum FILE_STATUS {
	FILE_STATUS_OK = 0,
	FILE_STATUS_CORRUPTED = 1,
};

/* To be changed whenever the layout of data of source is changed */
#define VERSION		1

class FilePlugin : public loggerd::LogPlugin {
public:
	FilePlugin(loggerd::LogManager *manager, struct pomp_loop *loop);
	~FilePlugin();

public:
	inline virtual const std::string &getName() const override
	{
		return PLUGIN_NAME;
	}

	virtual void setSettings(const std::string &val) override;
	void processSetting(const std::string &setting);

private:
	static void inotifyFdCb(int fd, uint32_t revents, void *userdata);
	static void inotifyCb(struct inotify_event *event, void *userdata);

	void excludePath(const std::string &path);
	void addPath(const std::string &path, int level=0);
	void addDir(const std::string &dirPath, int level);
	void addFile(const std::string &filePath);
	void addWatch(const std::string &path);

private:
	friend class FileLogSource;

private:
	loggerd::LogManager		*mManager;
	struct pomp_loop		*mLoop;
	int				mInotifyFd;
	std::map<int, std::string>	mInotifyWds;
	std::set<std::string>		mExcludePaths;
	std::vector<std::string>	mFilePaths;
	std::vector<std::string>	mSettings;
	bool				mFilePathsInitDone;
};

class FileLogSource : public loggerd::LogSource {
public:
	FileLogSource(FilePlugin *plugin);
	virtual ~FileLogSource();

public:
	virtual size_t readData(loggerd::LogData &data) override;
	virtual uint32_t getPeriodMs() override;
	virtual void startSession() override;

private:
	bool beginDumpFile(loggerd::LogData &data,
			const std::string &filePath,
			uint32_t id);
	bool continueDumpFile(loggerd::LogData &data);
	bool finishDumpFile(loggerd::LogData &data);

private:
	/**
	 * Context for current file, size should be relatively small, so
	 * 32-bit is enough for offset/size
	 */
	struct FileCtx {
		uint32_t	id;
		std::string	filePath;
		int		fd;
		uint32_t	size;
		uint32_t	off;
		FILE_STATUS	status;

		inline FileCtx()
		{
			id = 0;
			fd = -1;
			size = 0;
			off = 0;
			status = FILE_STATUS_OK;
		}

		inline void close()
		{
			if (fd >= 0)
				::close(fd);
			id = 0;
			filePath = "";
			fd = -1;
			size = 0;
			off = 0;
			status = FILE_STATUS_OK;
		}
	};

private:
	FilePlugin	*mPlugin;
	size_t		mCurrentFileIndex;
	uint32_t	mNextFileId;
	FileCtx		mCurrentFileCtx;
};

void FileLogSource::startSession()
{
	for (auto setting : mPlugin->mSettings)
		mPlugin->processSetting(setting);
}

FilePlugin::FilePlugin(loggerd::LogManager *manager, struct pomp_loop *loop)
{
	mManager = manager;
	mLoop = loop;
	mInotifyFd = -1;
	mFilePathsInitDone = false;
}

FilePlugin::~FilePlugin()
{
	int res = 0;

	if (mInotifyFd >= 0) {
		res = pomp_loop_remove(mLoop, mInotifyFd);
		if (res < 0)
			ULOG_ERRNO("pomp_loop_remove", -res);
		close(mInotifyFd);
		mInotifyFd = -1;
	}
}

void FilePlugin::setSettings(const std::string &val)
{
	size_t start = 0, end = 0;

	if (mFilePathsInitDone) {
		ULOGW("Unable to update list of file paths to dump");
		return;
	}

	/* Search <path> separated by '|'.
	 * Paths starting by '!' are exclude path and should be found before
	 * potential matches to be taken into account... */
	do {
		end = val.find('|', start);
		std::string item = (end != std::string::npos ?
				val.substr(start, end - start) :
				val.substr(start));
		mSettings.push_back(item);
		start = end + 1;
	} while (end != std::string::npos);

	mFilePathsInitDone = true;
}

void FilePlugin::processSetting(const std::string &setting)
{
	if (!setting.empty() && setting[0] == '!')
		excludePath(setting.substr(1));
	else
		addPath(setting);
}

void FilePlugin::inotifyFdCb(int fd, uint32_t revents, void *userdata)
{
	/* Use futils helper for reading and parsing inotify events */
	FilePlugin *self = reinterpret_cast<FilePlugin *>(userdata);
	inotify_process_fd(fd, &FilePlugin::inotifyCb, self);
}

void FilePlugin::inotifyCb(struct inotify_event *event, void *userdata)
{
	FilePlugin *self = reinterpret_cast<FilePlugin *>(userdata);

	/* Search associated path in our map */
	auto it = self->mInotifyWds.find(event->wd);
	if (it == self->mInotifyWds.cend()) {
		ULOGW("Unknown inotify wd: %d", event->wd);
		return;
	}
	std::string path = it->second;

	/* Append file name if any (for directory watch) */
	if (event->len != 0)
		path = path + "/" + event->name;

	/* Ignore temp file (they will surely be renamed later) */
	if (path.length() >= 4 && strcmp(path.c_str() + path.length() - 4,
			".tmp") == 0) {
		return;
	}

	self->addPath(path);
}

void FilePlugin::excludePath(const std::string &path)
{
	mExcludePaths.insert(path);
}

void FilePlugin::addPath(const std::string &path, int level)
{
	struct stat st;

	if (path.empty())
		return;

	if (mExcludePaths.find(path) != mExcludePaths.end()) {
		ULOGI("Excluding '%s'", path.c_str());
		return;
	}

	if (level > 16) {
		ULOGW("Too many recursion level: %d", level);
		return;
	}

	if (stat(path.c_str(), &st) < 0) {
		ULOGW("Unable to stat '%s': %d(%s)", path.c_str(),
				errno, strerror(errno));
		return;
	}

	if (S_ISDIR(st.st_mode))
		addDir(path, level);
	else if (S_ISREG(st.st_mode))
		addFile(path);
}

void FilePlugin::addDir(const std::string &dirPath, int level)
{
	DIR *dir = opendir(dirPath.c_str());
	struct dirent *entry = nullptr;

	if (dir == NULL) {
		ULOGW("Unable to open dir '%s': %d(%s)",
				dirPath.c_str(),
				errno, strerror(errno));
		return;
	}

	/* Recurse in directory */
	while ((entry = readdir(dir)) != nullptr) {
		if (strcmp(entry->d_name, ".") == 0)
			continue;
		else if (strcmp(entry->d_name, "..") == 0)
			continue;
		else
			addPath(dirPath + "/" + entry->d_name, level+ 1);
	}

	closedir(dir);

	/* If directory is writable (/data /var or /tmp), register for
	 * notifications (Only for top level) */
	if (level == 0 && (strncmp(dirPath.c_str(), "/data/", 6) == 0 ||
			strncmp(dirPath.c_str(), "/var/", 5) == 0 ||
			strncmp(dirPath.c_str(), "/tmp/", 5) == 0)) {
		addWatch(dirPath);
	}
}

void FilePlugin::addFile(const std::string &filePath)
{
	/* TODO: Check for duplicates or big files or ourselves.. */
	mFilePaths.push_back(filePath);
}

void FilePlugin::addWatch(const std::string &path)
{
	int res = 0;
	uint32_t mask = IN_CLOSE_WRITE | IN_MOVED_TO;
	const char *_path = path.c_str();

	ULOGI("Add watch for '%s'", _path);

	/* Create inotify watcher the first time */
	if (mInotifyFd < 0) {
		res = inotify_init();
		if (res < 0) {
			ULOG_ERRNO("inotify_create('%s')", errno, _path);
			return;
		}

		mInotifyFd = res;
		res = pomp_loop_add(mLoop, mInotifyFd, POMP_FD_EVENT_IN,
				&FilePlugin::inotifyFdCb, this);
		if (res < 0) {
			ULOG_ERRNO("pomp_loop_add", -res);
			close(mInotifyFd);
			mInotifyFd = -1;
			return;
		}
	}

	/* Add a new watch and remember associated path */
	res = inotify_add_watch(mInotifyFd, _path, mask);
	if (res < 0)
		ULOG_ERRNO("inotify_add_watch('%s')", -res, _path);
	else
		mInotifyWds.insert({ res, path });
}

FileLogSource::FileLogSource(FilePlugin *plugin)
{
	mPlugin = plugin;
	mCurrentFileIndex = 0;
	mNextFileId = 0;
}

FileLogSource::~FileLogSource()
{
	mCurrentFileCtx.close();
}

size_t FileLogSource::readData(loggerd::LogData &data)
{
	size_t writelen = 0;
	while (mCurrentFileIndex < mPlugin->mFilePaths.size()) {
		/* Try to begin new file */
		if (mCurrentFileCtx.fd == -1 && !beginDumpFile(data,
				mPlugin->mFilePaths[mCurrentFileIndex],
				mNextFileId)) {
			break;
		}
		writelen = data.used();

		/* Try to continue file (open may have failed) */
		if (mCurrentFileCtx.fd != -1) {
			if (!continueDumpFile(data))
				break;
			writelen = data.used();
		}

		/* Go to next file */
		if (mCurrentFileCtx.fd == -1) {
			mCurrentFileIndex++;
			mNextFileId++;
		}
	}

	/* Clear file list when finished. If a directory was watched, further
	 * files can be added dynamically later */
	if (mCurrentFileIndex == mPlugin->mFilePaths.size()) {
		mCurrentFileIndex = 0;
		mPlugin->mFilePaths.clear();
	}

	return writelen;
}

uint32_t FileLogSource::getPeriodMs()
{
	return PERIOD_MS;
}

bool FileLogSource::beginDumpFile(loggerd::LogData &data,
		const std::string &filePath,
		uint32_t id)
{
	bool ok = true;
	assert(mCurrentFileCtx.fd == -1);

	ULOGI("Dumping file '%s'", filePath.c_str());

	/* Try to open file, failure is not fatal, we will skip it */
	mCurrentFileCtx.fd = open(filePath.c_str(), O_RDONLY);
	if (mCurrentFileCtx.fd < 0) {
		ULOGW("Unable to open file '%s': %d(%s)",
				filePath.c_str(),
				errno, strerror(errno));
		return true;
	}

	/* Try to get file size, failure is not fatal */
	off_t size = lseek(mCurrentFileCtx.fd, 0, SEEK_END);
	if (size < 0) {
		ULOGW("Unable to get size of file '%s': %d(%s)",
				filePath.c_str(),
				errno, strerror(errno));
		size = 0;
	}
	lseek(mCurrentFileCtx.fd, 0, SEEK_SET);

	mCurrentFileCtx.id = id;
	mCurrentFileCtx.size = size;
	mCurrentFileCtx.off = 0;

	/* Write header */
	ok = ok && data.push((uint8_t)FILE_TAG_HEADER);
	ok = ok && data.push(mCurrentFileCtx.id);
	ok = ok && data.push(mCurrentFileCtx.size);
	ok = ok && data.pushString(filePath);

	/* Close file and try again later if not enough space now */
	if (!ok) {
		mCurrentFileCtx.close();
		mCurrentFileCtx.fd = -1;
	}
	return ok;
}

bool FileLogSource::continueDumpFile(loggerd::LogData &data)
{
	bool ok = true;

	assert(mCurrentFileCtx.fd != -1);

	/* If all data was written, finish file instead */
	if (mCurrentFileCtx.off == mCurrentFileCtx.size)
		return finishDumpFile(data);
	assert(mCurrentFileCtx.off < mCurrentFileCtx.size);

	/* Chunk header */
	ok = ok && data.push((uint8_t)FILE_TAG_CHUNK);
	ok = ok && data.push(mCurrentFileCtx.id);
	if (!ok || data.remaining() <= sizeof(uint32_t))
		return false;

	/* How many bytes can we dump now ? */
	uint32_t count = mCurrentFileCtx.size - mCurrentFileCtx.off;
	if (count > data.remaining() - sizeof(uint32_t))
		count = data.remaining() - sizeof(uint32_t);
	ok = ok && data.push(count);
	assert(ok);

	/* Read from file into buffer, failure is not fatal */
	ssize_t readlen = read(mCurrentFileCtx.fd, data.current(), count);
	if (readlen < 0) {
		ULOGW("Unable to read file '%s': %d(%s)",
				mCurrentFileCtx.filePath.c_str(),
				errno, strerror(errno));
		memset(data.current(), 0, count);
		mCurrentFileCtx.status = FILE_STATUS_CORRUPTED;
	} else if ((size_t)readlen < count) {
		ULOGW("Partial read of file '%s': %zu (%u)",
				mCurrentFileCtx.filePath.c_str(),
				readlen, count);
		memset(data.current() + readlen, 0, count - readlen);
		mCurrentFileCtx.status = FILE_STATUS_CORRUPTED;
	}

	/* In any case assume success */
	data.skip(count);
	mCurrentFileCtx.off += count;
	return true;
}

bool FileLogSource::finishDumpFile(loggerd::LogData &data)
{
	bool ok = true;
	assert(mCurrentFileCtx.off == mCurrentFileCtx.size);
	ok = ok && data.push((uint8_t)FILE_TAG_STATUS);
	ok = ok && data.push(mCurrentFileCtx.id);
	ok = ok && data.push((uint8_t)mCurrentFileCtx.status);
	if (ok)
		mCurrentFileCtx.close();
	return ok;
}

extern "C" void loggerd_plugin_init(loggerd::LogManager *manager,
		struct pomp_loop *loop, loggerd::LogPlugin **plugin)
{
	FilePlugin *_plugin = new FilePlugin(manager, loop);
	FileLogSource *logSource = new FileLogSource(_plugin);

	manager->addLogSource(logSource, PLUGIN_NAME, SOURCE_NAME, VERSION);
	*plugin = _plugin;
}

extern "C" void loggerd_plugin_shutdown(loggerd::LogManager *manager,
		struct pomp_loop *loop, loggerd::LogPlugin *plugin)
{
	FilePlugin *_plugin = static_cast<FilePlugin *>(plugin);
	delete _plugin;
}
