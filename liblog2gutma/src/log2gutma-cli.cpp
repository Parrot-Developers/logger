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

#include <log2gutma/log2gutma.hpp>

#include <iostream>
#define ULOG_TAG log2gutma_cli
#include <ulog.h>
ULOG_DECLARE_TAG(ULOG_TAG);

#include <getopt.h>

static void usage(const std::string &prog)
{
	std::cerr << "usage: " << prog << " [<options>] <input>" << std::endl;
	std::cerr << std::endl;
	std::cerr << "  -h --help: print this help message and exit" <<
								std::endl;
	std::cerr << "  -o --output-dir output directory." << std::endl;
	std::cerr << "                    (default: /mnt/user)" << std::endl;
	std::cerr << "  -i --input-file IN_FILE: input file." << std::endl;
}

int main(int argc, char **argv)
{
	int c = 0;
	int optidx = 0;
	int status = EXIT_FAILURE;
	std::string in_file = "", out_file = "";
	enum log2gutma::convert_status ret = log2gutma::STATUS_ERROR;

	const struct option long_options[] = {
		{"help",	no_argument,	   NULL, 'h' },
		{"output-dir",	required_argument, NULL, 'o' },
		{"input-file",	required_argument, NULL, 'i' },
		{NULL,		0,                 NULL,  0  }
	};
	const char short_options[] = "ho:i:";

	while ((c = getopt_long(argc, argv, short_options,
			long_options, &optidx)) != -1) {
		switch (c) {
		case 'h':
			usage(argv[0]);
			exit(EXIT_SUCCESS);
			break;
		case 'i':
			in_file = optarg;
			break;
		case 'o':
			out_file = optarg;
			break;
		case '?':
			std::cerr << "Invalid option in command line";
			std::cerr << std::endl;
			usage(argv[0]);
			goto out;
			break;
		default:
			break;
		}
	}

	if (in_file == "") {
		std::cerr << "No input file" << std::endl;
		goto out;
	}

	if (out_file == "") {
		std::cerr << "No output file" << std::endl;
		goto out;
	}

	ret = log2gutma::convert(in_file, out_file, false);
	if (ret == log2gutma::STATUS_ERROR) {
		std::cerr << "Impossible to convert log file." << std::endl;
		goto out;
	} else if (ret == log2gutma::STATUS_NOFLIGHT) {
		std::cout << "No need to convert this log file: no takeoff";
		std::cout << std::endl;
	}

	status = EXIT_SUCCESS;

out:
	return status;
}
