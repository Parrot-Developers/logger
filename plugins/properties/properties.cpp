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
#include <string>
#include <map>
#include <algorithm>
#include <vector>

#include <loggerd-plugin.hpp>
#include <libautopilot.h>
#include <libpomp.h>
#include <putils/properties.h>
#include <putils/propmon.h>
#include <futils/futils.h>

#define ULOG_TAG loggerd_props
#include <ulog.h>
ULOG_DECLARE_TAG(ULOG_TAG);

#define SOURCE_NAME	"properties"
#define PERIOD_MS	1000

static const std::string PLUGIN_NAME = "properties";

/* To be changed whenever the layout of data of source is changed */
#define VERSION		1

class PropPlugin : public loggerd::LogPlugin {
public:
	PropPlugin(loggerd::LogManager *manager, struct pomp_loop *loop);
	~PropPlugin();

public:
	inline virtual const std::string &getName() const override
	{
		return PLUGIN_NAME;
	}

	inline void flyingStateChanged(const char *val)
	{
		bool is_landed;
		enum autopilot_flying_state flying_state;

		flying_state = autopilot_flying_state_from_str(val);
		is_landed = autopilot_is_landed(flying_state);

		if (mIsLanded && !is_landed)
			mManager->updateTakeoff(true);

		mIsLanded = is_landed;
	}

	virtual void setSettings(const std::string &val) override;

public:
	typedef std::vector<std::string>		ValueVector;
	typedef std::map<std::string, ValueVector>	FlushMap;
	FlushMap					mFlushProperties;
	loggerd::LogManager				*mManager;
	bool						mIsLanded;
};

class PropLogSource : public loggerd::LogSource {
public:
	PropLogSource(PropPlugin *plugin, struct pomp_loop *loop);
	virtual ~PropLogSource();

public:
	virtual size_t readData(loggerd::LogData &data) override;
	virtual uint32_t getPeriodMs() override;

public:
	void start();
	void stop();

private:
	static void fdCb(int fd, uint32_t revents, void *userdata)
	{
		PropLogSource *self =
				reinterpret_cast<PropLogSource *>(userdata);
		struct property_change pch;
		while (propmon_receive(self->mPropMon, &pch) == 0)
			self->onPropertyChanged(&pch);
	}
	void onPropertyChanged(const struct property_change *pch);

private:
	struct Entry {
		struct timespec	ts;
		char		key[SYS_PROP_KEY_MAX];
		char		value[SYS_PROP_VALUE_MAX];

		inline Entry()
		{
			this->ts.tv_sec = 0;
			this->ts.tv_nsec = 0;
			memset(this->key, 0, sizeof(this->key));
			memset(this->value, 0, sizeof(this->value));
		}

		inline Entry(const struct timespec *ts,
				const char *key,
				const char *value)
		{
			this->ts = *ts;
			strncpy(this->key, key, SYS_PROP_KEY_MAX);
			this->key[SYS_PROP_KEY_MAX - 1] = '\0';
			strncpy(this->value, value, SYS_PROP_VALUE_MAX);
			this->value[SYS_PROP_VALUE_MAX - 1] = '\0';
		}

		inline Entry(const struct timespec *ts,
				const struct property_change *pch)
		{
			this->ts = *ts;
			strncpy(this->key, pch->key, SYS_PROP_KEY_MAX);
			this->key[SYS_PROP_KEY_MAX - 1] = '\0';
			strncpy(this->value, pch->value, SYS_PROP_VALUE_MAX);
			this->value[SYS_PROP_VALUE_MAX - 1] = '\0';
		}
	};

	typedef std::deque<Entry>	EntryDeque;

private:
	PropPlugin              *mPlugin;
	struct pomp_loop	*mLoop;
	struct propmon		*mPropMon;
	EntryDeque		mEntries;
};

PropLogSource::PropLogSource(PropPlugin *plugin, struct pomp_loop *loop)
{
	mLoop = loop;
	mPropMon = nullptr;
	mPlugin = plugin;
}

PropLogSource::~PropLogSource()
{
	stop();
	assert(mPropMon == nullptr);
}

size_t PropLogSource::readData(loggerd::LogData &data)
{
	size_t writelen = 0;

	while (!mEntries.empty()) {
		/* Get entry data */
		const Entry &entry = mEntries.front();
		uint32_t tv_sec = entry.ts.tv_sec;
		uint32_t tv_nsec = entry.ts.tv_nsec;

		/* Try to push entry, it it fails, abort loop */
		bool ok = true;
		ok = ok && data.push(tv_sec);
		ok = ok && data.push(tv_nsec);
		ok = ok && data.pushString(entry.key);
		ok = ok && data.pushString(entry.value);
		if (!ok)
			break;

		/* Remove from queue, update lengths */
		mEntries.pop_front();
		writelen = data.used();
	}

	return writelen;
}

uint32_t PropLogSource::getPeriodMs()
{
	return PERIOD_MS;
}

void PropLogSource::start()
{
	int res = 0;

	/* Create monitor, attach to loop */
	mPropMon = propmon_new();
	assert(mPropMon != nullptr);
	res = pomp_loop_add(mLoop, propmon_get_fd(mPropMon), POMP_FD_EVENT_IN,
			&PropLogSource::fdCb, this);
	if (res < 0)
		ULOG_ERRNO("pomp_loop_add", -res);

	res = propmon_start(mPropMon);
	if (res < 0)
		ULOG_ERRNO("propmon_start", -res);

	struct Context {
		struct timespec ts;
		PropLogSource *self;
	} ctx = { { 0, 0 }, this };
	time_get_monotonic(&ctx.ts);

	sys_prop_list([](const char *key,
			const char *value,
			void *cookie) {
		Context *ctx = reinterpret_cast<Context *>(cookie);
		ctx->self->mEntries.push_back(Entry(&ctx->ts, key, value));
	}, &ctx);
}

void PropLogSource::stop()
{
	pomp_loop_remove(mLoop, propmon_get_fd(mPropMon));
	propmon_destroy(mPropMon);
	mPropMon = nullptr;
}

void PropLogSource::onPropertyChanged(const struct property_change *pch)
{
	bool found = false;
	struct timespec ts = {0, 0};
	time_get_monotonic(&ts);

	/* check if property is in the list of properties triggering a flush */
	auto it = mPlugin->mFlushProperties.find(pch->key);
	if (it != mPlugin->mFlushProperties.end()) {
		found = std::any_of(it->second.begin(), it->second.end(),
				[&](const std::string &val) {
			return val == "*" || val == pch->value;
		});
	}
	if (found) {
		std::string reason = std::string(pch->key) + "=" +
			std::string(pch->value);
		mPlugin->mManager->flush(reason.c_str());
	}

	/* Update file date when a controller is connected
	 * When utc_off is set we are sure to have both system date and
	 * timezone */
	if (strcmp(pch->key, "persist.last.conn.date.utc_off") == 0)
		mPlugin->mManager->updateDate();
	else if (strcmp(pch->key, AUTOPILOT_FLYING_STATE_PROP) == 0)
		mPlugin->flyingStateChanged(pch->value);

	mEntries.push_back(Entry(&ts, pch));
}

PropPlugin::PropPlugin(loggerd::LogManager *manager, struct pomp_loop *loop)
{
	enum autopilot_flying_state flying_state;

	flying_state = autopilot_get_flying_state();

	mManager = manager;
	mIsLanded = autopilot_is_landed(flying_state);
}

PropPlugin::~PropPlugin()
{
}

void PropPlugin::setSettings(const std::string &val)
{
	size_t start = 0, end = 0;

	mFlushProperties.clear();

	if (val.empty())
		return;

	/* Search <key>[=<value>] separated by '|' */
	do {
		end = val.find('|', start);
		std::string item_key, item_val;
		std::string item = (end != std::string::npos ?
				    val.substr(start, end - start) :
				    val.substr(start));

		size_t pos = item.find('=');
		if (pos != std::string::npos) {
			item_key = item.substr(0, pos);
			item_val = item.substr(pos + 1);
		} else {
			item_key = item;
			item_val = "*";
		}

		mFlushProperties[item_key].push_back(item_val);
		ULOGI("flush will be triggered on property %s=%s",
		      item_key.c_str(), item_val.c_str());

		start = end + 1;
	} while (end != std::string::npos);
}

extern "C" void loggerd_plugin_init(loggerd::LogManager *manager,
		struct pomp_loop *loop, loggerd::LogPlugin **plugin)
{
	PropPlugin *_plugin = new PropPlugin(manager, loop);
	PropLogSource *logSource = new PropLogSource(_plugin, loop);

	logSource->start();
	manager->addLogSource(logSource, PLUGIN_NAME, SOURCE_NAME, VERSION);
	*plugin = _plugin;
}

extern "C" void loggerd_plugin_shutdown(loggerd::LogManager *manager,
		struct pomp_loop *loop, loggerd::LogPlugin *plugin)
{
	PropPlugin *_plugin = static_cast<PropPlugin *>(plugin);
	delete _plugin;
}
