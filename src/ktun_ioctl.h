/* SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note */
#ifndef KTUN_IOCTL_H
#define KTUN_IOCTL_H

#include <linux/if.h>
#include <linux/ioctl.h>
#include <linux/types.h>

#define KTUN_IOC_MAGIC 0xF0

struct ktunAttach {
  char name[IFNAMSIZ];
};

struct ktunInfo {
  char name[IFNAMSIZ];
  __u32 ifindex;
  __u32 mtu;
  __u32 queueLen;
  __u32 queueLimit;
};

#define KTUN_IOC_ATTACH _IOWR(KTUN_IOC_MAGIC, 1, struct ktunAttach)
#define KTUN_IOC_GET_INFO _IOR(KTUN_IOC_MAGIC, 2, struct ktunInfo)
#define KTUN_IOC_SET_MTU _IOW(KTUN_IOC_MAGIC, 3, __u32)

#endif
