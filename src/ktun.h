/* SPDX-License-Identifier: GPL-2.0 */
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

#define KTUN_MTU_DEFAULT 1500
#define KTUN_MTU_MIN 68
#define KTUN_MTU_MAX 9000
#define KTUN_TX_QUEUE_LEN 500
#define KTUN_QUEUE_LIMIT_DEFAULT 64
#define KTUN_QUEUE_LIMIT_MIN 1
#define KTUN_QUEUE_LIMIT_MAX 4096

struct ktunNet {
  struct net_device *_dev;
  struct sk_buff_head _txQueue;
  wait_queue_head_t _readWait;
  unsigned int _queueLimit;
  pid_t _ownerPid;
  atomic_long_t _truncated;
  struct list_head _node;
};

struct ktunFile {
  struct mutex _lock;
  struct net_device *_dev;
};

extern struct list_head ktunList;
extern struct mutex ktunListLock;

extern const struct file_operations ktunFops;

int KtunNetCreate(const char *name, struct net_device **devOut);
void KtunNetDestroy(struct net_device *dev);

int KtunProcInit(void);
void KtunProcExit(void);

extern unsigned int ktunDefaultQueueLimit;
extern const struct attribute_group *ktunDevGroups[];
extern const struct attribute_group ktunNetGroup;

#endif
