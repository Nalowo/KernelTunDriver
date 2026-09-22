/* SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note */
/*
 * ktun ioctl ABI -- shared by the module (src/) and ktunctl (tools/).
 *
 * Only types available on both sides: __u32 from <linux/types.h>, IFNAMSIZ
 * from <linux/if.h>. Fixed-width fields, so kernel and utility agree on the
 * byte layout (requirements §4.3).
 *
 * Userspace note: <linux/if.h> clashes with glibc's <net/if.h>; include only
 * one of them in the same translation unit.
 */
#ifndef KTUN_IOCTL_H
#define KTUN_IOCTL_H

#include <linux/if.h>
#include <linux/ioctl.h>
#include <linux/types.h>

#define KTUN_IOC_MAGIC                                                         \
  0xF0 /* not listed in Documentation/userspace-api/ioctl/ioctl-number.rst */

struct ktunAttach {
  char name[IFNAMSIZ]; /* in: wanted name, "" = "ktun%d"; out: actual name */
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

#endif /* KTUN_IOCTL_H */
