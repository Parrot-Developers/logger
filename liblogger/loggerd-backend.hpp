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

#ifndef _LOGGERD_BACKEND_HPP_
#define _LOGGERD_BACKEND_HPP_

#include <stdlib.h>
#include <stdint.h>
#include <string>
#include <sys/uio.h>

namespace loggerd {

class LogFile {
public:
	LogFile(struct loghdr *hdr, std::string path, uint32_t idx, size_t size);
	LogFile();

public:
	bool operator < (const LogFile &log) const;

public:
	struct loghdr *mHdr;
	std::string mPath;
	uint32_t mIdx;
	size_t mSize;
};

/* log backend interface */
class LogBackend {
public:
	inline virtual ~LogBackend() {}
	virtual int open() = 0;
	virtual void setMinLogId(uint32_t minLogId) = 0;
	virtual uint32_t getMinLogId() = 0;
	virtual void rotate(size_t removeSize, uint32_t maxFileCount) = 0;
	virtual void close() = 0;
	virtual bool isOpened() = 0;
	virtual void sync() = 0;
	virtual size_t size() = 0;
	virtual void write(const void *buf, size_t len, bool quiet) = 0;
	virtual void writev(const struct iovec *iov, int iovcnt, bool quiet) = 0;
	virtual void pwrite(const void *buf, size_t len, off_t offset) = 0;
	virtual int unlink(std::vector<LogFile>::const_iterator &it,
							size_t &removeSize) = 0;
};

}

#endif /* !_LOGGERD_BACKEND_HPP_ */
