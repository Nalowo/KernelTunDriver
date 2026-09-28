// SPDX-License-Identifier: GPL-2.0
#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/device.h>
#include <linux/kstrtox.h>
#include <linux/sysfs.h>

#include "ktun.h"

unsigned int ktunDefaultQueueLimit = KTUN_QUEUE_LIMIT_DEFAULT;

static int KtunParseQueueLimit(const char *buf, unsigned int *out) {
  unsigned int val;
  int err = kstrtouint(buf, 10, &val);
  if (err)
    return -EINVAL;
  if (val < KTUN_QUEUE_LIMIT_MIN || val > KTUN_QUEUE_LIMIT_MAX)
    return -EINVAL;
  *out = val;
  return 0;
}

static ssize_t KtunDefaultQueueLimitShow(struct device *d,
                                         struct device_attribute *attr,
                                         char *buf) {
  return sysfs_emit(buf, "%u\n", READ_ONCE(ktunDefaultQueueLimit));
}

static ssize_t KtunDefaultQueueLimitStore(struct device *d,
                                          struct device_attribute *attr,
                                          const char *buf, size_t count) {
  unsigned int val;
  int err = KtunParseQueueLimit(buf, &val);
  if (err)
    return err;
  WRITE_ONCE(ktunDefaultQueueLimit, val);
  return count;
}

static ssize_t KtunInterfaceCountShow(struct device *d,
                                      struct device_attribute *attr,
                                      char *buf) {
  struct list_head *pos;
  unsigned int count = 0;

  mutex_lock(&ktunListLock);
  list_for_each(pos, &ktunList) count++;
  mutex_unlock(&ktunListLock);

  return sysfs_emit(buf, "%u\n", count);
}

static struct device_attribute ktunDevAttrDefaultQueueLimit =
    __ATTR(default_queue_limit, 0644, KtunDefaultQueueLimitShow,
           KtunDefaultQueueLimitStore);
static struct device_attribute ktunDevAttrInterfaceCount =
    __ATTR(interface_count, 0444, KtunInterfaceCountShow, NULL);

static struct attribute *ktunDevAttrs[] = {
    &ktunDevAttrDefaultQueueLimit.attr,
    &ktunDevAttrInterfaceCount.attr,
    NULL,
};

static const struct attribute_group ktunDevGroup = {
    .attrs = ktunDevAttrs,
};

const struct attribute_group *ktunDevGroups[] = {
    &ktunDevGroup,
    NULL,
};

static struct ktunNet *KtunNetFromDevice(struct device *d) {
  return netdev_priv(to_net_dev(d));
}

static ssize_t KtunQueueLimitShow(struct device *d,
                                  struct device_attribute *attr, char *buf) {
  return sysfs_emit(buf, "%u\n", READ_ONCE(KtunNetFromDevice(d)->_queueLimit));
}

static ssize_t KtunQueueLimitStore(struct device *d,
                                   struct device_attribute *attr,
                                   const char *buf, size_t count) {
  struct net_device *dev = to_net_dev(d);
  struct ktunNet *kn = netdev_priv(dev);
  unsigned int val;
  int err = KtunParseQueueLimit(buf, &val);
  if (err)
    return err;

  WRITE_ONCE(kn->_queueLimit, val);
  if (netif_running(dev) && netif_queue_stopped(dev) &&
      skb_queue_len_lockless(&kn->_txQueue) < val)
    netif_wake_queue(dev);
  return count;
}

static ssize_t KtunQueueLenShow(struct device *d, struct device_attribute *attr,
                                char *buf) {
  return sysfs_emit(buf, "%u\n",
                    skb_queue_len_lockless(&KtunNetFromDevice(d)->_txQueue));
}

static ssize_t KtunOwnerPidShow(struct device *d, struct device_attribute *attr,
                                char *buf) {
  return sysfs_emit(buf, "%d\n", KtunNetFromDevice(d)->_ownerPid);
}

static struct device_attribute ktunNetAttrQueueLimit =
    __ATTR(queue_limit, 0644, KtunQueueLimitShow, KtunQueueLimitStore);
static struct device_attribute ktunNetAttrQueueLen =
    __ATTR(queue_len, 0444, KtunQueueLenShow, NULL);
static struct device_attribute ktunNetAttrOwnerPid =
    __ATTR(owner_pid, 0444, KtunOwnerPidShow, NULL);

static struct attribute *ktunNetAttrs[] = {
    &ktunNetAttrQueueLimit.attr,
    &ktunNetAttrQueueLen.attr,
    &ktunNetAttrOwnerPid.attr,
    NULL,
};

const struct attribute_group ktunNetGroup = {
    .name = "ktun",
    .attrs = ktunNetAttrs,
};
