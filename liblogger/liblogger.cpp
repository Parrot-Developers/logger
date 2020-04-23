/**
 * liblogger, a library for recording logs and telemetry
 *
 * Copyright (c) 2019 Parrot Drones SAS.
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

#include "headers.hpp"

ULOG_DECLARE_TAG(ULOG_TAG);

/* Public key to use for encryption */
#define LOGGERD_PUB_KEY_PATH		"/etc/loggerd.pub.pem"

#define FLUSH_PERIOD_MS			1000

namespace loggerd {

/**
 * Internal Log direct writer class.
 */
class DirectWriter: public LogDirectWriter {
public:
	DirectWriter(uint32_t id,
			const std::string &plugin,
			uint32_t version,
			LogBackend *backend);
	virtual ~DirectWriter();

public:
	virtual void write(const void *buf, size_t len) override;

public:
	BaseSource mBaseSource;
	LogBackend *mBackend;
};

/**
 * Daemon class.
 */
class LoggerdImpl : public Loggerd, LogManager {
public:
	LoggerdImpl(struct pomp_loop *loop,
		    const Options &opt,
		    SettingsManager &settingsManager
		    );
	virtual ~LoggerdImpl();

	virtual int loadPlugins(const std::string &pluginDir) override;
	virtual int loadPlugins(const std::vector<Plugin *> &plugins) override;
	virtual void unloadPlugins() override;
	virtual void destroyLogSources() override;
	virtual void destroyDirectWriters() override;

	virtual void stop() override;
	virtual void start() override;
	virtual void requestFlush() override;
	virtual void requestRotate() override;

public:
	virtual int addLogSource(LogSource *source,
			const std::string &plugin,
			const std::string &name,
			uint32_t version) override;
	virtual void removeLogSource(LogSource *source) override;
	virtual LogDirectWriter *getDirectWriter(const std::string &plugin,
			uint32_t version) override;
	virtual void flush(const char *reason) override;
	virtual void updateDate() override;
	virtual void updateExtraProperty(const std::string &key,
					const std::string &value) override;
	virtual void updateFlightId(const char *flight_id) override;
	virtual void updateRefTime(const char *message, time_t tv_sec,
				   long tv_nsec) override;
	virtual void updateTakeoff(bool takeoff) override;
	virtual void rotate() override;
	virtual void enableMd5() override;
	virtual void setEnabled(bool enabled) override;
	virtual void pollSources(bool force) override;

private:
	typedef std::vector<Source *>		SourceVector;
	typedef std::vector<DirectWriter *>	DirectWriterVector;
	typedef std::vector<Plugin *>		PluginVector;
	using CloseReason = LogFrontend::CloseReason;

private:
	static void timerCb(struct pomp_timer *timer, void *userdata);
	static void pompIdleCb(void *userdata);

	void updatePeriod();
	bool pushSourceDescription(Source *source);
	void pushSourceData(Source *source);
	void checkPendingRemove();
	void open();
	void close(CloseReason reason);
	void startSession();

private:
	PluginVector		mPluginVector;
	SourceVector		mSources;
	uint32_t		mPeriod;
	uint32_t		mIdCounter;
	uint64_t		mNow;
	uint64_t		mLastFlush;
	Buffer			*mBuffer;
	volatile bool		mFlushRequested;
	volatile bool		mRotateRequested;
	struct pomp_loop	*mLoop;
	struct pomp_timer	*mTimer;
	LogBackend		*mBackend;
	LogFrontend		*mFrontEnd;
	BaseSource		*mHeaderSource;
	BaseSource		*mFooterSource;
	DirectWriterVector	mDirectWriters;
	bool			mBufferInitOk;
	bool			mEncrypted;
	bool			mEnabled;
	SettingsManager         &mSettingsManager;
};

Plugin::Plugin()
{
	mName = "";
	mPlugin = nullptr;
	mManager = nullptr;
	mAutoUnload = false;
}

DirectWriter::DirectWriter(uint32_t id,
		const std::string &plugin,
		uint32_t version,
		LogBackend *backend) :
	mBaseSource(id, plugin, "loggerd", version), mBackend(backend)
{
}

DirectWriter::~DirectWriter()
{
}

void DirectWriter::write(const void *buf, size_t len)
{
	struct loggerd_entry_header hdr;

	if (mBaseSource.mPendingDescription) {
		ssize_t dlen;
		uint8_t desc[256];

		dlen = mBaseSource.fillDescription(desc, sizeof(desc));
		if (dlen < 0)
			return;

		/* directly write description */
		mBackend->write(desc, dlen, true);
		mBaseSource.mPendingDescription = false;
	}

	hdr.id = mBaseSource.mId;
	hdr.len = len;
	mBackend->write(&hdr, sizeof(hdr), true);
	mBackend->write(buf, len, true);
}

LoggerdImpl::LoggerdImpl(struct pomp_loop *loop, const Options &opt,
	SettingsManager &settingsManager) : mSettingsManager(settingsManager)
{
	mPeriod = LOGGERD_DEFAULT_PERIOD_MS;
	mIdCounter = LOGGERD_ID_BASE;
	mFlushRequested = false;
	mRotateRequested = false;
	mNow = 0;
	mLastFlush = 0;

	/* TODO: Use a system property to determine if we need to enable until
	 * we can get the debug setting */
	mEnabled = true;

	ULOGI("minFreeSpace=%zu maxUsedSpace=%zu maxLogSize=%zu minLogSize=%zu"
	      " maxLogCount=%u", opt.minFreeSpace, opt.maxUsedSpace,
	      opt.maxLogSize, opt.minLogSize, opt.maxLogCount);

	/* Create sources to be used as header and footer by the frontend */
	mHeaderSource = new BaseSource(mIdCounter++, "internal", "header",
			LOGGERD_FILE_VERSION);
	mFooterSource = new BaseSource(mIdCounter++, "internal", "footer",
			LOGGERD_FILE_VERSION);

	/* Create backend and front end */
	mBackend = backend_file_create(opt.outputDir);
	mFrontEnd = new LogFrontend(opt, mBackend, mHeaderSource, mFooterSource,
				    [&] { startSession(); });

	/* Setup buffer for compression */
	mBuffer = new Buffer(mFrontEnd);
	mBufferInitOk = mBuffer->init(LOGGERD_BLOCKSIZE_COMPRESSION,
			LOGGERD_BLOCKSIZE_ENTRY);

	mEncrypted = opt.encrypted;

	/* Create loop and timer */
	mLoop = loop;
	mTimer = pomp_timer_new(mLoop, &LoggerdImpl::timerCb, this);

	/* Setup optional settings server */
	mSettingsManager.initSettings(this);
}

LoggerdImpl::~LoggerdImpl()
{
	int res = 0;

	delete mBuffer;
	mBuffer = nullptr;

	delete mFrontEnd;
	mFrontEnd = nullptr;

	delete mBackend;
	mBackend = nullptr;

	delete mHeaderSource;
	mHeaderSource = nullptr;
	delete mFooterSource;
	mFooterSource = nullptr;

	/* Cleanup settings */
	mSettingsManager.cleanSettings();

	/* Cleanup timer */
	res = pomp_timer_destroy(mTimer);
	if (res < 0)
		ULOG_ERRNO("pomp_timer_destroy", -res);
	mTimer = nullptr;
}

void LoggerdImpl::startSession()
{
	/* Reset buffer in case frontend was closed internally */
	mBuffer->reset();

	/* Notify sources that a new session is starting */
	for (auto source : mSources)
		source->startSession();
	for (auto directWriter : mDirectWriters)
		directWriter->mBaseSource.startSession();
	mHeaderSource->startSession();
	mFooterSource->startSession();

	/* Create encryption context for new file */
	if (mEncrypted && !mBuffer->enableEncryption(LOGGERD_PUB_KEY_PATH))
		goto error;

	/* Success */
	return;

/* Cleanup in case of error */
error:
	if (mFrontEnd->isOpened())
		mFrontEnd->close(CloseReason::UNKNOWN);
	mBuffer->reset();
}

int LoggerdImpl::loadPlugins(const std::vector<Plugin *> &plugins)
{
	int res = 0;

	ULOGI("loading plugins vector");

	for (Plugin *plugin: plugins) {
		/* Load plugin */
		mPluginVector.push_back(plugin);
		plugin->init(this, mLoop);
		mSettingsManager.configureSettings(plugin);
	}

	mSettingsManager.startSettings();

	return res;
}

int LoggerdImpl::loadPlugins(const std::string &pluginDir)
{
	int res = 0;
	DIR *dir = nullptr;
	struct dirent *entry = nullptr;
	std::string path;
	DlPlugin *plugin = nullptr;

	ULOGI("loading plugins from '%s'", pluginDir.c_str());

	/* Open plugins directory */
	dir = opendir(pluginDir.c_str());
	if (dir == nullptr) {
		res = -errno;
		ULOGE("opendir(%s): err=%d(%s)", pluginDir.c_str(),
				-res, strerror(-res));
		goto out;
	}

	/* Load plugins */
	while ((entry = readdir(dir)) != nullptr) {
		if (strcmp(entry->d_name, ".") == 0)
			continue;
		if (strcmp(entry->d_name, "..") == 0)
			continue;

		/* Load plugin */
		path = pluginDir + "/" + entry->d_name;
		plugin = new DlPlugin(path);
		if (plugin->load() < 0) {
			delete plugin;
		} else {
			mPluginVector.push_back(plugin);
			plugin->init(this, mLoop);
			mSettingsManager.configureSettings(plugin);
                        plugin->setAutoUnload(true);
		}
	}

	if (mPluginVector.size() == 0)
		ULOGW("no plugins found");

	mSettingsManager.startSettings();

	/* Cleanup before exiting */
out:
	if (dir != nullptr)
		closedir(dir);
	return res;
}

void LoggerdImpl::unloadPlugins()
{
        for (Plugin *plugin: mPluginVector) {
		plugin->shutdown(this, mLoop);
		plugin->unload();
		if (plugin->autoUnload())
			delete plugin;
	}
}

int LoggerdImpl::addLogSource(LogSource *source,
		const std::string &plugin,
		const std::string &name,
		uint32_t version)
{
	Source *src = new Source(source, mIdCounter++, plugin, name, version);
	mSources.push_back(src);

	/* Notify new source of new session if file is already open, otherwise
	 * wait for open to do it */
	if (mFrontEnd->isOpened())
		src->startSession();
	return 0;
}

void LoggerdImpl::removeLogSource(LogSource *source)
{
	/* Search for source and mark it for removal */
	for (Source *_source : mSources) {
		if (_source->mSource == source) {
			_source->mPendingRemove = true;
			break;
		}
	}
}

void LoggerdImpl::destroyLogSources()
{
	SourceVector::iterator it = mSources.begin();

	while (it != mSources.end()) {
		delete (*it)->mSource;
		delete *it;
		it = mSources.erase(it);
	}
}

LogDirectWriter *LoggerdImpl::getDirectWriter(const std::string &plugin,
					  uint32_t version)
{
	for (auto writer : mDirectWriters) {
		if ((writer->mBaseSource.mPlugin == plugin) &&
				(writer->mBaseSource.mVersion == version)) {
			return writer;
		}
	}

	DirectWriter *writer = new DirectWriter(mIdCounter++,
			plugin, version, mBackend);
	mDirectWriters.push_back(writer);
	return writer;
}

void LoggerdImpl::destroyDirectWriters()
{
	DirectWriterVector::iterator it = mDirectWriters.begin();

	while (it != mDirectWriters.end()) {
		delete *it;
		it = mDirectWriters.erase(it);
	}
}

void LoggerdImpl::updatePeriod()
{
	uint32_t period;

	/* find shortest period */
	mPeriod = LOGGERD_DEFAULT_PERIOD_MS;

	for (auto source : mSources) {
		if (!source->mPendingRemove) {
			period = source->mSource->getPeriodMs();
			if (period < mPeriod)
				mPeriod = period;
		}
	}
}

bool LoggerdImpl::pushSourceDescription(Source *source)
{
	/* get remaining space in buffer */
	uint8_t *head = mBuffer->getWriteHead();
	size_t space = mBuffer->getWriteSpace();

	ssize_t len = source->fillDescription(head, space);
	if (len < 0)
		return false;

	mBuffer->push(len);
	/* ok, we're done describing this source */
	source->mPendingDescription = false;
	return true;
}

void LoggerdImpl::pushSourceData(Source *source)
{
	uint8_t *data;
	size_t len, size, count, written;
	struct loggerd_entry_header hdr;

	/* get remaining space in buffer */
	data = mBuffer->getWriteHead();
	size = mBuffer->getWriteSpace();
	written = 0;

	if (size <= sizeof(hdr))
		/* no enough space left */
		return;

	/* get data in reasonable chunks */
	while (size > sizeof(hdr)) {
		count = size;
		if (count > LOGGERD_BLOCKSIZE_ENTRY)
			count = LOGGERD_BLOCKSIZE_ENTRY;

		/* poll source, reserving space for entry header */
		LogData logData(data + sizeof(hdr), count - sizeof(hdr));
		len = source->mSource->readData(logData);
		if (len > 0) {
			hdr.id = source->mId;
			hdr.len = len;
			len += sizeof(hdr);
			/* insert entry header */
			memcpy(data, &hdr, sizeof(hdr));
			data += len;
			size -= len;
			written += len;
		} else {
			break;
		}
	}

	if (written > 0)
		/* commit data */
		mBuffer->push(written);

	/* schedule next poll */
	source->mDeadline = mNow + source->mSource->getPeriodMs();
}

void LoggerdImpl::pollSources(bool force)
{
	/* Early exit if log not opened */
	if (!mFrontEnd->isOpened())
		return;
	mNow = getTimeMs();

	/* recompute period (sources periods may have changed) */
	updatePeriod();

	/* round-robin polling of sources */
	for (auto source : mSources) {
		if (source->mPendingRemove)
			continue;
		if ((mNow >= source->mDeadline) || force) {
			/* description must be updated before sending data */
			if (source->mPendingDescription &&
					!pushSourceDescription(source)) {
				continue;
			}

			uint64_t t0 = getTimeMs();
			pushSourceData(source);
			uint64_t t1 = getTimeMs();

			if ((t1 - t0) > 2 * mPeriod) {
				ULOGW("polling source %s.%s took %u ms",
						source->mPlugin.c_str(),
						source->mName.c_str(),
						(unsigned int)(t1 - t0));
			}
		}
	}

	if (mNow >= mLastFlush + FLUSH_PERIOD_MS) {
		mBuffer->flush();
		mLastFlush = mNow;
	}

	checkPendingRemove();
}

void LoggerdImpl::flush(const char *reason)
{
	if (mFrontEnd->isOpened()) {
		ULOGI("flushing and syncing, reason: %s\n", reason);
		mBuffer->flush();
		mFrontEnd->sync();
	}
}

void LoggerdImpl::updateExtraProperty(const std::string &key, const std::string &value)
{
	mFrontEnd->updateExtraProperty(key, value);
}

void LoggerdImpl::updateDate()
{
	/* Simply forward to frontend */
	mFrontEnd->updateDate();
}

void LoggerdImpl::updateFlightId(const char *flight_id)
{
	mFrontEnd->updateFlightId(flight_id);
}

void LoggerdImpl::updateRefTime(const char *message, time_t tv_sec, long tv_nsec)
{
	mFrontEnd->updateRefTime(message, tv_sec, tv_nsec);
}

void LoggerdImpl::updateTakeoff(bool takeoff)
{
	mFrontEnd->updateTakeoff(takeoff);
}

void LoggerdImpl::rotate()
{
	close(CloseReason::ROTATE);
	open();
}

void LoggerdImpl::enableMd5()
{
	/* Forward to frontend */
	mFrontEnd->enableMd5();
}

/**
 * Check for source marked to be removed. Should be call when we are sure
 * we are not using the source or the list of sources
 */
void LoggerdImpl::checkPendingRemove()
{
	SourceVector::iterator it = mSources.begin();

	while (it != mSources.end()) {
		if ((*it)->mPendingRemove) {
			delete (*it)->mSource;
			delete *it;
			it = mSources.erase(it);
		} else {
			++it;
		}
	}
}

void LoggerdImpl::setEnabled(bool enabled)
{
	/* TODO: Set a system property also so next time we can be faster to
	 * determine if we need to enable or not logger */

	mEnabled = enabled;
	if (mEnabled)
		open();
	else
		close(CloseReason::DISABLED);
}

void LoggerdImpl::open()
{
	/* Early exit if buffer init failed */
	if (!mBufferInitOk)
		return;

	/* Nothing to do if already opened */
	if (mFrontEnd->isOpened())
		return;

	/* Open frontend (that will write header) */
	if (mFrontEnd->open() != 0)
		goto error;

	/* Setup timer */
	pomp_timer_set_periodic(mTimer, mPeriod, mPeriod);

	return;
error:
	if (mFrontEnd->isOpened())
		mFrontEnd->close(CloseReason::UNKNOWN);
	mBuffer->reset();
}

void LoggerdImpl::close(CloseReason reason)
{
	/* Nothing to do if not opened */
	if (!mFrontEnd->isOpened())
		return;

	/* Stop timer */
	pomp_timer_clear(mTimer);

	/* Flush buffer and close frontend (that will write footer with given
	 * reason and sync) */
	mBuffer->flush();
	mFrontEnd->close(reason);
	mBuffer->reset();
}

void LoggerdImpl::start()
{
	/* Open log if enabled otherwise wait for debug setting */
	if (mEnabled)
		open();
}

void LoggerdImpl::pompIdleCb(void *userdata)
{
	LoggerdImpl *self = reinterpret_cast<LoggerdImpl *>(userdata);

	if (self->mFlushRequested) {
		self->flush("SIGUSR1");
		self->mFlushRequested = false;
	}

	if (self->mRotateRequested) {
		self->rotate();
		self->mRotateRequested = false;
	}
}

void LoggerdImpl::stop()
{
	/* Do a last poll and close with given reason */
	if (mFrontEnd->isOpened()) {
		pollSources(true);
		close(CloseReason::EXITING);
	}
}

void LoggerdImpl::requestFlush()
{
	int res;

	/* Set flush flag, wakeup loop */
	mFlushRequested = true;
	res = pomp_loop_idle_add(mLoop, &LoggerdImpl::pompIdleCb, this);
	if (res < 0)
		ULOG_ERRNO("pomp_loop_idle_add", -res);
	pomp_loop_wakeup(mLoop);
}

void LoggerdImpl::requestRotate()
{
	int res;

	/* Set rotate flag, wakeup loop */
	mRotateRequested = true;
	res = pomp_loop_idle_add(mLoop, &LoggerdImpl::pompIdleCb, this);
	if (res < 0)
		ULOG_ERRNO("pomp_loop_idle_add", -res);
	pomp_loop_wakeup(mLoop);
}

void LoggerdImpl::timerCb(struct pomp_timer *timer, void *userdata)
{
	LoggerdImpl *self = reinterpret_cast<LoggerdImpl *>(userdata);
	self->pollSources(false);
}

Loggerd *Loggerd::create(struct pomp_loop *loop, const Options &opt,
					SettingsManager &settingsManager)
{
	return new LoggerdImpl(loop, opt, settingsManager);
}

void Loggerd::destroy(Loggerd *loggerd)
{
	delete loggerd;
}

} /* namespace loggerd */
