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
#include <algorithm>

#define BACKEND_FILE_NAME		"log.bin"
#define BACKEND_FILE_PATTERN		"log-%u.bin"
#define BACKEND_FILE_PATTERN_NEW	"log-%u-%.5s-%20s.bin"

namespace loggerd {

class BackendFile : public LogBackend {
public:
	BackendFile(const std::string &outputDir);
	virtual ~BackendFile();

	virtual int unlink(std::vector<LogFile>::const_iterator &it,
							size_t &removeSize);
	virtual int open();
	virtual uint32_t getMinLogId();
	virtual void setMinLogId(uint32_t minLogId);
	virtual void rotate(size_t removeSize, uint32_t maxFileCount);
	virtual void close();
	virtual bool isOpened();
	virtual void sync();
	virtual size_t size();
	virtual void write(const void *buf, size_t len, bool quiet);
	virtual void writev(const struct iovec *iov, int iovcnt, bool quiet);
	virtual void pwrite(const void *buf, size_t len, off_t offset);

private:
	std::string	mOutputDir;
	std::string	mPath;
	uint32_t	mMinLogId;
	int		mFd;
};

BackendFile::BackendFile(const std::string &outputDir)
{
	mFd = -1;
	mMinLogId = 0;
	mOutputDir = outputDir;
	mPath = outputDir + "/" + BACKEND_FILE_NAME;
}

BackendFile::~BackendFile()
{
	if (mFd >= 0)
		close();
}

int BackendFile::open()
{
	int res = 0;
	if (mFd >= 0)
		return -EBUSY;

	int oflags = O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC;
	mFd = ::open(mPath.c_str(), oflags, 0644);
	if (mFd < 0) {
		res = -errno;
		ULOGE("open(%s): err=%d(%s)", mPath.c_str(),
				-res, strerror(-res));
		return res;
	}

	/* sync file creation in directory, errors are non fatal */
	int fd = ::open(mOutputDir.c_str(), O_RDONLY);
	if (fd >= 0) {
		int ret = ::fsync(fd);
		if (ret < 0) {
			ULOGE("fsync(%s): err=%d(%s)", mOutputDir.c_str(),
					errno, strerror(errno));
		}
		::close(fd);
	} else {
		ULOGE("open(%s): err=%d(%s)", mOutputDir.c_str(),
				errno, strerror(errno));
	}

	ULOGI("'%s' opened", mPath.c_str());
	return 0;
}

uint32_t BackendFile::getMinLogId()
{
	return mMinLogId;
}

void BackendFile::setMinLogId(uint32_t minLogId)
{
	mMinLogId = minLogId;
}

void BackendFile::close()
{
	if (mFd >= 0) {
		::close(mFd);
		ULOGI("'%s' closed", mPath.c_str());
		mFd = -1;
	}
}

bool BackendFile::isOpened()
{
	return mFd >= 0;
}

/**
 * Rotate log files to avoid losing previous log.
 */
void BackendFile::rotate(size_t removeSize, uint32_t maxFileCount)
{
	int ret = 0;
	char path[512] = "";
	DIR * dir = nullptr;
	std::vector<LogFile> files;
	struct loghdr *hdr = nullptr;
	struct dirent *entry = nullptr;
	uint32_t count = 0, highest = 0, idx = 0;

	/* If the default name does not exist, assume rotation is not needed */
	/* coverity[fs_check_call] */
	ret = ::access(mPath.c_str(), F_OK);
	if (ret < 0)
		return;
	count = 1;

	dir = opendir(mOutputDir.c_str());
	if (dir == nullptr) {
		ULOG_ERRNO("opendir('%s')", errno, mOutputDir.c_str());
		return;
	}

	while ((entry = readdir(dir)) != nullptr) {
		LogFile log;
		struct stat stats;

		if (sscanf(entry->d_name, BACKEND_FILE_PATTERN, &idx) != 1)
			continue;

		highest = idx > highest ? idx : highest;
		log.mPath = mOutputDir + "/" + entry->d_name;
		log.mIdx = idx;

		ret = lstat(log.mPath.c_str(), &stats);
		if (ret < 0) {
			ULOGD("lstat '%s' error:%s", log.mPath.c_str(),
							strerror(errno));
		} else {
			log.mHdr = loghdr_new(log.mPath.c_str());
			log.mSize = stats.st_size;
			files.push_back(log);
		}

		count++;
	}

	closedir(dir);

	/* Remove logs where we have not takeoff first, then we can remove
	 * the others Inside these logs, we remove the oldest first
	 * (The one with lowest index).
	 */
	std::sort(files.begin(), files.end());
	std::vector<LogFile>::const_iterator it = files.begin();
	while (((removeSize > 0) || (maxFileCount && count >= maxFileCount))
							       && (count > 1)) {
		if (it != files.end()) {
			if (unlink(it, removeSize) >= 0)
				count--;
		} else {
			break;
		}
	}

	/* Free loghdr pointer after use. */
	for (it = files.begin(); it != files.end(); it++)
		loghdr_destroy(it->mHdr);

	if (highest > mMinLogId)
		mMinLogId = highest;
	else
		highest = mMinLogId;

	/* Read log header to get date */
	hdr = loghdr_new(mPath.c_str());

	/* Rename current to next index */
	if (hdr && loghdr_has_key(hdr, "date") &&
					loghdr_has_key(hdr, "ro.boot.uuid")) {
		snprintf(path, sizeof(path), "%s/" BACKEND_FILE_PATTERN_NEW,
			 mOutputDir.c_str(), highest + 1,
			 loghdr_get_value(hdr, "ro.boot.uuid"),
			 loghdr_get_value(hdr, "date"));
	} else {
		snprintf(path, sizeof(path), "%s/" BACKEND_FILE_PATTERN,
			 mOutputDir.c_str(), highest + 1);
	}

	ULOGI("Renaming '%s' -> '%s'", mPath.c_str(), path);
	/* coverity[toctou] */
	ret = ::rename(mPath.c_str(), path);
	if ((ret < 0) && (errno != ENOENT))
		ULOG_ERRNO("rename('%s', '%s')", errno, mPath.c_str(), path);

	loghdr_destroy(hdr);
}

int BackendFile::unlink(std::vector<LogFile>::const_iterator &it,
							size_t &removeSize)
{
	const char *path = it->mPath.c_str();
	size_t size = it->mSize;
	std::string flight;
	int ret = 0;

	if (it->mHdr == nullptr || !loghdr_has_key(it->mHdr, "takeoff"))
		flight = "unknown";
	else if (loghdr_get_value(it->mHdr, "takeoff") == std::string("1"))
		flight = "true";
	else
		flight = "false";

	ULOG_EVT("LOGS", "event='remove';reason='ROTATE';flight='%s';"
			 "path='%s'", flight.c_str(), path);

	ret = ::unlink(path);
	if (ret < 0) {
		ULOG_ERRNO("unlink('%s')", errno, path);
		ret = -1;
	} else {
		removeSize = removeSize > size ? removeSize - size : 0;
	}
	it++;

	return ret;
}

void BackendFile::sync()
{
	int ret;

	if (mFd >= 0) {
		ret = ::fsync(mFd);
		if (ret < 0)
			ULOG_ERRNO("fsync", errno);
	}
}

size_t BackendFile::size()
{
	off_t ret;

	if (mFd < 0)
		return 0;

	ret = lseek(mFd, 0, SEEK_CUR);
	return (ret < 0) ? 0 : ret;
}


/**
 * Write data to log file. See 'writev'
 */
void BackendFile::write(const void *buf, size_t len, bool quiet)
{
	struct iovec iov[1];
	iov[0].iov_base = const_cast<void *>(buf);
	iov[0].iov_len = len;
	writev(iov, 1, quiet);
}

/**
 * Write data to log file.
 * The quiet flag indicates that the function should NOT log anything on its own
 * to avoid avalanche effect.
 */
void BackendFile::writev(const struct iovec *iov, int iovcnt, bool quiet)
{
	ssize_t res = 0;
	if (mFd < 0)
		return;

	/* Compute total size we want to write */
	size_t len = 0;
	for (int i = 0; i < iovcnt; i++)
		len += iov[i].iov_len;

	res = ::writev(mFd, iov, iovcnt);
	if (res < 0) {
		/* 'quiet' flag ignored, we are closing the file. */
		ULOG_ERRNO("backend write", errno);
		close();
	} else if ((size_t)res < len) {
		/* Handle this as an error meaning file is too big or no space
		 * left on device
		 * 'quiet' flag ignored, we are closing the file. */
		ULOGW("backend partial write: %zd(%zu)", res, len);
		close();
	} else if (!quiet) {
		ULOGD("wrote %zd bytes", res);
	}
}

void BackendFile::pwrite(const void *buf, size_t len, off_t offset)
{
	ssize_t res = 0;
	const uint8_t *data = (const uint8_t *)buf;

	if (mFd < 0)
		return;

	while (len > 0) {
		res = ::pwrite(mFd, data, len, offset);
		if (res <= 0) {
			/* 'quiet' flag ignored, we are closing the file. */
			ULOG_ERRNO("backend pwrite", errno);
			close();
			break;
		}
		len -= res;
		data += res;
		offset += res;
	}
}

LogBackend *backend_file_create(const std::string &outputDir)
{
	return new BackendFile(outputDir);
}

LogFile::LogFile(struct loghdr *hdr, std::string path, uint32_t idx, size_t size)
{
	mHdr = hdr;
	mPath = path;
	mIdx = idx;
	mSize = size;
}

LogFile::LogFile()
{
	mHdr = nullptr;
	mPath = "";
	mIdx = 0;
	mSize = 0;
}

bool LogFile::operator < (const LogFile &log) const
{
	bool ret = false;
	std::string takeoff_a, takeoff_b;

	if (mHdr == nullptr || !loghdr_has_key(mHdr, "takeoff"))
		takeoff_a = "0";
	else
		takeoff_a = loghdr_get_value(mHdr, "takeoff");

	if (log.mHdr == nullptr || !loghdr_has_key(log.mHdr, "takeoff"))
		takeoff_b = "0";
	else
		takeoff_b = loghdr_get_value(log.mHdr, "takeoff");

	if (takeoff_a == takeoff_b)
		ret = (mIdx < log.mIdx);
	else
		ret = (takeoff_b == "1");

	return ret;
}

} /* namespace loggerd */
