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
#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netinet/ip.h>
#include <netinet/ip_icmp.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>

#include "ktun_ioctl.h"

#define KTUN_DEV_PATH "/dev/ktun"

struct options {
  const char *name; /* -n, NULL = "ktun%d" */
  unsigned int mtu; /* -m, 0 = leave as is */
};

static void Usage(const char *prog) {
  fprintf(stderr,
          "usage: %s [-n NAME] [-m MTU] dump\n"
          "       %s [-n NAME] [-m MTU] echo\n"
          "       %s selftest\n",
          prog, prog, prog);
}

static int OpenAndAttach(const struct options *opts, char name[IFNAMSIZ]) {
  int fd = open(KTUN_DEV_PATH, O_RDWR);
  if (fd < 0)
    return -1;

  struct ktunAttach req = {0};
  if (opts->name)
    strncpy(req.name, opts->name, IFNAMSIZ - 1);

  if (ioctl(fd, KTUN_IOC_ATTACH, &req) < 0)
    goto fail;

  if (opts->mtu) {
    __u32 mtu = opts->mtu;
    if (ioctl(fd, KTUN_IOC_SET_MTU, &mtu) < 0)
      goto fail;
  }

  memcpy(name, req.name, IFNAMSIZ);
  printf("%s\n", name);
  return fd;

fail:;
  int savedErrno = errno;
  close(fd);
  errno = savedErrno;
  return -1;
}

static void PrintPacket(const char *name, const unsigned char *buf,
                        size_t len) {
  const unsigned int version = len ? buf[0] >> 4 : 0;

  if (version == 6) {
    printf("%s: IPv6 len=%zu (skipped)\n", name, len);
    return;
  }
  if (version != 4 || len < sizeof(struct iphdr)) {
    printf("%s: unknown version=%u len=%zu\n", name, version, len);
    return;
  }

  const struct iphdr *ip = (const struct iphdr *)buf;
  const size_t ipHdrLen = ip->ihl * 4;
  char src[INET_ADDRSTRLEN], dst[INET_ADDRSTRLEN];
  inet_ntop(AF_INET, &ip->saddr, src, sizeof(src));
  inet_ntop(AF_INET, &ip->daddr, dst, sizeof(dst));

  if (ip->protocol != IPPROTO_ICMP || ipHdrLen < sizeof(struct iphdr) ||
      ipHdrLen + sizeof(struct icmphdr) > len) {
    printf("%s: IPv4 proto=%u %s -> %s len=%zu\n", name, ip->protocol, src,
           dst, len);
    return;
  }

  const struct icmphdr *icmp = (const struct icmphdr *)(buf + ipHdrLen);
  printf("%s: IPv4 ICMP %s -> %s ", name, src, dst);
  if (icmp->type == ICMP_ECHO)
    printf("echo request");
  else if (icmp->type == ICMP_ECHOREPLY)
    printf("echo reply");
  else
    printf("type=%u", icmp->type);
  printf(" id=%u seq=%u len=%zu\n", ntohs(icmp->un.echo.id),
         ntohs(icmp->un.echo.sequence), len);
}

static int CmdDump(const struct options *opts) {
  char name[IFNAMSIZ];
  int fd = OpenAndAttach(opts, name);

  if (fd < 0) {
    fprintf(stderr, "attach: %s\n", strerror(errno)); /* R-U.4 */
    return 1;
  }

  static unsigned char buf[65536]; /* > KTUN_MTU_MAX; static: off the stack */
  for (;;) {
    ssize_t n = read(fd, buf, sizeof(buf));
    if (n < 0) {
      if (errno == EINTR)
        continue;
      fprintf(stderr, "read: %s\n", strerror(errno)); /* R-U.4 */
      close(fd);
      return 1;
    }
    PrintPacket(name, buf, n);
  }
}

/* §5.3 */
static int CmdEcho(const struct options *opts) {
  /* TODO: like dump, plus IPv4 ICMP echo request -> reply */
  (void)opts;
  fprintf(stderr, "echo: not implemented\n");
  return 1;
}

/* §5.4 */
static int CmdSelftest(void) {
  /* TODO: checks 1-9, PASS/FAIL per check, exit 0 only if all pass */
  fprintf(stderr, "selftest: not implemented\n");
  return 1;
}

int main(int argc, char **argv) {
  struct options opts = {0};
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
