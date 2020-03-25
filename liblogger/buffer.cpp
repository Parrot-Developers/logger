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

#define CHECK(_x) do { if (!(ok = (_x))) goto out; } while (0)

#define AES_BLOCK_SIZE	16

namespace loggerd {

Buffer::Buffer(LogFrontend *frontend)
{
	mFlushThreshold = 0;
	mFrontend = frontend;

	mWriteBuffer = nullptr;
	mWriteBufferSize = 0;
	mWriteHead = nullptr;
	mWriteBufferUsed = 0;

	memset(&mLz4Prefs, 0, sizeof(mLz4Prefs));
	mLz4Buffer = nullptr;
	mLz4BufferSize = 0;

	mAesCtx = nullptr;
	mAesBuffer = nullptr;
	mAesBufferSize = 0;

	mAesCtx = nullptr;
	mAesBuffer = nullptr;
	mAesBufferSize = 0;
}

Buffer::~Buffer()
{
	reset();

	free(mWriteBuffer);
	mWriteBuffer = nullptr;

	free(mLz4Buffer);
	mLz4Buffer = nullptr;
}

bool Buffer::init(size_t flushSize, size_t minSpace)
{
	bool ok = true;

	/* Setup write buffer */
	mFlushThreshold = flushSize;
	mWriteBufferSize = flushSize + minSpace;
	mWriteBuffer = (uint8_t *)malloc(mWriteBufferSize);
	CHECK(mWriteBuffer != nullptr);
	mWriteHead = mWriteBuffer;

	/* Setup lz4 compression */
	mLz4Prefs.frameInfo.contentChecksumFlag = contentChecksumEnabled;

	/* Level 1 gives ~1/3 compression ratio with minimal CPU usage */
	mLz4Prefs.compressionLevel = 1;
	mLz4Prefs.autoFlush = 1;

	/* Setup compressed buffer
	 * Add room for header and optional aes padding */
	mLz4BufferSize = LZ4F_compressFrameBound(mWriteBufferSize, &mLz4Prefs);
	mLz4BufferSize += sizeof(struct loggerd_entry_header);
	mLz4BufferSize += AES_BLOCK_SIZE;
	mLz4Buffer = (uint8_t *)malloc(mLz4BufferSize);
	CHECK(mLz4Buffer != nullptr);

out:
	return ok;
}

void Buffer::reset()
{
	/* Reset buffer */
	mWriteHead = mWriteBuffer;
	mWriteBufferUsed = 0;

	/* Cleanup aes context */
	if (mAesCtx != nullptr)
		EVP_CIPHER_CTX_free(mAesCtx);
	mAesCtx = nullptr;
	free(mAesBuffer);
	mAesBuffer = nullptr;
	mAesBufferSize = 0;
}

bool Buffer::enableEncryption(const std::string &pubKeyPath)
{
	bool ok = true;
	FILE *fp = nullptr;
	RSA *rsa = nullptr;
	uint8_t *pubKeyDer = nullptr;
	int pubKeyDerSize = 0;
	uint8_t pubKeyDerHash[SHA256_DIGEST_LENGTH];
	EVP_PKEY *pkey = nullptr;
	const EVP_CIPHER *cipher = nullptr;
	uint8_t *aesEncryptedKey = nullptr;
	size_t aesEncryptedKeySize = 0;
	uint8_t *aesIv = nullptr;
	size_t aesIvSize = 0;
	struct loggerd_entry_header aesDescHdr;
	size_t off = 0;
	uint32_t len = 0;
	uint8_t *aesDesc = nullptr;
	size_t aesDescSize = 0;

	/* Open public key file */
	fp = fopen(pubKeyPath.c_str(), "rb");
	if (fp == nullptr) {
		ULOGE("Failed to open '%s': err=%d(%s)", pubKeyPath.c_str(),
			errno, strerror(errno));
		ok = false;
		goto out;
	}

	/* Load public key in PEM format */
	if (PEM_read_RSA_PUBKEY(fp, &rsa, NULL, NULL) == NULL) {
		ULOGE("Failed to load public key '%s'", pubKeyPath.c_str());
		ok = false;
		goto out;
	}

	/* Get sha256 of the asn.1 DER format of the public key */
	pubKeyDerSize = i2d_RSA_PUBKEY(rsa, &pubKeyDer);
	if (pubKeyDerSize < 0) {
		ULOGE("Failed to convert public key");
		ok = false;
		goto out;
	}
	if (SHA256(pubKeyDer, pubKeyDerSize, pubKeyDerHash) == NULL) {
		ULOGE("Failed to compute public key hash");
		ok = false;
		goto out;
	}

	/* Assign it in a evp key */
	pkey = EVP_PKEY_new();
	CHECK(pkey != nullptr);
	if (!EVP_PKEY_assign_RSA(pkey, rsa)) {
		ULOGE("Failed to assign public key");
		ok = false;
		goto out;
	}
	rsa = nullptr;

	/* Get aes 256 cbc cipher */
	cipher = EVP_aes_256_cbc();
	CHECK(cipher != nullptr);
	CHECK(EVP_CIPHER_block_size(cipher) == AES_BLOCK_SIZE);

	/* Allocate encrypted key (based on public key size) */
	aesEncryptedKeySize = EVP_PKEY_size(pkey);
	aesEncryptedKey = (uint8_t *)malloc(aesEncryptedKeySize);
	CHECK(aesEncryptedKey != nullptr);

	/* Allocate initialization vector */
	aesIvSize = EVP_CIPHER_iv_length(cipher);
	aesIv = (uint8_t *)malloc(aesIvSize);
	CHECK(aesIv != nullptr);

	/* Create cipher context */
	mAesCtx = EVP_CIPHER_CTX_new();
	CHECK(mAesCtx != nullptr);

	/* Setup seal by generating aes key and iv then encrypting aes key
	 * with the publick key (in a dedicated block to avoid errors about
	 * jump crosses initialization) */
	{
		uint8_t *ek[1] = { aesEncryptedKey };
		int ekl[1] = { 0 };
		EVP_PKEY *pubk[1] = { pkey };
		int n = EVP_SealInit(mAesCtx, cipher, ek, ekl, aesIv, pubk, 1);
		CHECK(n == 1);
		aesEncryptedKeySize = ekl[0];
	}

	/* Construct Aes description block */
	aesDescSize = sizeof(aesDescHdr) +
			3 * sizeof(uint32_t) +
			SHA256_DIGEST_LENGTH +
			aesEncryptedKeySize +
			aesIvSize;
	aesDesc = (uint8_t *)malloc(aesDescSize);
	CHECK(aesDesc != nullptr);
	off = 0;

	aesDescHdr.id = LOGGERD_ID_AES_DESC;
	aesDescHdr.len = aesDescSize - sizeof(aesDescHdr);
	memcpy(aesDesc + off, &aesDescHdr, sizeof(aesDescHdr));
	off += sizeof(aesDescHdr);

	len = SHA256_DIGEST_LENGTH;
	memcpy(aesDesc + off, &len, sizeof(len));
	off += sizeof(uint32_t);
	memcpy(aesDesc + off, pubKeyDerHash, len);
	off += len;

	len = aesEncryptedKeySize;
	memcpy(aesDesc + off, &len, sizeof(len));
	off += sizeof(uint32_t);
	memcpy(aesDesc + off, aesEncryptedKey, len);
	off += len;

	len = aesIvSize;
	memcpy(aesDesc + off, &len, sizeof(len));
	off += sizeof(uint32_t);
	memcpy(aesDesc + off, aesIv, len);
	off += len;

	/* Disable internal padding, we will manage it ourself */
	EVP_CIPHER_CTX_set_padding(mAesCtx, 0);

	/* Allocate encrypted output buffer */
	mAesBufferSize = sizeof(struct loggerd_entry_header) + mLz4BufferSize;
	mAesBuffer = (uint8_t *) malloc(mAesBufferSize);
	CHECK(mAesBuffer != nullptr);

	/* Write Aes description */
	mFrontend->write(aesDesc, aesDescSize, false);

out:
	free(aesDesc);
	free(aesEncryptedKey);
	free(aesIv);
	if (pubKeyDer != nullptr)
		OPENSSL_free(pubKeyDer);
	if (pkey != nullptr)
		EVP_PKEY_free(pkey);
	if (rsa != nullptr)
		RSA_free(rsa);
	if (fp != nullptr)
		fclose(fp);
	return ok;
}

uint8_t *Buffer::getWriteHead()
{
	return mWriteHead;
}

size_t Buffer::getWriteSpace()
{
	return mWriteBufferSize - mWriteBufferUsed;
}

void Buffer::push(size_t size)
{
	if (size <= (mWriteBufferSize - mWriteBufferUsed)) {
		ULOGD("pushed %zu bytes\n", size);
		mWriteBufferUsed += size;
		mWriteHead += size;
		if (mWriteBufferUsed >= mFlushThreshold)
			flush();
	} else {
		ULOGE("cannot push %zu bytes, buffer only has %zu bytes left!",
		      size, mWriteBufferSize - mWriteBufferUsed);
	}
}

void Buffer::flush()
{
	size_t lz4Len = 0, padLen;
	uint8_t padByte = 0;
	int aesInLen = 0, aesOutLen = 0;
	struct loggerd_entry_header lz4Hdr, aesHdr;

	if (mWriteBufferUsed == 0)
		/* nothing to flush */
		return;

	/* compress data into output buffer */
	lz4Len = LZ4F_compressFrame(mLz4Buffer + sizeof(lz4Hdr),
				 mLz4BufferSize - sizeof(lz4Hdr),
				 mWriteBuffer,
				 mWriteBufferUsed,
				 &mLz4Prefs);
	mWriteBufferUsed = 0;
	mWriteHead = mWriteBuffer;

	if (LZ4F_isError(lz4Len)) {
		ULOGE("LZ4F_compressFrame(%zu) failed with status %zu",
		      mWriteBufferUsed, lz4Len);
		return;
	}

	/* Add lz4 block header */
	lz4Hdr.id = LOGGERD_ID_LZ4;
	lz4Hdr.len = lz4Len;
	memcpy(mLz4Buffer, &lz4Hdr, sizeof(lz4Hdr));

	/* Output a single compressed log entry if no encryption */
	if (mAesCtx == nullptr) {
		mFrontend->write(mLz4Buffer, sizeof(lz4Hdr) + lz4Len, false);
		return;
	}

	/* Pad data to encrypt to have a multiple of aes block size.
	 * See PKCS#7 (RFC 2315 - 10.3 Content-encryption process)
	 * At least one byte is written and up to the block size.
	 * The value of the byte added is equal to the number of padding bytes
	 * added. This way at the reception it is easy to determine the number
	 * of bytes that was added */
	aesInLen = sizeof(lz4Hdr) + lz4Len;
	padLen = aesInLen % AES_BLOCK_SIZE == 0 ? AES_BLOCK_SIZE :
		AES_BLOCK_SIZE - aesInLen % AES_BLOCK_SIZE;
	padByte = padLen;
	memset(mLz4Buffer + aesInLen, padByte, padLen);
	aesInLen += padLen;

	/* Prepare header of encrypted block */
	aesHdr.id = LOGGERD_ID_AES;
	aesHdr.len = aesInLen;
	memcpy(mAesBuffer, &aesHdr, sizeof(aesHdr));

	/* Encrypt compressed buffer */
	if (!EVP_SealUpdate(mAesCtx, mAesBuffer + sizeof(aesHdr), &aesOutLen,
			mLz4Buffer, aesInLen)) {
		ULOGE("EVP_SealUpdate failed");
		return;
	}
	if (aesOutLen != aesInLen) {
		ULOGE("EVP_SealUpdate incomplete encryption");
		return;
	}

	/* Write to the backend */
	mFrontend->write(mAesBuffer, sizeof(aesHdr) + aesOutLen, false);
}

} /* namespace loggerd */
