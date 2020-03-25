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

#include "headers.hpp"

namespace loggerd {

BaseSource::BaseSource(uint32_t id,
		const std::string &plugin,
		const std::string &name,
		uint32_t version)
{
	mId = id;
	mPlugin = plugin;
	mName = name;
	mVersion = version;
	mPendingDescription = true;
	mPendingRemove = false;
}

BaseSource::~BaseSource()
{
}

ssize_t BaseSource::fillDescription(uint8_t *buf, size_t len)
{
	bool ok = true;
	struct loggerd_entry_header hdr;

	if (len < sizeof(hdr))
		return -1;

	/* push source description after room for header */
	LogData logData(buf + sizeof(hdr), len - sizeof(hdr));
	ok = ok && logData.push(mId);
	ok = ok && logData.push(mVersion);
	ok = ok && logData.pushString(mPlugin);
	ok = ok && logData.pushString(mName);
	if (!ok)
		return -1;

	/* insert entry header */
	hdr.id = LOGGERD_ID_SOURCE_DESC;
	hdr.len = logData.used();
	memcpy(buf, &hdr, sizeof(hdr));

	return sizeof(hdr) + hdr.len;
}

void BaseSource::startSession()
{
	/* will need to push description next time */
	mPendingDescription = true;
}

Source::Source(LogSource *source,
		uint32_t id,
		const std::string &plugin,
		const std::string &name,
		uint32_t version) : BaseSource(id, plugin, name, version)
{
	mSource = source;
	mDeadline = 0;
}

Source::~Source()
{
}

void Source::startSession()
{
	/* call base class method and then implementation specific code */
	BaseSource::startSession();
	mSource->startSession();
}

} /* namespace loggerd */
