// SPDX-License-Identifier: GPL-2.0
/*
 * ktunctl -- userspace side of ktun (requirements §5).
 *
 *   ktunctl [-n NAME] [-m MTU] dump
 *   ktunctl [-n NAME] [-m MTU] echo
 *   ktunctl selftest
 *
 * Built statically (the guest has no libc): make tools -> build/ktunctl.
 */
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "ktun_ioctl.h"

#define KTUN_DEV_PATH "/dev/ktun"

struct options {
	const char *name;	/* -n, NULL = "ktun%d" */
	unsigned int mtu;	/* -m, 0 = leave as is */
};

static void Usage(const char *prog)
{
	fprintf(stderr,
		"usage: %s [-n NAME] [-m MTU] dump\n"
		"       %s [-n NAME] [-m MTU] echo\n"
		"       %s selftest\n",
		prog, prog, prog);
}

/* R-U.3: open /dev/ktun, ATTACH, optional SET_MTU, print the actual name. */
static int OpenAndAttach(const struct options *opts)
{
	/* TODO */
	(void)opts;
	errno = EOPNOTSUPP;
	return -1;
}

/* §5.2 */
static int CmdDump(const struct options *opts)
{
	int fd = OpenAndAttach(opts);

	if (fd < 0) {
		fprintf(stderr, "attach: %s\n", strerror(errno));	/* R-U.4 */
		return 1;
	}
	/* TODO: read loop, one line per packet */
	close(fd);
	return 0;
}

/* §5.3 */
static int CmdEcho(const struct options *opts)
{
	/* TODO: like dump, plus IPv4 ICMP echo request -> reply */
	(void)opts;
	fprintf(stderr, "echo: not implemented\n");
	return 1;
}

/* §5.4 */
static int CmdSelftest(void)
{
	/* TODO: checks 1-9, PASS/FAIL per check, exit 0 only if all pass */
	fprintf(stderr, "selftest: not implemented\n");
	return 1;
}

int main(int argc, char **argv)
{
	struct options opts = { 0 };
	const char *cmd;
	int opt;

	while ((opt = getopt(argc, argv, "n:m:")) != -1) {
		switch (opt) {
		case 'n':
			opts.name = optarg;
			break;
		case 'm':
			opts.mtu = strtoul(optarg, NULL, 10);
			break;
		default:
			Usage(argv[0]);
			return 1;
		}
	}
	if (optind != argc - 1) {
		Usage(argv[0]);
		return 1;
	}
	cmd = argv[optind];

	if (strcmp(cmd, "dump") == 0)
		return CmdDump(&opts);
	if (strcmp(cmd, "echo") == 0)
		return CmdEcho(&opts);
	if (strcmp(cmd, "selftest") == 0)
		return CmdSelftest();

	Usage(argv[0]);
	return 1;
}
