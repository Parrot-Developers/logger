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

#include <deque>
#include <set>

#include <loggerd-plugin.hpp>
#include <libpomp.h>
#include <shs.h>
#include <futils/futils.h>

#define ULOG_TAG loggerd_shs
#include <ulog.h>
ULOG_DECLARE_TAG(ULOG_TAG);

#define SOURCE_NAME	"settings"
#define PERIOD_MS	1000

static const std::string PLUGIN_NAME = "settings";

/* To be changed whenever the layout of data of source is changed */
#define VERSION		1

class ShsPlugin : public loggerd::LogPlugin {
public:
	inline ShsPlugin() {}

public:
	inline virtual const std::string &getName() const override
	{
		return PLUGIN_NAME;
	}

	virtual void setSettings(const std::string &val) override;

public:
	/* List of settings to filter and anonymize log (passwords...) */
	std::set<std::string>	mFilter;
};

class ShsLogSource : public loggerd::LogSource {
public:
	ShsLogSource(ShsPlugin *plugin, struct pomp_loop *loop);
	virtual ~ShsLogSource();

public:
	virtual size_t readData(loggerd::LogData &data) override;
	virtual uint32_t getPeriodMs() override;

public:
	void start();
	void stop();

private:
	static void shsCb(struct shs_ctx *ctx,
			enum shs_evt evt,
			const struct shs_entry *old_entries,
			const struct shs_entry *new_entries,
			size_t count,
			void *userdata)
	{
		ShsLogSource *self = reinterpret_cast<ShsLogSource *>(userdata);
		self->onShsCb(evt, old_entries, new_entries, count);
	}
	void onShsCb(enum shs_evt evt,
			const struct shs_entry *old_entries,
			const struct shs_entry *new_entries,
			size_t count);

private:
	struct Entry {
		struct timespec		ts;
		std::string		name;
		struct shs_value	value;

		inline Entry()
		{
			this->ts.tv_sec = 0;
			this->ts.tv_nsec = 0;
			shs_value_init(&this->value);
		}

		inline Entry(const struct timespec *ts,
				const struct shs_entry *entry)
		{
			this->ts = *ts;
			this->name = entry->name;
			shs_value_init(&this->value);
			shs_value_copy(&this->value, &entry->value);
		}

		inline Entry(const Entry &other)
		{
			this->ts = other.ts;
			this->name = other.name;
			shs_value_init(&this->value);
			shs_value_copy(&this->value, &other.value);
		}

		inline ~Entry()
		{
			shs_value_clean(&this->value);
		}

		inline Entry &operator=(const Entry &other)
		{
			if (this != &other) {
				shs_value_clean(&this->value);
				shs_value_copy(&this->value, &other.value);
			}
			return *this;
		}
	};

	typedef std::deque<Entry>	EntryDeque;

private:
	ShsPlugin		*mPlugin;
	struct pomp_loop	*mLoop;
	struct shs_ctx		*mShsCtx;
	EntryDeque		mEntries;
};

void ShsPlugin::setSettings(const std::string &val)
{
	size_t start = 0, end = 0;

	mFilter.clear();

	if (val.empty())
		return;

	/* Search <name> separated by '|' */
	do {
		end = val.find('|', start);
		std::string name = (end != std::string::npos ?
				    val.substr(start, end - start) :
				    val.substr(start));

		mFilter.insert(name);

		start = end + 1;
	} while (end != std::string::npos);
}

ShsLogSource::ShsLogSource(ShsPlugin *plugin, struct pomp_loop *loop)
{
	mPlugin = plugin;
	mLoop = loop;
	mShsCtx = nullptr;
}

ShsLogSource::~ShsLogSource()
{
	stop();
	assert(mShsCtx == nullptr);
}

size_t ShsLogSource::readData(loggerd::LogData &data)
{
	size_t writelen = 0;

	while (!mEntries.empty()) {
		/* Get entry data */
		const Entry &entry = mEntries.front();
		uint32_t tv_sec = entry.ts.tv_sec;
		uint32_t tv_nsec = entry.ts.tv_nsec;
		uint8_t type = entry.value.type;
		bool filtered = mPlugin->mFilter.find(entry.name) !=
				mPlugin->mFilter.end();

		/* Try to push entry, it it fails, abort loop */
		bool ok = true;
		ok = ok && data.push(tv_sec);
		ok = ok && data.push(tv_nsec);
		ok = ok && data.pushString(entry.name);
		ok = ok && data.push(type);

		switch (entry.value.type) {
		case SHS_TYPE_BOOLEAN:
			ok = ok && data.push(filtered ? (uint8_t)0:
				(uint8_t)entry.value.val._boolean);
			break;
		case SHS_TYPE_INT:
			ok = ok && data.push(filtered ? (int)0:
				entry.value.val._int);
			break;
		case SHS_TYPE_DOUBLE:
			ok = ok && data.push(filtered ? (double)0.0 :
				entry.value.val._double);
			break;
		case SHS_TYPE_STRING:
			ok = ok && data.pushString(filtered ? "********" :
				entry.value.val._cstring);
			break;
		}

		if (!ok)
			break;

		/* Remove from queue, update lengths */
		mEntries.pop_front();
		writelen = data.used();
	}

	return writelen;
}

uint32_t ShsLogSource::getPeriodMs()
{
	return PERIOD_MS;
}

void ShsLogSource::start()
{
	int res = 0;

	/* Create context, attach to loop */
	mShsCtx = shs_ctx_new_client(&ShsLogSource::shsCb, this);
	assert(mShsCtx != nullptr);
	res = shs_ctx_pomp_loop_register(mShsCtx, mLoop);
	if (res < 0)
		ULOG_ERRNO("shs_ctx_pomp_loop_register", -res);

	/* Subscribe to all settings */
	res = shs_ctx_subscribe(mShsCtx, "*", &ShsLogSource::shsCb, this);
	if (res < 0)
		ULOG_ERRNO("shs_ctx_subscribe", -res);

	/* Start context */
	res = shs_ctx_start(mShsCtx);
	if (res < 0)
		ULOG_ERRNO("shs_ctx_start", -res);
}

void ShsLogSource::stop()
{
	shs_ctx_stop(mShsCtx);
	shs_ctx_pomp_loop_unregister(mShsCtx, mLoop);
	shs_ctx_destroy(mShsCtx);
	mShsCtx = nullptr;
}

void ShsLogSource::onShsCb(enum shs_evt evt,
		const struct shs_entry *old_entries,
		const struct shs_entry *new_entries,
		size_t count)
{
	struct timespec ts = {0, 0};
	time_get_monotonic(&ts);

	switch (evt) {
	case SHS_EVT_CONNECTED: /* NO BREAK */
	case SHS_EVT_UPDATED:
		for (size_t i = 0; i < count; i++)
			mEntries.push_back(Entry(&ts, &new_entries[i]));
		break;

	case SHS_EVT_DISCONNECTED: /* NO BREAK */
	case SHS_EVT_LOADING:
		/* Nothing to do */
		break;
	}
}

extern "C" void loggerd_plugin_init(loggerd::LogManager *manager,
		struct pomp_loop *loop, loggerd::LogPlugin **plugin)
{
	ShsPlugin *_plugin = new ShsPlugin();
	ShsLogSource *logSource = new ShsLogSource(_plugin, loop);

	logSource->start();
	manager->addLogSource(logSource, PLUGIN_NAME, SOURCE_NAME, VERSION);
	*plugin = _plugin;
}

extern "C" void loggerd_plugin_shutdown(loggerd::LogManager *manager,
		struct pomp_loop *loop, loggerd::LogPlugin *plugin)
{
	ShsPlugin *_plugin = static_cast<ShsPlugin *>(plugin);
	delete _plugin;
}
