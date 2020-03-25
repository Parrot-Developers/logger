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

#ifndef _LIBLOGHDR_H_
#define _LIBLOGHDR_H_

#ifdef __cplusplus
extern "C" {
#endif

#include <stdlib.h>
#include <stdbool.h>

/**
 * log header, an opaque structure.
 */
struct loghdr;

/**
 * Extract log header informations
 *
 * @param path: path to a log file
 * @return      log header if successful, NULL if an error occurred
 */
struct loghdr *loghdr_new(const char *path);

/**
 * Destroy log header
 *
 * @param hdr: log header
 */
void loghdr_destroy(struct loghdr *hdr);

/**
 * Give value associated to key
 *
 * @param hdr:     log header to analyze
 * @param key:     key to search in log header
 * @return         value associated to key
 */
const char *loghdr_get_value(struct loghdr *hdr, const char *key);

/**
 * Convert log header to a string
 *
 * @param hdr:     log header to convert
 * @param buf:     buffer for the resulting string
 * @param len:     length of the buffer
 * @return         0 if successful, -errno if an error occurred
 */
int loghdr_tostring(struct loghdr *hdr, char *buf, size_t len);

/**
 * Analyze presence of key in log header
 *
 * @param hdr:     log header to analyze
 * @param key:     key to search in log header
 * @return         true if key is present, false if key is not
 */
bool loghdr_has_key(struct loghdr *hdr, const char *key);

#ifdef __cplusplus
}
#endif

#endif /* _LIBLOGHDR_H_ */
