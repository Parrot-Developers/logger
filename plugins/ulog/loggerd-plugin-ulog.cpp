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

#include <string.h>
#include <unistd.h>
#include <fcntl.h>

#define ULOG_TAG loggerd_ulog
#include <ulog.h>
ULOG_DECLARE_TAG(loggerd_ulog);
#include <ulogger.h>
#include <ulogprint.h>

#include "loggerd-plugin-ulog.hpp"

#define ULOG_DEV_PREFIX		"ulog_"
#define ULOG_DEV_PREFIX_LEN	5

namespace loggerd {

const std::string UlogSource::SOURCE_TYPE = "ulog";

UlogSource::UlogSource(int fd,
		const std::string &name,
		LogDirectWriter *directWriter,
		loggerd::LogManager *manager)
{
	mFd = fd;
	mPid = (int32_t)getpid();
	mName = name;
	mDirectWriter = directWriter;
	mManager = manager;
}

UlogSource::~UlogSource()
{
	if (mFd >= 0)
		close(mFd);
}

size_t UlogSource::readData(loggerd::LogData &data)
{
	int ret;
	ssize_t readlen;
	struct ulog_entry entry;

	/* NOTE: dropped entries are already managed in kernel driver */

	if (mFd < 0)
		return 0;

	while (data.remaining() > 0) {
		/*
		* read exactly one ulogger entry, until the driver implements
		* batch reads
		*/
		readlen = read(mFd, data.current(), data.remaining());
		if (readlen < 0) {
			if ((errno == EINTR)  ||
			    (errno == EAGAIN) ||
			    (errno == EINVAL))
				/* retry later or not enough space in buffer */
				break;

			/* fatal error */
			ULOG_ERRNO("%s: read", errno, mName.c_str());
			close(mFd);
			mFd = -1;
			break;

		} else if (readlen == 0) {
			/* nothing more to read */
			break;
		}

		/* parse entry to extract some info (pid) */
		ret = ulog_parse_raw(data.current(), readlen, &entry);
		if (ret < 0) {
			ULOG_ERRNO("%s: ulog_parse_raw", errno, mName.c_str());
			close(mFd);
			mFd = -1;
			break;
		}

		/* Optional filtering */
		if (!filterEntry(&entry))
			continue;

		if (filterTime(&entry)) {
			mManager->updateRefTime(entry.message, entry.tv_sec,
						entry.tv_nsec);
		} else if (filterGcsName(&entry)) {
			mManager->updateGcsName(entry.message);
		} else if (filterGcsType(&entry)) {
			mManager->updateGcsType(entry.message);
		}

		/*
		 * loggerd messages are directly written to backend
		 * file to avoid creating Hofstadter strange loops ;-)
		 */
		if (entry.pid == mPid)
			mDirectWriter->write(data.current(), readlen);
		else
			data.skip(readlen);
	}

	return data.used();
}

uint32_t UlogSource::getPeriodMs()
{
	return 1000;
}

bool UlogSource::filterGcsName(const struct ulog_entry *entry)
{
	const char pattern[] = "EVTS:CONTROLLER;name=";
	return (strncmp(entry->message, pattern, sizeof(pattern) - 1) == 0);
}

bool UlogSource::filterGcsType(const struct ulog_entry *entry)
{
	const char pattern[] = "EVT:CONTROLLER;event='connected'";
	return (strncmp(entry->message, pattern, sizeof(pattern) - 1) == 0);
}

bool UlogSource::filterTime(const struct ulog_entry *entry)
{
	return (strncmp(entry->message, "EVT:TIME", 8) == 0);
}

static void add_ulog_device(loggerd::LogManager *manager,
		loggerd::LogDirectWriter *directWriter,
		const std::string &name,
		const UlogFactory::CreateFunc &createFunc)
{
	int fd;
	std::string path;

	path = "/dev/" ULOG_DEV_PREFIX + name;

	fd = open(path.c_str(), O_RDONLY | O_NONBLOCK | O_CLOEXEC);
	if (fd >= 0) {
		ULOGI("createFunc %s", name.c_str());
		UlogSource *logSource = createFunc(fd, name, directWriter,
						   manager);
		if (logSource == nullptr) {
			close(fd);
		} else {
			ULOGI("addLogSource %s", name.c_str());
			manager->addLogSource(logSource,
					UlogSource::SOURCE_TYPE,
					name,
					UlogSource::VERSION);
		}
	} else {
		ULOG_ERRNO("open(%s)", errno, path.c_str());
	}
}

void UlogFactory::createSources(LogManager *manager,
		const CreateFunc &createFunc)
{
	FILE *fp;
	char *p, buf[32];
	const char *name;
	LogDirectWriter *writer;

	/* we may need to directly write to output buffer */
	writer = manager->getDirectWriter(UlogSource::SOURCE_TYPE,
			UlogSource::VERSION);

	/* retrieve list of dynamically created ulog devices */
	fp = fopen("/sys/devices/virtual/misc/ulog_main/logs", "r");
	if (fp) {
		while (fgets(buf, sizeof(buf), fp)) {
			p = strchr(buf, ' ');

			if (p && (strlen(buf) > 5)) {
				*p = '\0';
				/* skip 'ulog_' prefix in log name */
				name = buf + ULOG_DEV_PREFIX_LEN;
				add_ulog_device(manager, writer,
						name, createFunc);
			}
		}
		fclose(fp);
	} else {
		/* backward compatibility if attribute file is not present */
		add_ulog_device(manager, writer,
				&ULOGGER_LOG_MAIN[ULOG_DEV_PREFIX_LEN],
				createFunc);
	}
}

} /* namespace loggerd */