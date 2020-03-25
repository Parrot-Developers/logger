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

#ifndef _SOURCE_HPP_
#define _SOURCE_HPP_

namespace loggerd {

/* Forward declarations */
class LogSource;

/**
 * Internal Log source base class.
 */
class BaseSource {
public:
	BaseSource(uint32_t id,
			const std::string &plugin,
			const std::string &name,
			uint32_t version);
	virtual ~BaseSource();

	ssize_t fillDescription(uint8_t *buf, size_t len);
	virtual void startSession();

public:
	uint32_t	mId;
	std::string	mPlugin;
	std::string	mName;
	uint32_t	mVersion;
	bool		mPendingDescription;
	bool		mPendingRemove;
};

/**
 * Internal Log source wrapper class.
 */
class Source : public BaseSource {
public:
	Source(LogSource *source,
			uint32_t id,
			const std::string &plugin,
			const std::string &name,
			uint32_t version);
	~Source();

	virtual void startSession() override;

public:
	uint64_t	mDeadline;
	LogSource	*mSource;
};

} /* namespace loggerd */

#endif /* !_SOURCE_HPP_ */
