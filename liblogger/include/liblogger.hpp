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

#ifndef _LIBLOGGER_HPP_
#define _LIBLOGGER_HPP_

#include <loggerd-plugin.hpp>

#include <map>
#include <string>
#include <vector>

namespace loggerd {

/**
 * Plugin class.
 */
class Plugin {
public:
	Plugin();
	virtual ~Plugin() {};

	inline bool autoUnload() const
	{
		return mAutoUnload;
	}

	inline void setAutoUnload(bool autoUnload)
	{
		mAutoUnload = autoUnload;
	}

	inline LogPlugin *getPlugin() const
	{
		return mPlugin;
	}

	virtual int load() = 0;
	virtual void unload() = 0;
	virtual void init(LogManager *manager, struct pomp_loop *loop) = 0;
	virtual void shutdown(LogManager *manager, struct pomp_loop *loop) = 0;

	inline const std::string &getName() const
	{
		return mPlugin != nullptr ? mPlugin->getName() : mName;
	}

private:
	bool mAutoUnload;

protected:
	std::string	mName;
	LogPlugin	*mPlugin;
	LogManager	*mManager;
};

class Loggerd {
public:
	enum class Security {
		NONE,
		NOT_DEBUG,
		ALWAYS,
	};

	struct ExtraProperty {
		std::string key;
		std::string value;
		size_t size;
		off_t offset;
		bool readOnly;

		/*
		 * If value is the empty string, key can be update only once per header creation.
		 * Else the value associated to key stays the same during the entire library
		 * instantiation. Size is used to reserve storage to store value.
		 */
		inline ExtraProperty(const std::string &_key, size_t _size,
						const std::string &_value = "")
		{
			size = 0;
			offset = 0;
			key = _key;
			readOnly = _value != "";
			if (_value.length() < _size)
				value = _value + std::string(_size - _value.length(), 'F');
			else
				value = std::string(_value, 0, _size);
		}
	};

	struct Options {
		bool encrypted;
		std::string outputDir;
		uint32_t maxLogCount;
		size_t minFreeSpace;
		size_t maxUsedSpace;
		size_t maxLogSize;
		size_t minLogSize;
		LogIdxManager *logIdxManager;
		std::vector<ExtraProperty> extraProps;

		inline Options()
			: encrypted(false), maxLogCount(0), minFreeSpace(0),
			maxUsedSpace(0), maxLogSize(0), minLogSize(0),
			logIdxManager(nullptr)
		{
		}
	};

	virtual int loadPlugins(const std::string &pluginDir) = 0;
	virtual int loadPlugins(const std::vector<Plugin *> &plugins) = 0;
	virtual void unloadPlugins() = 0;
	virtual void destroyLogSources() = 0;
	virtual void destroyDirectWriters() = 0;

	virtual void stop() = 0;
	virtual void start() = 0;
	virtual void requestFlush() = 0;
	virtual void requestRotate() = 0;

	static Loggerd* create(struct pomp_loop *loop,
			       const Options &opt,
			       SettingsManager &settingsManager
			       );
	static void destroy(Loggerd *loggerd);

protected:
	Loggerd() {};
	virtual ~Loggerd() {};
};

} /* namespace loggerd */

#endif /* _LIBLOGGER_HPP_ */
