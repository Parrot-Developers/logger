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

#ifndef _LOGGERD_HEADERS_HPP_
#define _LOGGERD_HEADERS_HPP_

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <errno.h>
#include <dirent.h>
#include <dlfcn.h>
#include <signal.h>
#include <unistd.h>
#include <inttypes.h>
#include <getopt.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/statvfs.h>

#include <string>
#include <map>
#include <vector>

#include <lz4frame.h>
#include <openssl/evp.h>
#include <openssl/pem.h>
#include <openssl/sha.h>

#define ULOG_TAG loggerd
#include <ulog.h>

#include <libpomp.h>
#include <futils/futils.h>

#ifdef BUILD_LIBPUTILS
#include <putils/properties.h>
#endif /* BUILD_LIBPUTILS */

#include "libloghdr.h"
#include "liblogger.hpp"
#include "loggerd-plugin.hpp"
#include "loggerd-backend.hpp"
#include "loggerd-format.h"

#define LOGGERD_DEFAULT_PERIOD_MS	200

#define LOGGERD_BLOCKSIZE_ENTRY		(512 * 1024)		/* 512 kB */
#define LOGGERD_BLOCKSIZE_COMPRESSION	(2 * 1024 * 1024)	/* 2 MB */

#define DEFAULT_MSG "EVT:TIME;date='1970-01-01';time='T000000+0200'"
#define TIME_ZERO "00000000000000000000"
#define GCS_DEFAULT_SIZE 128

#include "buffer.hpp"
#include "plugin.hpp"
#include "source.hpp"
#include "frontend.hpp"

namespace loggerd {

LogBackend *backend_file_create(const std::string &outputDir);

/**
 * Get current monotonic time in ms.
 */
static inline uint64_t getTimeMs(void)
{
	uint64_t time_us = 0;
	struct timespec ts = {0, 0};
	time_get_monotonic(&ts);
	time_timespec_to_us(&ts, &time_us);
	return time_us / 1000;
}

/**
 * Get system date formated as ISO short format
 */
static inline void getDate(char *date, size_t datesize)
{
	uint64_t epoch_sec = 0;
	int32_t utc_offset_sec = 0;
	struct tm tm;
	memset(&tm, 0, sizeof(tm));

	time_local_get(&epoch_sec, &utc_offset_sec);
	time_local_to_tm(epoch_sec, utc_offset_sec, &tm);
	strftime(date, datesize, "%Y%m%dT%H%M%S%z", &tm);
}


} /* namespace loggerd */

#endif /* _LOGGERD_HEADERS_HPP_ */
