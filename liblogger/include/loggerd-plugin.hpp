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

#ifndef _LOGGERD_PLUGIN_HPP_
#define _LOGGERD_PLUGIN_HPP_

#include <stdint.h>
#include <string>

struct pomp_loop;

namespace loggerd {

class Plugin;

/* encapsulates data buffer to help pushing stuff */
class LogData {
public:
	inline LogData(uint8_t *data, size_t len)
	{
		mBase = data;
		mLen = len;
		mCurrent = mBase;
	}

	inline size_t used() const { return mCurrent - mBase; }
	inline size_t remaining() const { return mLen - used(); }
	inline uint8_t *current() const { return mCurrent; }

	/* Skip 'count' bytes in data */
	inline bool skip(size_t count)
	{
		if (count > remaining())
			return false;
		mCurrent += count;
		return true;
	}

	/* Rewind 'count' bytes in data */
	inline bool rewind(size_t count)
	{
		if (count > used())
			return false;
		mCurrent -= count;
		return true;
	}

	/* Push a value */
	template <typename T>
	inline bool push(const T &val)
	{
		return pushBuffer(&val, sizeof(val));
	}

	/* Push a buffer */
	inline bool pushBuffer(const void *buf, size_t len)
	{
		if (len > remaining())
			return false;
		memcpy(mCurrent, buf, len);
		mCurrent += len;
		return true;
	}

	/* Push a string */
	inline bool pushString(const std::string &str)
	{
		return pushString(str.c_str(), str.length());
	}

	inline bool pushString(const char *str, int len=-1)
	{
		/* Compute length if needed */
		if (len == -1)
			len = strlen(str);


		/* Check if string is too big */
		if (len + 1 > UINT16_MAX)
			return false;

		/* Check if enough room for length + string */
		uint16_t slen = len + 1;
		if (sizeof(slen) + slen > remaining())
			return false;

		/* Push length + string */
		push(slen);
		pushBuffer(str, slen);
		return true;
	}

	inline bool pushIntAsString(int value)
	{
		char buffer[10+1];
		snprintf(buffer, sizeof(buffer), "%d", value);
		return pushString(buffer);
	}

private:
	uint8_t	*mBase;
	size_t	mLen;
	uint8_t	*mCurrent;
};

/* log plugin interface */
class LogPlugin {
public:
	inline virtual ~LogPlugin() {}
	inline virtual void setSettings(const std::string &val) {}
	virtual const std::string &getName() const = 0;
};

/* log source interface */
class LogSource {
public:
	inline virtual ~LogSource() {}
	virtual size_t readData(loggerd::LogData &data) = 0;
	virtual uint32_t getPeriodMs() = 0;
	inline virtual void startSession() {}
};

/* log direct write interface */
class LogDirectWriter {
public:
	inline virtual ~LogDirectWriter() {}
	virtual void write(const void *buf, size_t len) = 0;
};

/* interface for managing (adding/removing) log sources */
class LogManager {
public:
	inline virtual ~LogManager() {}
	virtual int addLogSource(loggerd::LogSource *source,
			const std::string &plugin,
			const std::string &name,
			uint32_t version) = 0;
	virtual void removeLogSource(loggerd::LogSource *source) = 0;
	virtual LogDirectWriter *getDirectWriter(const std::string &plugin,
						 uint32_t version) = 0;
	virtual void flush(const char *reason) = 0;
	virtual void updateDate() = 0;
	virtual void updateGcsName(const char *message) = 0;
	virtual void updateGcsType(const char *message) = 0;
	virtual void updateExtraProperty(const std::string &key,
						const std::string &value) = 0;
	virtual void updateFlightId(const char *flight_id) = 0;
	virtual void updateRefTime(const char *message, time_t tv_sec,
				   long tv_nsec) = 0;
	virtual void updateTakeoff(bool takeoff) = 0;
	virtual void rotate() = 0;
	virtual void enableMd5() = 0;
	virtual void setEnabled(bool enabled) = 0;
	virtual void pollSources(bool force) = 0;
};

class SettingsManager {
public:
	inline SettingsManager(struct pomp_loop *loop) { mLoop = loop; }
	inline virtual ~SettingsManager() {}

public:
	virtual void initSettings(LogManager *manager) = 0;
	virtual void cleanSettings() = 0;
	virtual void startSettings() = 0;
	virtual void configureSettings(Plugin *plugin) = 0;

protected:
	struct pomp_loop *mLoop;
};

class LogIdxManager {
public:
	inline LogIdxManager()
	{ mIndex = 0; }

	inline virtual ~LogIdxManager() {}

public:
	virtual uint32_t getIndex() = 0;
	virtual int getIndexStr(char *value, size_t size) = 0;
	virtual void setIndex(uint32_t index) = 0;

protected:
	uint32_t mIndex;
};

} /* namespace loggerd */

/* plugin C interface */
extern "C" void loggerd_plugin_init(loggerd::LogManager *manager,
		struct pomp_loop *loop, loggerd::LogPlugin **plugin);

extern "C" void loggerd_plugin_shutdown(loggerd::LogManager *manager,
		struct pomp_loop *loop, loggerd::LogPlugin *plugin);
#endif /* _LOGGERD_PLUGIN_HPP_ */
