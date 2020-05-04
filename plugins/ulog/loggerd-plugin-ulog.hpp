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

#ifndef _LOGGERD_PLUGIN_ULOG_HPP_
#define _LOGGERD_PLUGIN_ULOG_HPP_

#include <functional>

#include "loggerd-plugin.hpp"

/* Forward declaration */
struct ulog_entry;

namespace loggerd {

class UlogSource : public LogSource {
public:
	static const std::string SOURCE_TYPE;
	static const uint32_t VERSION = 1;

public:
	UlogSource(int fd, const std::string &name,
			LogDirectWriter *directWriter,
			loggerd::LogManager *manager);
	virtual ~UlogSource();
	virtual size_t readData(loggerd::LogData &data) override;
	virtual uint32_t getPeriodMs() override;

protected:
	/* Return true if entry shall be kept, false to skip it */
	inline virtual bool filterEntry(const struct ulog_entry *entry)
	{ return true; }
	bool filterGcsName(const struct ulog_entry *entry);
	bool filterGcsType(const struct ulog_entry *entry);
	bool filterTime(const struct ulog_entry *entry);

private:
	int mFd;
	int32_t mPid;
	std::string mName;
	loggerd::LogManager *mManager;
	loggerd::LogDirectWriter *mDirectWriter;
};

class UlogFactory {
public:
	/* Return ulog source instance or null if not interrested in it */
	typedef std::function<UlogSource *(
			int fd,
			const std::string &name,
			LogDirectWriter *directWriter,
			loggerd::LogManager *manager)> CreateFunc;


	static void createSources(LogManager *manager,
			const CreateFunc &createFunc=defaultCreateSource);


	inline static UlogSource *defaultCreateSource(int fd,
			const std::string &name,
			LogDirectWriter *directWriter,
			loggerd::LogManager *manager)
	{
		return new UlogSource(fd, name, directWriter, manager);
	}
};

}  /* namespace loggerd */

#endif /* !_LOGGERD_PLUGIN_ULOG_HPP_ */