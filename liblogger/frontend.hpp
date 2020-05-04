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

#ifndef _FRONTEND_HPP_
#define _FRONTEND_HPP_

#include <openssl/md5.h>
#include <functional>

namespace loggerd {

/* Forward declarations */
class Backend;
class BaseSource;

/**
 * Front end class that act as an intermediate between logger and the backend
 * to handle header/footer and automatic close on some condition.
 */
class LogFrontend {
public:
	typedef std::function<void ()> StartSession;
public:
	enum class CloseReason {
		/* Unknown reason */
		UNKNOWN = 0,
		/* Default value at init and when file has not been closed */
		NOT_CLOSED,
		/* Normal close, dameon is exiting */
		EXITING,
		/* Log is temporarily disabled */
		DISABLED,
		/* No space left on storage */
		NO_SPACE_LEFT,
		/* File is too big */
		FILE_TOO_BIG,
		/* Max space reserved for logs as been reached */
		QUOTA_REACHED,
		/* Rotation of files will be done */
		ROTATE,
	};
	static const char *closeReasonStr(CloseReason reason);

	LogFrontend(const loggerd::Loggerd::Options &opt,
		LogBackend *backend,
		BaseSource *headerSource,
		BaseSource *footerSource,
		const StartSession &startSession);
	~LogFrontend();

	void enableMd5();

	/* Open log and write header */
	int open();

	/* Close log file with given reason that will be written in a footer */
	void close(CloseReason reason);

	bool isOpened();

	inline CloseReason getCloseReason() const
	{ return mCloseReason; }

	/* Rewrite the date in the log header once it is sure to be valid */
	void updateDate();
	void updateExtraProperty(const std::string &key, const std::string &value);
	void updateFlightId(const char *flight_id);

	/* Rewrite gcs information in log header */
	void updateGcsName(const char *message);
	void updateGcsType(const char *message);

	/* Rewrite reference time and associated time stamp in header */
	void updateRefTime(const char *message, time_t tv_sec, long tv_nsec);
	void updateTakeoff(bool takeoff);

	/* Rewrite the md5 in the log header once the file footer is written */
	void updateMd5();

	/* Sync log file on the backend storage */
	void sync();

	/* Write data to the backend, if 'quiet' is given no further logs
	 * shall be generated to avoid avalanche effect */
	void write(const void *buf, size_t len, bool quiet);
	void writev(const struct iovec *iov, int iovcnt, bool quiet, bool isHeader = false);

private:
	void updateGcsField(off_t *off, size_t *s, const char *desc,
							const char *message);
	void updateField(off_t *off, size_t *s, const char *data,
			const char *desc, int size = -1);
	bool writehdrField(off_t *off, size_t *s, const char *key,
			const char *value, LogData &logData,
			size_t prev, int len = -1);
	void writeHeader();
	void writeFooter(CloseReason reason);
	/* Calculate free space left, and update mRemoveSize if it's less than
	 * the limit.
	 * Return true if mRemoveSize was updated, false otherwise. */
	bool updateRemoveSizeForFreeSpace(bool quiet);
	/* Calculate used space, and update mRemoveSize if it's above the limit.
	 * Return true if mRemoveSize was updated, false otherwise. */
	bool updateRemoveSizeForUsedSpace(bool quiet);
	/* Calculate current log size.
	 * Return true if it's above the limit, false otherwise. */
	bool checkLogSize(bool quiet);
	void updateRemoveSize();
	size_t computeUsedSpace();

	const StartSession mStartSession;
	std::string	mMonotonic;
	std::string	mAbsolute;
	bool		mTakeoff;
	MD5_CTX 	mCtx;
	Loggerd::Options mOpt;
	char		mMd5[(MD5_DIGEST_LENGTH * 2) + 1];
	char		mGcsName[GCS_DEFAULT_SIZE + 1];
	char		mGcsType[GCS_DEFAULT_SIZE + 1];
	size_t		mUsedSpace;
	LogBackend	*mBackend;
	BaseSource	*mHeaderSource;
	BaseSource	*mFooterSource;
	off_t		mFlightIdOff;
	off_t		mTakeoffOff;
	off_t		mDateOff;
	off_t		mMd5Off;
	off_t		mMonotonicOff;
	off_t		mAbsoluteOff;
	off_t		mGcsNameOff;
	off_t		mGcsTypeOff;
	size_t		mFlightIdSize;
	size_t		mTakeoffSize;
	size_t		mAbsoluteSize;
	size_t		mMonotonicSize;
	size_t		mDateSize;
	size_t		mMd5Size;
	size_t		mGcsNameSize;
	size_t		mGcsTypeSize;
	bool		mMd5Enabled;
	bool		mClosing;
	bool		mCheckSpace;
	CloseReason 	mCloseReason;
	size_t		mRemoveSize;
	int		mIndex;
};

} /* namespace loggerd */

#endif /* !_FRONTEND_HPP_ */
