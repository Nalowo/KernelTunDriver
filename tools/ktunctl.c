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
#include <poll.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>

#include "ktun_ioctl.h"

#define KTUN_DEV_PATH "/dev/ktun"

struct options {
  const char *name; /* -n, NULL = "ktun%d" */
  unsigned int mtu; /* -m, 0 = оставить как есть */
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

// RFC 1071: сумма 16-битных слов в сетевом порядке, переносы из старших
// разрядов возвращаются в младшие, результат инвертируется. Возвращает
// значение уже в сетевом порядке — его можно класть прямо в заголовок.
static uint16_t Checksum(const void *data, size_t len) {
  const unsigned char *p = data;
  uint32_t sum = 0;

  while (len > 1) {
    sum += (uint32_t)p[0] << 8 | p[1]; // слово big-endian, как в сети
    p += 2;
    len -= 2;
  }
  if (len) // нечётная длина: последний байт дополняется нулём справа
    sum += (uint32_t)p[0] << 8;

  while (sum >> 16) // свернуть переносы (end-around carry)
    sum = (sum & 0xffff) + (sum >> 16);

  return htons((uint16_t)~sum);
}

// Превращает ICMP echo request в echo reply на месте (§5, шаги 1-6).
// Возвращает 0, если пакет не echo request и отвечать не нужно.
static int MakeEchoReply(unsigned char *buf, size_t len) {
  if (len < sizeof(struct iphdr) || buf[0] >> 4 != 4)
    return 0;

  struct iphdr *ip = (struct iphdr *)buf;
  const size_t ipHdrLen = ip->ihl * 4;
  if (ipHdrLen < sizeof(struct iphdr) || ipHdrLen > len ||
      ip->protocol != IPPROTO_ICMP)
    return 0;

  const size_t icmpLen = len - ipHdrLen;
  if (icmpLen < sizeof(struct icmphdr))
    return 0;
  struct icmphdr *icmp = (struct icmphdr *)(buf + ipHdrLen);
  if (icmp->type != ICMP_ECHO)
    return 0;

  const uint32_t saddr = ip->saddr; // адреса местами: ответ идёт отправителю
  ip->saddr = ip->daddr;
  ip->daddr = saddr;
  ip->ttl = 64;

  ip->check = 0; // поле суммы входит в сумму, поэтому сначала обнулить
  ip->check = Checksum(ip, ipHdrLen);

  icmp->type = ICMP_ECHOREPLY; // id, seq и данные остаются — по ним ping
  icmp->code = 0;              // сопоставляет ответ с запросом
  icmp->checksum = 0;
  icmp->checksum = Checksum(icmp, icmpLen); // заголовок ICMP + данные
  return 1;
}

// Общий цикл dump/echo: ждём пакет в poll(), читаем, печатаем, в режиме
// echo отвечаем на echo request. Выход — только по сигналу или ошибке.
static int RunLoop(const struct options *opts, int echo) {
  char name[IFNAMSIZ];
  int fd = OpenAndAttach(opts, name);

  if (fd < 0) {
    fprintf(stderr, "attach: %s\n", strerror(errno));
    return 1;
  }

  static unsigned char buf[65536]; // > KTUN_MTU_MAX; static — не на стеке
  struct pollfd pfd = {.fd = fd, .events = POLLIN};
  for (;;) {
    if (poll(&pfd, 1, -1) < 0) { // спим в ядре до wake_up из xmit
      if (errno == EINTR)
        continue;
      fprintf(stderr, "poll: %s\n", strerror(errno));
      break;
    }
    if (pfd.revents & POLLERR) { // файл не привязан
      fprintf(stderr, "poll: not attached\n");
      break;
    }
    if (!(pfd.revents & POLLIN))
      continue;

    // после POLLIN не уснёт: пакет уже в очереди, читатель у неё один
    ssize_t n = read(fd, buf, sizeof(buf));
    if (n < 0) {
      if (errno == EINTR)
        continue;
      fprintf(stderr, "read: %s\n", strerror(errno));
      break;
    }
    PrintPacket(name, buf, n);

    if (!echo || !MakeEchoReply(buf, n))
      continue;
    if (write(fd, buf, n) != n) { // один write = один пакет в стек
      fprintf(stderr, "write: %s\n", strerror(errno));
      break;
    }
    printf("%s:   -> echo reply written\n", name);
  }

  close(fd);
  return 1;
}

static int CmdDump(const struct options *opts) { return RunLoop(opts, 0); }

static int CmdEcho(const struct options *opts) { return RunLoop(opts, 1); }

static int selftestFailed;
static void Expect(int num, const char *what, int ret, int err, int want) {
  const int ok = ret < 0 && err == want;
  if (ok)
    printf("PASS %d %s -> %s\n", num, what, strerror(want));
  else
    printf("FAIL %d %s: want %s, got %s\n", num, what, strerror(want),
           ret < 0 ? strerror(err) : "success");
  selftestFailed |= !ok;
}

static int CmdSelftest(void) {
  unsigned char buf[64] = {0x45}; // минимальный IPv4-заголовок (20 байт)
  int ret;

  int fd = open(KTUN_DEV_PATH, O_RDWR);
  if (fd < 0) {
    fprintf(stderr, "open: %s\n", strerror(errno));
    return 1;
  }

  ret = read(fd, buf, sizeof(buf));
  Expect(1, "read unattached", ret, errno, EBADFD);

  ret = write(fd, buf, 20);
  Expect(2, "write unattached", ret, errno, EBADFD);

  ret = ioctl(fd, _IO(KTUN_IOC_MAGIC, 0x7f));
  Expect(3, "unknown ioctl", ret, errno, ENOTTY);

  struct ktunAttach req = {0};
  ret = ioctl(fd, KTUN_IOC_ATTACH, &req);
  if (ret == 0) {
    printf("PASS 4 attach -> %s\n", req.name);
  } else {
    printf("FAIL 4 attach: %s\n", strerror(errno));
    selftestFailed = 1;
    close(fd);
    printf("selftest: FAIL (checks 5-9 need an attached file)\n");
    return 1;
  }
  ret = ioctl(fd, KTUN_IOC_ATTACH, &req);
  Expect(4, "second attach", ret, errno, EBUSY);

  ret = write(fd, buf, 20);
  Expect(5, "write while down", ret, errno, EIO);

  __u32 mtu = 10;
  ret = ioctl(fd, KTUN_IOC_SET_MTU, &mtu);
  Expect(6, "SET_MTU 10", ret, errno, EINVAL);
  mtu = 100000;
  ret = ioctl(fd, KTUN_IOC_SET_MTU, &mtu);
  Expect(6, "SET_MTU 100000", ret, errno, EINVAL);

  mtu = 1400;
  struct ktunInfo info = {0};
  if (ioctl(fd, KTUN_IOC_SET_MTU, &mtu) < 0) {
    printf("FAIL 7 SET_MTU 1400: %s\n", strerror(errno));
    selftestFailed = 1;
  } else if (ioctl(fd, KTUN_IOC_GET_INFO, &info) < 0) {
    printf("FAIL 7 GET_INFO: %s\n", strerror(errno));
    selftestFailed = 1;
  } else if (info.mtu != 1400) {
    printf("FAIL 7 GET_INFO mtu=%u, want 1400\n", info.mtu);
    selftestFailed = 1;
  } else {
    printf("PASS 7 SET_MTU 1400 -> GET_INFO mtu=1400\n");
  }

  fcntl(fd, F_SETFL, fcntl(fd, F_GETFL) | O_NONBLOCK);
  ret = read(fd, buf, sizeof(buf));
  Expect(8, "nonblocking read, empty queue", ret, errno, EAGAIN);

  int fd2 = open(KTUN_DEV_PATH, O_RDWR);
  if (fd2 < 0) {
    printf("FAIL 9 open: %s\n", strerror(errno));
    selftestFailed = 1;
  } else {
    struct ktunAttach lo = {.name = "lo"};
    ret = ioctl(fd2, KTUN_IOC_ATTACH, &lo);
    Expect(9, "attach \"lo\"", ret, errno, EEXIST);
    close(fd2);
  }

  close(fd); // release удалит интерфейс
  printf("selftest: %s\n", selftestFailed ? "FAIL" : "PASS");
  return selftestFailed;
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
  setvbuf(stdout, NULL, _IOLBF, 0); // построчно даже при выводе в файл

  if (strcmp(cmd, "dump") == 0)
    return CmdDump(&opts);
  if (strcmp(cmd, "echo") == 0)
    return CmdEcho(&opts);
  if (strcmp(cmd, "selftest") == 0)
    return CmdSelftest();

  Usage(argv[0]);
  return 1;
}
