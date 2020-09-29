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

#include <futils/fs.h>
#include <string>
#include "headers.hpp"

namespace loggerd {

const char *LogFrontend::closeReasonStr(CloseReason reason)
{
	switch (reason) {
	case CloseReason::UNKNOWN: return "UNKNOWN";
	case CloseReason::NOT_CLOSED: return "NOT_CLOSED";
	case CloseReason::EXITING: return "EXITING";
	case CloseReason::DISABLED: return "DISABLED";
	case CloseReason::NO_SPACE_LEFT: return "NO_SPACE_LEFT";
	case CloseReason::FILE_TOO_BIG: return "FILE_TOO_BIG";
	case CloseReason::QUOTA_REACHED: return "QUOTA_REACHED";
	case CloseReason::ROTATE: return "ROTATE";
	default: return "UNKNOWN";
	}
}

LogFrontend::LogFrontend(const loggerd::Loggerd::Options &opt,
		LogBackend *backend,
		BaseSource *headerSource,
		BaseSource *footerSource,
		const StartSession &startSession) :
			mStartSession(startSession), mOpt(opt)
{
	mAbsolute = TIME_ZERO;
	mMonotonic = DEFAULT_MSG;
	mBackend = backend;
	mHeaderSource = headerSource;
	mFooterSource = footerSource;
	mRemoveSize = 0;
	mFlightIdOff = 0;
	mTakeoffOff = 0;
	mDateOff = 0;
	mGcsNameOff = 0;
	mGcsTypeOff = 0;
	mMd5Off = 0;
	mMonotonicOff = 0;
	mAbsoluteOff = 0;
	mFlightIdSize = 0;
	mTakeoffSize = 0;
	mAbsoluteSize = 0;
	mGcsNameSize = 0;
	mGcsTypeSize = 0;
	mMonotonicSize = 0;
	mDateSize = 0;
	mMd5Size = 0;
	mTakeoff = false;
	mMd5Enabled = false;
	mClosing = false;
	mCheckSpace = false;
	mCloseReason = CloseReason::NOT_CLOSED;
	mIndex = 0;
	mUsedSpace = 0;
	memset(&mCtx, 0, sizeof(mCtx));
	memset(&mGcsName, 0, sizeof(mGcsName));
	memset(&mGcsType, 0, sizeof(mGcsType));
}

LogFrontend::~LogFrontend()
{
}

void LogFrontend::enableMd5()
{
	mMd5Enabled = true;
}

void LogFrontend::updateRemoveSize()
{
	/* Reset calculated size to remove and calculated space used
	 * by logger. */
	mRemoveSize = 0;
	mUsedSpace = futils_fs_dir_size(mOpt.outputDir.c_str(), false);

	updateRemoveSizeForFreeSpace(false);
	updateRemoveSizeForUsedSpace(false);
}

int LogFrontend::open()
{
	int res = 0;

	if (isOpened())
		return -EEXIST;

	/* Start by calculating the removeSize to cleanup last loggerd session.
	 */
	if (mCloseReason == CloseReason::NOT_CLOSED)
		updateRemoveSize();

	/* Forward lifetime dependant index to backend */
	if (mOpt.logIdxManager != nullptr)
		mBackend->setMinLogId(mOpt.logIdxManager->getIndex());

	/* Avoid destroying an existing log by rotating files */
	mBackend->rotate(mRemoveSize, mOpt.maxLogCount);

	/* minLogId needs to be updated by the backend since preexisting
	 * fdr-lite can be present in the directory.
	 * Thus we retrieve its new value to modify lifetime dependant index.
	 */
	if (mOpt.logIdxManager != nullptr)
		mOpt.logIdxManager->setIndex(mBackend->getMinLogId());

	/* Try to open backend, then write header */
	res = mBackend->open();
	if (res < 0)
		return res;

	/* Initialize md5 */
	if (mMd5Enabled)
		MD5_Init(&mCtx);

	mUsedSpace = futils_fs_dir_size(mOpt.outputDir.c_str(), false);
	mCloseReason = CloseReason::NOT_CLOSED;
	writeHeader();
	mStartSession();

	return 0;
}

void LogFrontend::close(CloseReason reason)
{
	unsigned char md5[MD5_DIGEST_LENGTH];

	if (!isOpened() || mClosing)
		return;

	/* Write footer with given reason, then sync and close */
	mClosing = true;
	mCloseReason = reason;
	writeFooter(reason);
	ULOGI("closing log, reason: %s (%zu bytes written)",
			closeReasonStr(reason), mBackend->size());

	/* Finalize md5 computation */
	if (mMd5Enabled)
	{
		MD5_Final(md5, &mCtx);
		for (int i = 0; i < MD5_DIGEST_LENGTH; i++)
			snprintf(mMd5 + i*2, 3,"%02x", md5[i]);
		updateMd5();
	}

	mBackend->sync();

	/* Update usedSpace before closing the file so that usedSpace is
	 * still accurate for the size checks without having to measure the
	 * directory size from scratch. */
	mUsedSpace += mBackend->size();

	mBackend->close();
	mClosing = false;
}

bool LogFrontend::isOpened()
{
	return mBackend->isOpened();
}

bool LogFrontend::writehdrField(off_t *off, size_t *s, const char *key,
	const char *value, LogData &logData, size_t prev, int len)
{
	bool ok = true;

	ok = ok && logData.pushString(key);

	if (s && off)
		*off = logData.used();
	ok = ok && logData.pushString(value, len);
	if (s && off) {
		*s = logData.used() - *off;
		*off += mBackend->size() + prev;
	}

	return ok;
}

void LogFrontend::updateField(off_t *off, size_t *s, const char *data,
			   const char *desc, int size)
{
	size_t len;

	if (*off == 0 || *s == 0)
		return;

	if (size != -1)
		len = sizeof(uint16_t) + size;
	else
		len = sizeof(uint16_t) + strlen(data);

	uint8_t *buf = (uint8_t *) malloc(len + 1);
	if (buf == NULL)
		return;

	LogData logData(buf, len + 1);

	if (!logData.pushString(data, size)) {
		ULOGW("Failed to rewrite %s", desc);
	} else if (logData.used() != *s) {
		ULOGW("Failed to rewrite %s, size mismatch: %zu(%zu)",
		      desc, logData.used(), *s);
	} else {
		/* Clear offset/size after write so we don't update it again */
		ULOGI("Update %s @%jd:%zu -> %s", desc, (intmax_t) *off, *s,
			data);
		mBackend->pwrite(buf, logData.used(), *off);
		*off = 0;
		*s = 0;
	}

	free(buf);
}

void LogFrontend::updateExtraProperty(const std::string &key, const std::string &value)
{
	for (auto &property : mOpt.extraProps) {
		if (property.key != key)
			continue;

		property.value = value;
		updateField(&property.offset, &property.size,
			property.value.c_str(), property.key.c_str());
	}
}

void LogFrontend::updateDate()
{
	char date[64] = "";

	getDate(date, sizeof(date));
	updateField(&mDateOff, &mDateSize, date, "date");
}

void LogFrontend::updateFlightId(const char *flight_id)
{
	updateField(&mFlightIdOff, &mFlightIdSize, flight_id, "flight_id");
}

void LogFrontend::updateGcsName(const char *message)
{
	snprintf(mGcsName, sizeof(mGcsName), "%s", message);
	updateGcsField(&mGcsNameOff, &mGcsNameSize, "gcs_name", message);
}

void LogFrontend::updateGcsType(const char *message)
{
	snprintf(mGcsType, sizeof(mGcsType), "%s", message);
	updateGcsField(&mGcsTypeOff, &mGcsTypeSize, "gcs_type", message);
}

void LogFrontend::updateGcsField(off_t *off, size_t *s, const char *desc,
							const char *message)
{
	size_t size = strlen(message);
	char copy[GCS_DEFAULT_SIZE + 1];

	memset(copy, 0, sizeof(copy));
	size = size > GCS_DEFAULT_SIZE ? GCS_DEFAULT_SIZE : size;
	snprintf(copy, size + 1, "%s", message);

	updateField(off, s, copy, desc, sizeof(copy) - 1);
}

void LogFrontend::updateRefTime(const char *message, time_t tv_sec,
				long tv_nsec)
{
	uint64_t us;
	char absolute[21];
	struct timespec ts;

	if (mAbsoluteOff == 0 || mMonotonicOff == 0)
		return;

	mMonotonic = message;

	ts.tv_sec = tv_sec;
	ts.tv_nsec = tv_nsec;
	time_timespec_to_us(&ts, &us);
	snprintf(absolute, sizeof(absolute), "%020" PRIu64, us);
	mAbsolute = absolute;

	updateField(&mMonotonicOff, &mMonotonicSize, mMonotonic.c_str(),
		    "monotonic");
	updateField(&mAbsoluteOff, &mAbsoluteSize, mAbsolute.c_str(),
		    "absolute");
}

void LogFrontend::updateTakeoff(bool takeoff)
{
	if (takeoff == mTakeoff || mTakeoffOff == 0)
		return;

	mTakeoff = takeoff;

	updateField(&mTakeoffOff, &mTakeoffSize, takeoff ? "1" : "0",
								"takeoff");
}

void LogFrontend::updateMd5()
{
	updateField(&mMd5Off, &mMd5Size, mMd5, "md5");
}

void LogFrontend::sync()
{
	mBackend->sync();
}

void LogFrontend::write(const void *buf, size_t len, bool quiet)
{
	struct iovec iov[1];
	iov[0].iov_base = const_cast<void *>(buf);
	iov[0].iov_len = len;
	writev(iov, 1, quiet);
}

void LogFrontend::writev(const struct iovec *iov, int iovcnt, bool quiet, bool isHeader)
{
	CloseReason reason = CloseReason::NOT_CLOSED;

	if (!isOpened())
		return;

	/* Compute md5 if enabled */
	if (mMd5Enabled && !isHeader) {
		for (int i = 0; i < iovcnt; i++)
			MD5_Update(&mCtx, iov[i].iov_base, iov[i].iov_len);
	}

	/* Always write this buffer, check limits after */
	mBackend->writev(iov, iovcnt, quiet);

	if (!mCheckSpace)
		return;

	mRemoveSize = 0;

	/* If log size is too big, close it right away so that check space
	 * includes space required for the new log. */
	if (checkLogSize(quiet))
		close(CloseReason::FILE_TOO_BIG);

	/* If too much space is used or if there is not enough free space,
	 * the log file must be closed and the close reason is set. */
	if (updateRemoveSizeForFreeSpace(quiet))
		reason = CloseReason::NO_SPACE_LEFT;
	if (updateRemoveSizeForUsedSpace(quiet))
		reason = CloseReason::QUOTA_REACHED;

	/* If there is a close reason, close the file if not already */
	if ((reason != CloseReason::NOT_CLOSED) && isOpened())
		close(reason);

	/* open new file if current one has been closed */
	if (!isOpened())
		open();
}

void LogFrontend::writeHeader()
{
	int res;
	bool ok = true;
	struct iovec iov[4];
	int iovcnt = 0;

	/* Add a buffer in iov
	 * The buffer shall not be reused after put in the iov */
	auto addIov = [&](const void *buf, size_t len) {
		if (iovcnt >= (int)FUTILS_SIZEOF_ARRAY(iov)) {
			ok = false;
		} else {
			iov[iovcnt].iov_base = const_cast<void *>(buf);
			iov[iovcnt].iov_len = len;
			iovcnt++;
		}
	};

	/* File magic and version */
	struct loggerd_file_header fileHdr;
	memset(&fileHdr, 0, sizeof(fileHdr));
	fileHdr.magic = LOGGERD_FILE_MAGIC;
	fileHdr.version = LOGGERD_FILE_VERSION;
	addIov(&fileHdr, sizeof(fileHdr));

	/* Header source description */
	uint8_t descBuf[128];
	ssize_t descLen = mHeaderSource->fillDescription(
			descBuf, sizeof(descBuf));
	if (descLen < 0) {
		descLen = 0;
		ok = false;
	} else {
		mHeaderSource->mPendingDescription = false;
		addIov(descBuf, descLen);
	}

	struct loggerd_entry_header hdr;
	/* coverity[stack_use_local_overflow] */
	uint8_t hdrBuf[2048];
	size_t prev = sizeof(fileHdr) + descLen + sizeof(hdr);
	LogData logData(hdrBuf + sizeof(hdr), sizeof(hdrBuf) - sizeof(hdr));

	char index[10+1];
	snprintf(index, sizeof(index), "%d", mIndex);
	ok = ok && writehdrField(NULL, NULL, "index", index, logData, prev);
	mIndex++;

#ifdef BUILD_LIBPUTILS
	/* system properties included in file header */
	static const char * const sysprops[] = {
		"ro.hardware",
		"ro.product.model.id",
		"ro.product.board_id",
		"ro.product.usb.pid",
		"ro.build.date",
		"ro.parrot.build.group",
		"ro.parrot.build.product",
		"ro.parrot.build.project",
		"ro.parrot.build.region",
		"ro.parrot.build.uid",
		"ro.parrot.build.variant",
		"ro.parrot.build.version",
		"ro.revision",
		"ro.mech.revision",
		"ro.factory.hcam_serial",
		"ro.factory.serial",
		"ro.factory.product.pro",
		"ro.boot.uuid",
		"ro.smartbattery.gfw_version",
		"ro.smartbattery.g_date",
		"ro.smartbattery.usb_model",
		"ro.smartbattery.usb_version",
		"ro.smartbattery.version",
		"ro.smartbattery.serial",
		"ro.smartbattery.hw_version",
		"ro.smartbattery.design_cap",
		"ro.smartbattery.device_info",
		"ro.smartbattery.device_name",
		"ro.esc.fw_version",
		"ro.esc.hw_version",
		"ddr_info.sync",
		"ro.smartbattery.cycle_count",
		"ro.smartbattery.soh",
	};

	for (size_t i = 0; i < SIZEOF_ARRAY(sysprops); i++) {
		char value[SYS_PROP_VALUE_MAX] = "";
		sys_prop_get(sysprops[i], value, "");
		ok = ok && writehdrField(NULL, NULL, sysprops[i], value, logData,
			 		 prev);
	}

	char flight_id[SYS_PROP_VALUE_MAX] = "";
	char default_id[] = "FFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFF";

	sys_prop_get("control.flight.uuid", flight_id, default_id);
	if (strcmp(flight_id, "") == 0)
		snprintf(flight_id, sizeof(flight_id), "%s", default_id);
	ok = ok && writehdrField(&mFlightIdOff, &mFlightIdSize,
			"control.flight.uuid", flight_id, logData, prev);

#endif /* BUILD_LIBPUTILS */

	for (auto &property : mOpt.extraProps) {
		size_t *size = property.readOnly ? NULL : &property.size;
		off_t *offset = property.readOnly ? NULL : &property.offset;

		ok = ok && writehdrField(offset, size, property.key.c_str(),
			property.value.c_str(), logData, prev);
	}

	if (mOpt.logIdxManager != nullptr) {
		char value[128] = "";
		res = mOpt.logIdxManager->getIndexStr(value, sizeof(value));
		if (res >= 0) {
			ok = ok && writehdrField(NULL, NULL, "lifetime.index",
						 value, logData, prev);
		}
	}

	/* Write current date/time and remember where we store it so we can
	 * rewrite it when the system date is changed later */
	char date[64] = "";
	getDate(date, sizeof(date));
	ok = ok && writehdrField(&mDateOff, &mDateSize, "date", date, logData,
				 prev);

	ok = ok && writehdrField(&mGcsNameOff, &mGcsNameSize, "gcs.name",
			mGcsName, logData, prev, sizeof(mGcsName) - 1);
	ok = ok && writehdrField(&mGcsTypeOff, &mGcsTypeSize, "gcs.type",
			mGcsType, logData, prev, sizeof(mGcsType) - 1);

	/* Write default md5 and remember where we store it so we can
	 * rewrite it when computation is ended*/
	char md5[(MD5_DIGEST_LENGTH * 2) + 1] = "";
	memset(md5, 'f', sizeof(md5));
	md5[MD5_DIGEST_LENGTH * 2] = '\0';
	ok = ok && writehdrField(&mMd5Off, &mMd5Size, "md5", md5, logData, prev);

	/* Write reference time monotonic and remember where we store it so we
	 * can rewrite it when computation is ended*/
	ok = ok && writehdrField(&mMonotonicOff, &mMonotonicSize,
		 	"reftime.monotonic", mMonotonic.c_str(), logData, prev);
	if (mMonotonic != DEFAULT_MSG)
		mMonotonicOff = 0;

	ok = ok && writehdrField(&mAbsoluteOff, &mAbsoluteSize,
			"reftime.absolute", mAbsolute.c_str(), logData, prev);
	if (mAbsolute != TIME_ZERO)
		mAbsoluteOff = 0;

	/* Indicate if aircraft had takeoff during this flight */
	ok = ok && writehdrField(&mTakeoffOff, &mTakeoffSize, "takeoff",
			mTakeoff ? "1" : "0", logData, prev);

	/* Insert entry header */
	hdr.id = mHeaderSource->mId;
	hdr.len = ok ? logData.used() : 0;
	memcpy(hdrBuf, &hdr, sizeof(hdr));

	/* write uncompressed entry for easy parsing */
	addIov(hdrBuf, sizeof(hdr) + hdr.len);

	/* desactivate space check */
	mCheckSpace = false;

	/* Write our header */
	writev(iov, iovcnt, false, true);

	/* reactivate space check */
	mCheckSpace = true;
}

void LogFrontend::writeFooter(CloseReason reason)
{
	bool ok = true;
	struct iovec iov[4];
	int iovcnt = 0;

	/* Add a buffer in iov
	 * The buffer shall not be reused after put in the iov */
	auto addIov = [&](const void *buf, size_t len) {
		if (iovcnt >= (int)FUTILS_SIZEOF_ARRAY(iov)) {
			ok = false;
		} else {
			iov[iovcnt].iov_base = const_cast<void *>(buf);
			iov[iovcnt].iov_len = len;
			iovcnt++;
		}
	};

	/* Footer source description */
	uint8_t descBuf[128];
	ssize_t descLen = mFooterSource->fillDescription(
			descBuf, sizeof(descBuf));
	if (descLen < 0)
		ok = false;
	else
		addIov(descBuf, descLen);

	struct loggerd_entry_header hdr;
	uint8_t propBuf[1024];
	LogData logData(propBuf + sizeof(hdr), sizeof(propBuf) - sizeof(hdr));

	ok = ok && logData.pushString("reason");
	ok = ok && logData.pushString(closeReasonStr(reason));

	/* Insert entry header */
	hdr.id = mFooterSource->mId;
	hdr.len = ok ? logData.used() : 0;
	memcpy(propBuf, &hdr, sizeof(hdr));

	/* write uncompressed entry for easy parsing */
	addIov(propBuf, sizeof(hdr) + hdr.len);

	/* desactivate space check */
	mCheckSpace = false;

	/* Write our footer */
	writev(iov, iovcnt, false);

	/* reactivate space check */
	mCheckSpace = true;
}

bool LogFrontend::updateRemoveSizeForFreeSpace(bool quiet)
{
	struct statvfs vstat;
	size_t removeSize;
	size_t freeSpace;

	/* If file is currently being written (is open) we remove space to stay
	 * in the limits of the available space.
	 * If the file is not yet opened, we remove an additionnal reserve of
	 * memory for the next file. */
	size_t reservedSpace = isOpened() ? 0 : mOpt.minLogSize;

	/* 0 means no limit so always OK */
	if ((mOpt.minFreeSpace == 0) && (reservedSpace == 0))
		return false;

	/* Check if we reached min free space limit */
	if (::statvfs(mOpt.outputDir.c_str(), &vstat) < 0) {
		if (!quiet)
			ULOG_ERRNO("statvfs(%s)", errno, mOpt.outputDir.c_str());
		return false;
	}

	freeSpace = vstat.f_bavail * vstat.f_bsize;

	if (freeSpace < mOpt.minFreeSpace + reservedSpace) {
		/* Test use "reservedSpace" to check if we need to close the
		 * file and free some space, but if we do need to free some
		 * space, then we have to keep enough memory for next file, no
		 * matter if current file is opened or not. So use mMinLogSize
		 * in removeSize */
		removeSize = mOpt.minFreeSpace - freeSpace + mOpt.minLogSize;

		/* Update mRemoveSize if the size to remove is
		 * greater than what was already scheduled. */
		if (mRemoveSize < removeSize) {
			mRemoveSize = removeSize;
			return true;
		}
	}

	return false;
}

bool LogFrontend::updateRemoveSizeForUsedSpace(bool quiet)
{
	size_t removeSize;
	size_t usedSpace;

	/* 0 means no limit so always OK */
	if (mOpt.maxUsedSpace == 0)
		return false;

	/* Check if we reached max usable space limit */
	usedSpace = mUsedSpace + mBackend->size();

	/* If file is currently being written (is open) we remove space to stay
	 * in the limits of the available space.
	 * If the file is not yet opened, we remove an additionnal reserve of
	 * memory for the next file. */
	size_t reservedSpace = isOpened() ? 0 : mOpt.minLogSize;

	if (usedSpace + reservedSpace > mOpt.maxUsedSpace) {
		/* Test use "reservedSpace" to check if we need to close the
		 * file and free some space, but if we do need to free some
		 * space, then we have to keep enough memory for next file, no
		 * matter if current file is opened or not. So use mMinLogSize
		 * in removeSize */
		removeSize = usedSpace - mOpt.maxUsedSpace + mOpt.minLogSize;

		/* Update mRemoveSize if the size to remove is
		 * greater than what was already scheduled. */
		if (mRemoveSize < removeSize) {
			mRemoveSize = removeSize;
			return true;
		}
	}

	return false;
}

bool LogFrontend::checkLogSize(bool quiet)
{
	size_t logSize;

	/* 0 means no limit so always OK */
	if (mOpt.maxLogSize == 0)
		return false;

	/* Make sure the total size will not exceed the limit */
	logSize = mBackend->size();

	if (logSize > mOpt.maxLogSize)
		return true;

	return false;
}
} /* namespace loggerd */
