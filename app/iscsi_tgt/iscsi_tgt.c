/*-
 *   BSD LICENSE
 *
 *   Copyright (c) Intel Corporation.
 *   All rights reserved.
 *
 *   Redistribution and use in source and binary forms, with or without
 *   modification, are permitted provided that the following conditions
 *   are met:
 *
 *     * Redistributions of source code must retain the above copyright
 *       notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above copyright
 *       notice, this list of conditions and the following disclaimer in
 *       the documentation and/or other materials provided with the
 *       distribution.
 *     * Neither the name of Intel Corporation nor the names of its
 *       contributors may be used to endorse or promote products derived
 *       from this software without specific prior written permission.
 *
 *   THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 *   "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 *   LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 *   A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 *   OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 *   SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 *   LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 *   DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 *   THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 *   (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 *   OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <inttypes.h>
#include <stdint.h>
#include <stdlib.h>

#include <rte_config.h>
#include <rte_memzone.h>
#include <rte_eal.h>
#include <rte_lcore.h>
#include <rte_malloc.h>
#include <rte_memory.h>

#include "spdk/event.h"
#include "iscsi/iscsi.h"
#include "spdk/log.h"
#include "spdk/net.h"

uint64_t g_flush_timeout;

static void
spdk_iscsi_dump_memory_info(void)
{
	struct rte_malloc_socket_stats stats;
	int i;

	for (i = 0; i < RTE_MAX_NUMA_NODES; i++) {
		rte_malloc_get_socket_stats(i, &stats);
		if (stats.heap_totalsz_bytes > 0)
			fprintf(stderr, "Socket %d: Total memory %"PRIu64" MB,"
				" Free memory %"PRIu64" MB\n",
				i, stats.heap_totalsz_bytes >> 20,
				stats.heap_freesz_bytes >> 20);
	}
}

static void
spdk_sigusr1(int signo __attribute__((__unused__)))
{
	char *config_str = NULL;
	if (spdk_app_get_running_config(&config_str, "iscsi.conf") < 0)
		fprintf(stderr, "Error getting config\n");
	else {
		fprintf(stdout, "============================\n");
		fprintf(stdout, " iSCSI target running config\n");
		fprintf(stdout, "=============================\n");
		fprintf(stdout, "%s", config_str);
	}
	free(config_str);
}

static void
usage(char *executable_name)
{
	printf("%s [options]\n", executable_name);
	printf("options:\n");
	printf(" -c config  config file (default %s)\n", SPDK_ISCSI_DEFAULT_CONFIG);
	printf(" -e mask    tracepoint group mask for spdk trace buffers (default 0x0)\n");
	printf(" -m mask    core mask for DPDK\n");
	printf(" -i instance ID\n");
	printf(" -l facility use specific syslog facility (default %s)\n",
	       SPDK_APP_DEFAULT_LOG_FACILITY);
	printf(" -n channel number of memory channels used for DPDK\n");
	printf(" -p core    master (primary) core for DPDK\n");
	printf(" -s size    memory size in MB for DPDK\n");
#ifdef DEBUG
	printf(" -t flag    trace flag (all, net, iscsi, scsi, target, debug)\n");
#else
	printf(" -t flag    trace flag (not supported - must rebuild with CONFIG_DEBUG=y)\n");
#endif
	printf(" -v         verbose (enable warnings)\n");
	printf(" -H         show this usage\n");
	printf(" -V         show version\n");
	printf(" -d         disable coredump file enabling\n");
}

static void
spdk_startup(spdk_event_t event)
{
	if (getenv("MEMZONE_DUMP") != NULL) {
		rte_memzone_dump(stdout);
		fflush(stdout);
	}

	/* Dump socket memory information */
	spdk_iscsi_dump_memory_info();
}

int
main(int argc, char **argv)
{
	int ch;
	int rc;
	struct spdk_app_opts opts = {};

	/* default value in opts structure */
	spdk_app_opts_init(&opts);

	opts.config_file = SPDK_ISCSI_DEFAULT_CONFIG;
	opts.name = "iscsi";

	while ((ch = getopt(argc, argv, "c:de:i:l:m:n:p:qs:t:H")) != -1) {
		switch (ch) {
		case 'd':
			opts.enable_coredump = false;
			break;
		case 'c':
			opts.config_file = optarg;
			break;
		case 'i':
			opts.instance_id = atoi(optarg);
			break;
		case 'l':
			opts.log_facility = optarg;
			break;
		case 't':
			rc = spdk_log_set_trace_flag(optarg);
			if (rc < 0) {
				fprintf(stderr, "unknown flag\n");
				usage(argv[0]);
				exit(EXIT_FAILURE);
			}
#ifndef DEBUG
			fprintf(stderr, "%s must be built with CONFIG_DEBUG=y for -t flag\n",
				argv[0]);
			usage(argv[0]);
			exit(EXIT_FAILURE);
#endif
			break;
		case 'e':
			opts.tpoint_group_mask = optarg;
			break;
		case 'q':
			spdk_g_notice_stderr_flag = 0;
			break;
		case 'm':
			opts.reactor_mask = optarg;
			break;
		case 'n':
			opts.dpdk_mem_channel = atoi(optarg);
			break;
		case 'p':
			opts.dpdk_master_core = atoi(optarg);
			break;
		case 's':
			opts.dpdk_mem_size = atoi(optarg);
			break;
		case 'H':
		default:
			usage(argv[0]);
			exit(EXIT_SUCCESS);
		}
	}

	if (spdk_g_notice_stderr_flag == 1 &&
	    isatty(STDERR_FILENO) &&
	    !strncmp(ttyname(STDERR_FILENO), "/dev/tty", strlen("/dev/tty"))) {
		printf("Warning: printing stderr to console terminal without -q option specified.\n");
		printf("Suggest using -q to disable logging to stderr and monitor syslog, or\n");
		printf("redirect stderr to a file.\n");
		printf("(Delaying for 10 seconds...)\n");
		sleep(10);
	}

	optind = 1; /* reset the optind */

	opts.shutdown_cb = spdk_iscsi_shutdown;
	opts.usr1_handler = spdk_sigusr1;
	spdk_app_init(&opts);

	printf("Total cores available: %d\n", rte_lcore_count());
	printf("Using net framework %s\n", spdk_net_framework_get_name());
	/* Blocks until the application is exiting */
	rc = spdk_app_start(spdk_startup, NULL, NULL);

	spdk_app_fini();

	return rc;
}