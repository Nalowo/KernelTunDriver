/* SPDX-License-Identifier: GPL-2.0 */
/*
 * ktun -- module-internal declarations shared between the .c files.
 * The userspace ABI lives in ktun_ioctl.h, not here.
 */
#ifndef KTUN_H
#define KTUN_H

#include <linux/atomic.h>
#include <linux/list.h>
#include <linux/mutex.h>
#include <linux/netdevice.h>
#include <linux/skbuff.h>
#include <linux/sysfs.h>
#include <linux/types.h>
#include <linux/wait.h>

#define KTUN_MTU_DEFAULT 1500 /* R-7.1 */
#define KTUN_MTU_MIN 68
#define KTUN_MTU_MAX 9000
#define KTUN_TX_QUEUE_LEN 500       /* R-7.2: qdisc length, as in tun.c */
#define KTUN_QUEUE_LIMIT_DEFAULT 64 /* R-10.1 */
#define KTUN_QUEUE_LIMIT_MIN 1
#define KTUN_QUEUE_LIMIT_MAX 4096

/*
 * Per-interface state: lives in netdev_priv(dev), so it is freed together
 * with the net_device by free_netdev() (R-12.2).
 */
struct ktunNet {
  struct net_device *_dev;
  struct sk_buff_head
      _txQueue; /* ndo_start_xmit -> read(); own spinlock (R-7.6) */
  wait_queue_head_t _readWait; /* read()/poll() sleep here (§2.9) */
  unsigned int _queueLimit;    /* READ_ONCE/WRITE_ONCE only (R-8.4) */
  pid_t _ownerPid;             /* task_tgid_vnr(current) at ATTACH */
  atomic_long_t _truncated;    /* R-4.5, R-7.5 */
  struct list_head _node;      /* entry in ktunList */
};

/* Per-open-file state: file->private_data (R-2.2). */
struct ktunFile {
  struct mutex _lock;      /* serialises ATTACH on one file (R-3.1) */
  struct net_device *_dev; /* NULL until ATTACH */
};

/* main.c: global interface list (R-9.4) */
extern struct list_head ktunList;
extern struct mutex ktunListLock;

/* chardev.c */
extern const struct file_operations ktunFops;

/* netdev.c */
int KtunNetCreate(const char *name, struct net_device **devOut);
void KtunNetDestroy(struct net_device *dev);

/* procfs.c */
int KtunProcInit(void);
void KtunProcExit(void);

/* sysfs.c */
extern unsigned int ktunDefaultQueueLimit; /* READ_ONCE/WRITE_ONCE only */
extern const struct attribute_group *ktunDevGroups[];
extern const struct attribute_group ktunNetGroup;

#endif /* KTUN_H */
