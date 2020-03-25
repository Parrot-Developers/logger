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
#include <assert.h>

#include <algorithm>
#include <map>
#include <vector>

#include <loggerd-plugin.hpp>
#include <libpomp.h>
#include <libshdata.h>

#define ULOG_TAG loggerd_tlm
#include <ulog.h>
ULOG_DECLARE_TAG(ULOG_TAG);


static const std::string PLUGIN_NAME = "telemetry";

#define PERIOD_MS	100
#define LIST_PERIOD_MS	1000

/* To be changed whenever the layout of data of source is changed */
#define VERSION		1

/** Tag markers */
enum TLM_TAG {
	TLM_TAG_HEADER = 0,
	TLM_TAG_SAMPLE = 1,
};

class TlmLogSource : public loggerd::LogSource {
public:
	TlmLogSource(const std::string &name);
	virtual ~TlmLogSource();
	inline const std::string &getName() const { return mName; }

public:
	virtual size_t readData(loggerd::LogData &data) override;
	virtual uint32_t getPeriodMs() override;
	virtual void startSession() override;

private:
	bool isOpened() const { return mShdCtx != nullptr; }
	bool open();
	void close();

private:
	std::string		mName;
	struct shd_ctx		*mShdCtx;
	struct shd_header	mShdHeader;
	void			*mMetadata;
	struct shd_sample	*mSampleArray;
	uint32_t		mMaxNbSamples;
	bool			mHeaderLogged;
	struct timespec		mLastTs;
};

class TlmPlugin : public loggerd::LogPlugin {
public:
	TlmPlugin(loggerd::LogManager *manager, struct pomp_loop *loop);
	~TlmPlugin();

	void start();
	void stop();
	void setFilter(const std::string &filter);

public:
	inline virtual const std::string &getName() const override
	{
		return PLUGIN_NAME;
	}

	virtual void setSettings(const std::string &val) override;

private:
	static void timerCb(struct pomp_timer *timer, void *userdata)
	{
		TlmPlugin *self = reinterpret_cast<TlmPlugin *>(userdata);
		self->onTimer();
	}
	void onTimer();
	void getSectionList(std::vector<std::string> &sections) const;


private:
	typedef std::map<std::string, TlmLogSource *>	LogSourceMap;
	typedef std::vector<std::string>		StringVector;
	enum class FilterType { FILTER_ADD, FILTER_REMOVE };
private:
	loggerd::LogManager	*mManager;
	struct pomp_loop	*mLoop;
	struct pomp_timer	*mTimer;
	StringVector		mFilter;
	FilterType		mFilterType;
	LogSourceMap		mLogSourceMap;
};

TlmLogSource::TlmLogSource(const std::string &name)
{
	mName = name;
	mShdCtx = nullptr;
	memset(&mShdHeader, 0, sizeof(mShdHeader));
	mMetadata = nullptr;
	mSampleArray = nullptr;
	mMaxNbSamples = 0;
	mHeaderLogged = false;
	mLastTs.tv_sec = 0;
	mLastTs.tv_nsec = 0;
}

TlmLogSource::~TlmLogSource()
{
	close();
}

size_t TlmLogSource::readData(loggerd::LogData &data)
{
	int res = 0;
	size_t writelen = 0;
	uint32_t bytes_per_sample = 0;
	uint32_t nb_samples = 0;
	struct shd_read_request request;
	struct shd_read_result result;
	memset(&request, 0, sizeof(request));
	memset(&result, 0, sizeof(result));

	/* Try to open section if needed */
	if (!isOpened() && !open())
		goto out;

	/* Did we already logged header ? */
	if (!mHeaderLogged) {
		uint32_t headerlen = sizeof(uint8_t) +
				sizeof(mShdHeader) +
				mShdHeader.metadata_size;
		if (data.remaining() < headerlen)
			goto out;

		/* Section header and metadata */
		bool ok = true;
		ok = ok && data.push((uint8_t)TLM_TAG_HEADER);
		ok = ok && data.push(mShdHeader.sample_count);
		ok = ok && data.push(mShdHeader.sample_size);
		ok = ok && data.push(mShdHeader.sample_rate);
		ok = ok && data.push(mShdHeader.metadata_size);
		ok = ok && data.pushBuffer(mMetadata, mShdHeader.metadata_size);
		assert(ok);

		/* Update lengths */
		writelen = data.used();
		mHeaderLogged = true;
	}

	/* How many samples can we log in remaining data ?
	 * 1 byte for the tag, 2 u32 for timestamp, 1 u32 for seqnum */
	bytes_per_sample = 1 + 3 * sizeof(uint32_t) + mShdHeader.sample_size;
	nb_samples = std::min((uint32_t)data.remaining() / bytes_per_sample,
			mMaxNbSamples);
	if (nb_samples == 0)
		goto out;

	/* Setup read request */
	request.mode = SHD_READ_MODE_STRICTLY_AFTER;
	request.ts = mLastTs;
	request.max_nb_samples = nb_samples;
	request.max_nb_samples_before_ref = 0;
	request.max_nb_samples_after_ref = nb_samples - 1;
	request.hint = nullptr;
	request.out_sample_array = mSampleArray;

	/* Setup sample data */
	memset(mSampleArray, 0, mMaxNbSamples * sizeof(struct shd_sample));
	for (uint32_t i = 0; i < nb_samples; i++) {
		mSampleArray[i].data = data.current() +
				i * (bytes_per_sample) +
				1 + 3 * sizeof(uint32_t);
		mSampleArray[i].data_size = mShdHeader.sample_size;
	}

	/* Do the read */
	res = shd_read(mShdCtx, &request, &result);
	if (res < 0) {
		/* No log for expected errors */
		if (res == -EAGAIN || res == -ENOENT)
			goto out;
		/* If format has changed, need to reopen the shd */
		if (res == -ENODEV) {
			close();
			goto out;
		}
		ULOG_ERRNO("shd_read", -res);
		goto out;
	}

	/* Finalize read samples */
	for (uint32_t i = 0; i < result.nb_samples; i++) {
		uint32_t tv_sec = mSampleArray[i].ts.tv_sec;
		uint32_t tv_nsec = mSampleArray[i].ts.tv_nsec;
		uint32_t seqnum = mSampleArray[i].seqnum;

		/* Write tag, timestamp and sequence number */
		bool ok = true;
		ok = ok && data.push((uint8_t)TLM_TAG_SAMPLE);
		ok = ok && data.push(tv_sec);
		ok = ok && data.push(tv_nsec);
		ok = ok && data.push(seqnum);
		ok = ok && data.skip(mShdHeader.sample_size);
		assert(ok);

		/* Update lengths */
		writelen = data.used();
	}

	mLastTs = mSampleArray[result.nb_samples - 1].ts;

out:
	return writelen;
}

uint32_t TlmLogSource::getPeriodMs()
{
	return PERIOD_MS;
}

void TlmLogSource::startSession()
{
	/* Force writing header next time */
	mHeaderLogged = false;
}

bool TlmLogSource::open()
{
	int res = 0;

	/* Try to open section */
	res = shd_open2(mName.c_str(), nullptr, &mShdHeader, &mShdCtx);
	if (res < 0) {
		/* No log for expected errors */
		if (res == -EAGAIN || res == -ENOENT)
			goto error;
		ULOG_ERRNO("shd_open2", -res);
		goto error;
	}

	/* Allocate memory to read metadata */
	mMetadata = calloc(1, mShdHeader.metadata_size);
	if (mMetadata == nullptr)
		goto error;

	/* Read metadata */
	res = shd_read_metadata(mShdCtx, mMetadata, mShdHeader.metadata_size);
	if (res < 0) {
		ULOG_ERRNO("shd_read_metadata", -res);
		goto error;
	}

	/* Allocate memory for sample array */
	mMaxNbSamples = std::min(mShdHeader.sample_count, (uint32_t)2000);
	mSampleArray = (struct shd_sample *)calloc(mMaxNbSamples,
			sizeof(struct shd_sample));
	if (mSampleArray == nullptr)
		goto error;

	return true;

error:
	close();
	return false;
}

void TlmLogSource::close()
{
	int res = 0;

	if (mShdCtx != nullptr) {
		res = shd_close2(mShdCtx);
		if (res < 0)
			ULOG_ERRNO("shd_close2", -res);
		mShdCtx = nullptr;
	}

	free(mMetadata);
	mMetadata = nullptr;

	free(mSampleArray);
	mSampleArray = nullptr;
	mMaxNbSamples = 0;

	mHeaderLogged = false;
	mLastTs.tv_sec = 0;
	mLastTs.tv_nsec = 0;
}

TlmPlugin::TlmPlugin(loggerd::LogManager *manager, struct pomp_loop *loop)
{
	mFilterType = FilterType::FILTER_ADD;
	mManager = manager;
	mLoop = loop;
	mTimer = pomp_timer_new(mLoop, &TlmPlugin::timerCb, this);
}

TlmPlugin::~TlmPlugin()
{
	pomp_timer_destroy(mTimer);
	mTimer = nullptr;
}

void TlmPlugin::start()
{
	int res = 0;
	res = pomp_timer_set_periodic(mTimer, LIST_PERIOD_MS, LIST_PERIOD_MS);
	if (res < 0)
		ULOG_ERRNO("pomp_timer_set_periodic", -res);
}

void TlmPlugin::stop()
{
	int res = 0;
	res = pomp_timer_clear(mTimer);
	if (res < 0)
		ULOG_ERRNO("pomp_timer_clear", -res);

	/* Log source are automatically destroyed by manager */
	mLogSourceMap.clear();
}

void TlmPlugin::setFilter(const std::string &filter)
{
	/* Split at ';' and create a vector */
	size_t start = 0, end = 0;
	mFilter.clear();
	if (!filter.empty() && filter != "*") {
		do {
			end = filter.find(';', start);
			std::string name = (end != std::string::npos ?
					filter.substr(start, end - start) :
					filter.substr(start));
			mFilter.push_back(name);
			ULOGI("Add '%s' to telemetry filter", name.c_str());
			start = end + 1;
		} while (end != std::string::npos);
	} else {
		ULOGI("Clear telemetry filter");
	}
}

void TlmPlugin::onTimer()
{
	std::vector<std::string> sections;
	if (mFilterType == FilterType::FILTER_ADD && mFilter.empty())
		getSectionList(sections);
	else if (mFilterType == FilterType::FILTER_REMOVE)
		getSectionList(sections);
	else
		sections = mFilter;

	/* Check for added sections */
	for (uint32_t i = 0; i < sections.size(); i++) {
		if (mLogSourceMap.find(sections[i]) != mLogSourceMap.end())
			continue;

		if (mFilterType == FilterType::FILTER_REMOVE) {
			auto it = std::find(mFilter.begin(), mFilter.end(),
				sections[i]);
			if (it != mFilter.end())
				continue;
		}

		/* Create a new log source */
		TlmLogSource *logSource = new TlmLogSource(sections[i]);
		mLogSourceMap.insert(LogSourceMap::value_type(
				sections[i], logSource));
		mManager->addLogSource(logSource, PLUGIN_NAME,
				sections[i], VERSION);
	}

	/* Check for removed sections */
	auto it = mLogSourceMap.begin();
	while (it != mLogSourceMap.end()) {
		TlmLogSource *logSource = it->second;
		assert(logSource != nullptr);

		/* Check if still valid */
		auto match = [&](const std::string &s) -> bool {
			return s == logSource->getName();
		};
		bool found = std::find_if(sections.begin(),
				sections.end(),
				match) != sections.end();
		if (found) {
			++it;
		} else {
			/* Remove from map and manager,
			 * manager will delete us later */
			it = mLogSourceMap.erase(it);
			mManager->removeLogSource(logSource);
		}
	}
}

void TlmPlugin::getSectionList(std::vector<std::string> &sections) const
{
	int res = 0;
	char **names = nullptr;
	uint32_t count = 0;

	res = shd_get_section_list(&names, &count);
	if (res < 0) {
		ULOG_ERRNO("shd_get_section_list", -res);
		return;
	}

	for (uint32_t i = 0; i < count; i++)
		sections.push_back(names[i]);
	shd_free_section_list(names, count);
}

void TlmPlugin::setSettings(const std::string &val)
{
	size_t start = 0, end = 0;

	/* Clear current filter first */
	setFilter("");

	/* Search <key>=<value> separated by '|' */
	do {
		end = val.find('|', start);
		std::string item = (end != std::string::npos ?
				val.substr(start, end - start) :
				val.substr(start));

		size_t pos = item.find('=');
		if (pos != std::string::npos) {
			std::string item_key = item.substr(0, pos);
			std::string item_val = item.substr(pos + 1);
			if (item_key == "filter" || item_key == "filter_add") {
				mFilterType = FilterType::FILTER_ADD;
				setFilter(item_val);
			} else if (item_key == "filter_remove") {
				mFilterType = FilterType::FILTER_REMOVE;
				setFilter(item_val);
			} else {
				ULOGE("Unknown key %s", item_key.c_str());
			}
		}

		start = end + 1;
	} while (end != std::string::npos);
}

extern "C" void loggerd_plugin_init(loggerd::LogManager *manager,
		struct pomp_loop *loop, loggerd::LogPlugin **plugin)
{
	TlmPlugin *_plugin = new TlmPlugin(manager, loop);
	_plugin->start();
	*plugin = _plugin;
}

extern "C" void loggerd_plugin_shutdown(loggerd::LogManager *manager,
		struct pomp_loop *loop, loggerd::LogPlugin *plugin)
{
	TlmPlugin *_plugin = static_cast<TlmPlugin *>(plugin);
	_plugin->stop();
	delete _plugin;
}
