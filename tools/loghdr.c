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

#include <getopt.h>
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <stdbool.h>

#define ULOG_TAG loghdr
#include <ulog.h>
ULOG_DECLARE_TAG(ULOG_TAG);

#include "libloghdr.h"

static void usage(const char *progname)
{
	fprintf(stderr,
		"usage: %s [<options>] <input>\n"
		"\n"
		"  -h --help: print this help message and exit\n"
		"  -k --key KEY: search KEY in header and print it\n"
		"\n",
		progname);
}


int main(int argc, char **argv)
{
	int c = 0, optidx = 0;
	int ret = -1;
	char *inputFile;
	char *key = NULL;
	/* coverity[stack_use_local_overflow] */
	char buf[2048] = "";
	struct loghdr *ptr = NULL;
	const struct option long_options[] = {
		{"help", no_argument,       NULL, 'h' },
		{"key",  required_argument, NULL, 'k' },
		{NULL,   0,                 NULL,  0  },
	};
	const char short_options[] = "hk:";

	/* Parse options */
	while ((c = getopt_long(argc, argv, short_options,
			long_options, &optidx)) != -1) {
		switch (c) {
		case 'h':
			usage(argv[0]);
			ret = 0;
			goto out;
			break;
		case 'k':
			key = optarg;
			break;
		default:
			ULOGW("Invalid option %c in command line", optopt);
			usage(argv[0]);
			goto out;
		}
	}

	if (optind < argc)
		inputFile = argv[optind++];
	else {
		ULOGE("No input file.");
		usage(argv[0]);
		goto out;
	}

	ptr = loghdr_new(inputFile);
	if (!ptr)
		goto out;

	if (key) {
		if (loghdr_has_key(ptr, key))
			printf("[%s]: %s\n", key, loghdr_get_value(ptr, key));
	} else {
		if (loghdr_tostring(ptr, buf, sizeof(buf)) == 0)
			printf("%s", buf);
	}

	ret = 0;

out:
	if (ptr != NULL)
		loghdr_destroy(ptr);

	return ret;
}
