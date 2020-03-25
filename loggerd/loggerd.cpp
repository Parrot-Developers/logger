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

#include <schedcfg.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <getopt.h>

#include <vector>
#include <map>

#include <shs.h>
#include <libpomp.h>
#include <liblogger.hpp>
#include <shs_manager.hpp>
#include <logidxproperty.hpp>
#include <putils/properties.h>

#define ULOG_TAG loggerd
#include <ulog.h>
ULOG_DECLARE_TAG(ULOG_TAG);

/* directory where plugins should be located */
#define LOGGERD_PLUGIN_DIR		"/usr/lib/loggerd-plugins"
#define LOGGERD_SHS_ROOT		"logger"

/**
 * Global daemon object, just so signal handler can access it.
 */
static loggerd::Loggerd *sDaemon;
static struct pomp_loop *sLoop;
static bool sQuit;

static void sighandler(int signo)
{
	ULOGI("%s: signo=%d(%s)", __func__, signo, strsignal(signo));

	if (signo == SIGUSR1) {
		/* Ask daemon to flush */
		sDaemon->requestFlush();
	} else if (signo == SIGUSR2) {
		/* Ask daemon to rotate */
		sDaemon->requestRotate();
	} else {
		/* Ask daemon to quit */
		sQuit = true;
		pomp_loop_wakeup(sLoop);
	}
}

static int apply_sched()
{
	int ret;
	struct schedcfg *schedcfg;

	schedcfg = schedcfg_new(NULL);
	if (schedcfg == NULL) {
		ULOGE("failed to create schedcfg");
		return -EINVAL;
	}

	ret = schedcfg_selfconf(schedcfg, "loggerd");
	if (ret == -ENOENT) {
		ULOGN("keeping default scheduling");
	} else if (ret < 0) {
		ULOG_ERRNO("can't reconfigure process thread", -ret);
	}

	schedcfg_destroy(schedcfg);
	return ret;
}

static void usage(const char *progname)
{
	fprintf(stderr,
		"usage: %s [<options>] [<instance>:<dir>...]\n"
		"Logger daemon.\n"
		"\n"
		"  -h --help : print this help message and exit\n"
		"  -p --plugins-dir : plugins directory (default: %s)\n"
		"  -o --output-dir : output directory.\n"
		"                    (default: current directory)\n"
		"  -s --secure : enable encryption unless system property\n"
		"                rw.debuggable is set.\n"
		"  -n --shs-server-name : name of shs server\n"
		"                         for inspecting debug variables\n"
		"                         (default: " LOGGERD_SHS_ROOT  ")\n"
		"  -a --secure-always : enable encryption  unconditionally.\n"
		"  -f --min-free-space <size> : minimum size to keep free on\n"
		"                               filesystem before stopping log\n"
		"                               (0 to disable check).\n"
		"  -u --max-use-space <size> : maximum size of used space\n"
		"                             (0 for no limit).\n"
		"  -m --max-log-size <size> : maximum size of log\n"
		"                             (0 for no limit).\n"
		"  -i --min-log-size <size> : size reserved for a new log \n"
		"                             when removing old ones\n"
		"                             (0 for no size reserved).\n"
		"  -c --max-log-count : maximum number of log files\n"
		"                       (0 for no limit).\n"
		"  -P --persistent-property-name : name of persistent property\n"
		"                                  to use to define log index.\n"
		"\n",
		progname, LOGGERD_PLUGIN_DIR);
}

int main(int argc, char *argv[])
{
	int res;
	size_t pos;
	std::string str;
	std::string key, val;
	int c = 0, optidx = 0;
	int status = EXIT_SUCCESS;
	std::string pluginDir = LOGGERD_PLUGIN_DIR;
	loggerd::Loggerd::Options opt;
	sLoop = pomp_loop_new();
	loggerd::LogIdxProperty logIdxProperty;
	char debuggable[SYS_PROP_VALUE_MAX] = "";

	using ExtraProperty = loggerd::Loggerd::ExtraProperty;

	opt.encrypted = false;
	opt.outputDir = ".";
	opt.minFreeSpace = 0;
	opt.maxLogSize = 0;
	opt.minLogSize = 0;
	opt.maxLogCount = 0;
	opt.maxUsedSpace = 0;
	opt.logIdxManager = nullptr;

	loggerd::ShsManager settings(sLoop, LOGGERD_SHS_ROOT);

	const struct option long_options[] = {
		{"help",       no_argument,       nullptr, 'h' },
		{"output-dir", required_argument, nullptr, 'o' },
		{"plugin-dir", required_argument, nullptr, 'p' },
		{"secure",     no_argument,       nullptr, 's' },
		{"secure-always", no_argument,    nullptr, 'a' },
		{"min-free-space", required_argument, nullptr, 'f' },
		{"max-log-size", required_argument, nullptr, 'm' },
		{"min-log-size", required_argument, nullptr, 'i' },
		{"max-log-count", required_argument, nullptr, 'c' },
		{"max-use-space", required_argument, nullptr, 'u' },
		{"shs-server-name", required_argument, nullptr, 'n' },
		{"persistent-property-name", required_argument, nullptr, 'P' },
		{"extra-header-property", required_argument, nullptr, 'x'},
		{nullptr,      0,                 nullptr, 0   },
	};
	const char short_options[] = "ho:p:asm:c:u:f:i:n:P:x:";

	/* Parse options */
	while ((c = getopt_long(argc, argv, short_options,
			long_options, &optidx)) != -1) {
		switch (c) {
		case 'h':
			usage(argv[0]);
			exit(EXIT_SUCCESS);
			break;
		case 'o':
			opt.outputDir = optarg;
			break;
		case 'p':
			pluginDir = optarg;
			break;
		case 's':
			sys_prop_get("rw.debuggable", debuggable, "0");
			opt.encrypted = strcmp(debuggable, "1") != 0;
			break;
		case 'a':
			opt.encrypted = true;
			break;
		case 'f':
			opt.minFreeSpace = atoll(optarg);
			break;
		case 'm':
			opt.maxLogSize = atoll(optarg);
			break;
		case 'i':
			opt.minLogSize = atoll(optarg);
			break;
		case 'c':
			opt.maxLogCount = atoll(optarg);
			break;
		case 'u':
			opt.maxUsedSpace = atoll(optarg);
			break;
		case 'n':
			settings = loggerd::ShsManager(sLoop, optarg);
			break;
		case 'P':
			logIdxProperty = loggerd::LogIdxProperty(optarg);
			opt.logIdxManager = &logIdxProperty;
			break;
		case 'x':
			str = optarg;
			pos = str.find(':');
			key = std::string(str, 0, pos);
			val.clear();
			if (pos != std::string::npos)
				val = std::string(str, pos + 1);
			opt.extraProps.push_back(ExtraProperty(key, val.length(), val));
			break;
		case '?':
			ULOGW("Invalid option in command line");
			goto error;
		default:
			break;
		}
	}

	/* TODO support all kind of size representation in options; %, M, Mi,
	 * Gi, ... you name it. */

	/* Apply priority/affinity to main thread, potential other threads will
	 * inherit them. */
	apply_sched();

	/* Create daemon object, setup signal handler */
	sQuit = false;
	sDaemon = loggerd::Loggerd::create(sLoop, opt, settings);
	signal(SIGINT, &sighandler);
	signal(SIGTERM, &sighandler);
	signal(SIGUSR1, &sighandler);
	signal(SIGUSR2, &sighandler);
	signal(SIGPIPE, SIG_IGN);

	/* Run the daemon with plugins */
	sDaemon->loadPlugins(pluginDir);
	sDaemon->start();
	while (!sQuit)
		pomp_loop_wait_and_process(sLoop, -1);
	sDaemon->stop();
	sDaemon->destroyLogSources();
	sDaemon->destroyDirectWriters();
	sDaemon->unloadPlugins();

	status = EXIT_SUCCESS;
	goto out;

error:
	status = EXIT_FAILURE;
out:
	/* Cleanup */
	signal(SIGINT, SIG_DFL);
	signal(SIGTERM, SIG_DFL);
	signal(SIGUSR1, SIG_DFL);
	signal(SIGPIPE, SIG_DFL);

	if (sDaemon != nullptr) {
		loggerd::Loggerd::destroy(sDaemon);
		sDaemon = nullptr;
	}

	/* Cleanup loop */
	res = pomp_loop_destroy(sLoop);
	if (res < 0)
		ULOG_ERRNO("pomp_loop_destroy", -res);
	sLoop = nullptr;

	return status;
}
