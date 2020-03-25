/**
 * Copyright (c) 2019-2020 Parrot Drones SAS
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *   * Redistributions of source code must retain the above copyright
 *     notice, this list of conditions and the following disclaimer.
 *   * Redistributions in binary form must reproduce the above copyright
 *     notice, this list of conditions and the following disclaimer in the
 *     documentation and/or other materials provided with the distribution.
 *   * Neither the name of the Parrot Drones SAS Company nor the
 *     names of its contributors may be used to endorse or promote products
 *     derived from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE PARROT DRONES SAS COMPANY BE LIABLE FOR
 * ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
 * CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
 * OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <map>
#include <string>
#include <iostream>

#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/types.h>

#define ULOG_TAG libloghdr
#include <ulog.h>
ULOG_DECLARE_TAG(ULOG_TAG);

#include "libloghdr.h"
#include "loggerd-format.h"

#define MAX_HEADER_SIZE (64 * 1024)

struct loghdr {
	int fd;
	std::string path;
	std::map<std::string, std::string> properties;
};

static int loghdr_readHelper(int fd, void *buf, size_t count)
{
	int res = 0;

	res = read(fd, buf ,count);
	if (res != (int) count) {
		if (res < 0)
			res = -errno;
		else
			res = -EIO;
		ULOG_ERRNO("Unable to read %lu bytes", -res, count);
	}

	return res;
}

static int loghdr_read_fileheader(struct loghdr *hdr)
{
	int res = 0;
	uint32_t magic;
	uint32_t version;

	res = loghdr_readHelper(hdr->fd, &magic, sizeof(magic));
	if (res < 0)
		goto out;
	if (magic != LOGGERD_FILE_MAGIC) {
		res = -EINVAL;
		ULOGE("Bad magic: 0x%08x(0x%08x)", magic,
		       LOGGERD_FILE_MAGIC);
		goto out;
	}

	res = loghdr_readHelper(hdr->fd, &version, sizeof(version));
	if (res < 0)
		goto out;
	if (version > LOGGERD_FILE_VERSION) {
		res = -EINVAL;
		ULOGE("Bad version: %u(%u)", version,
		       LOGGERD_FILE_VERSION);
		goto out;
	}

	ULOGD("File magic: 0x%08x, File version: %u", magic, version);

out:
	return res;
}

static int loghdr_read_header(struct loghdr *hdr)
{
	int res = 0;
	uint32_t len;
	uint16_t slen;
	uint8_t *data = NULL;
	bool isKey = true;
	uint32_t remaining;
	std::string *ptrTmp;
	std::string key("");
	std::string value("");

	/* Skip source description */
	if (lseek(hdr->fd, sizeof(uint32_t), SEEK_CUR) < 0) {
		res = -errno;
		ULOG_ERRNO("Unable to seek to offset", -res);
		goto out;
	}

	res = loghdr_readHelper(hdr->fd, &len, sizeof(len));
	if (res < 0)
		goto out;

	if (lseek(hdr->fd, len + sizeof(uint32_t), SEEK_CUR) < 0) {
		res = -errno;
		ULOG_ERRNO("Unable to seek to offset", -res);
		goto out;
	}

	/* Then read header */
	res = loghdr_readHelper(hdr->fd, &len, sizeof(len));
	if (res < 0)
		goto out;
	if (len > MAX_HEADER_SIZE) {
		res = -EINVAL;
		ULOGE("File header seems too big: %u bytes, limit is %u bytes.",
		      len, MAX_HEADER_SIZE);
		goto out;
	}

	data = (uint8_t *) malloc((uint64_t)len + 1);
	if (data == NULL) {
		res = -errno;
		ULOG_ERRNO("Unable to allocate space for header data", -res);
		goto out;
	}

	res = loghdr_readHelper(hdr->fd, data, len);
	if (res < 0)
		goto out;
	data[res] = 0;

	/* And parse it to get key-value couple */
	remaining = len;

	while (remaining > sizeof(slen)) {
		ptrTmp = isKey ? &key : &value;

		memcpy(&slen, data + len - remaining, sizeof(slen));
		remaining -= sizeof(slen);

		if (remaining < slen) {
			res = -EINVAL;
			ULOG_ERRNO("Unable to read %u bytes", -res, len);
			goto out;
		} else {
			*ptrTmp = (char *) data + len - remaining;
			remaining -= slen;
		}

		if (!isKey)
			hdr->properties[key] = value;

		isKey = !isKey;
	}

out:
	free(data);
	return res;
}

static int loghdr_extract(struct loghdr *hdr)
{
	int res = 0;

	res = loghdr_read_fileheader(hdr);
	if (res < 0)
		return res;

	res = loghdr_read_header(hdr);
	if (res < 0)
		return res;

	return 0;
}

bool loghdr_has_key(struct loghdr *hdr, const char *key)
{
	return hdr->properties.find(std::string(key))
	       != hdr->properties.end();
}

const char *loghdr_get_value(struct loghdr *hdr, const char *key)
{
	std::string sKey(key);
	auto it = hdr->properties.find(sKey);

	if (it != hdr->properties.end()) {
		ULOGD("[%s]: [%s]", key, it->second.c_str());
		return it->second.c_str();
	}

	return NULL;
}

int loghdr_tostring(struct loghdr *hdr, char *buf, size_t len)
{
	int p_len;
	std::string tmp("");

	if (!hdr || !buf)
		return -EINVAL;

	for (auto it = hdr->properties.cbegin();
	     it != hdr->properties.cend();++it)
		tmp += "[" + it->first + "]: [" + it->second + "]\n";

	p_len = snprintf(buf, len, "%s", tmp.c_str());

	if (p_len >= (int)len) {
		ULOGE("Not enough space in buffer");
		return -EINVAL;
	}

	return 0;
}

struct loghdr *loghdr_new(const char *path)
{
	int res = 0;

	struct loghdr *hdr = new loghdr();
	hdr->fd = open(path, O_RDONLY);
	hdr->path = std::string(path);

	if (hdr->fd < 0) {
		if (errno != ENOENT)
			ULOG_ERRNO("open(%s)", errno, path);
		goto error;
	}
	ULOGD("'%s' opened", path);

	res = loghdr_extract(hdr);

	close(hdr->fd);
	ULOGD("'%s' closed", hdr->path.c_str());
	hdr->fd = -1;

	if (res < 0)
		goto error;

	return hdr;

error:
	loghdr_destroy(hdr);
	return NULL;
}

void loghdr_destroy(struct loghdr *hdr)
{
	if (!hdr)
		return;

	delete hdr;
}
