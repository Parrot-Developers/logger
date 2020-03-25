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

#ifndef _BUFFER_HPP_
#define _BUFFER_HPP_

namespace loggerd {

/* Forward declarations */
class LogFrontend;

/*
 * Buffer class.
 */
class Buffer {
public:
	Buffer(LogFrontend *frontend);
	~Buffer();

	/*
	 * flushSize is a filling threshold above which a flush is triggered
	 * minSpace is the desired minimum space returned by getWriteSpace()
	 */
	bool init(size_t flushSize, size_t minSpace);

	bool enableEncryption(const std::string &pubKeyPath);

	/* Discard pending data and delete encryption context */
	void reset();

	uint8_t *getWriteHead();
	size_t getWriteSpace();
	void push(size_t size);
	void flush();

private:
	size_t			mFlushThreshold;
	LogFrontend		*mFrontend;

	// Write
	uint8_t			*mWriteBuffer;
	size_t			mWriteBufferSize;
	uint8_t			*mWriteHead;
	size_t			mWriteBufferUsed;

	// Compression
	LZ4F_preferences_t	mLz4Prefs;
	uint8_t			*mLz4Buffer;
	size_t			mLz4BufferSize;

	// Encryption
	EVP_CIPHER_CTX		*mAesCtx;
	uint8_t			*mAesBuffer;
	size_t			mAesBufferSize;
};

} // namespace loggerd

#endif /* !_BUFFER_HPP_ */
