// SPDX-License-Identifier: GPL-2.0
/*
 * ktun -- /sys attributes (requirements §4.10).
 *
 *   /sys/class/misc/ktun/       default_queue_limit (rw), interface_count (ro)
 *   /sys/class/net/ktunN/ktun/  queue_limit (rw), queue_len (ro), owner_pid
 * (ro)
 *
 * Attributes are spelled out with __ATTR() instead of DEVICE_ATTR_RW(): the
 * macro would force snake_case callback names (<name>_show/<name>_store).
 */
#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/device.h>
#include <linux/kstrtox.h>
#include <linux/sysfs.h>

#include "ktun.h"

unsigned int ktunDefaultQueueLimit = KTUN_QUEUE_LIMIT_DEFAULT;

/* ---- misc device: /sys/class/misc/ktun/ (R-10.1) ---- */

static ssize_t KtunDefaultQueueLimitShow(struct device *d,
                                         struct device_attribute *attr,
                                         char *buf) {
  /* TODO: sysfs_emit() */
  return -EOPNOTSUPP;
}

static ssize_t KtunDefaultQueueLimitStore(struct device *d,
                                          struct device_attribute *attr,
                                          const char *buf, size_t count) {
  /* TODO R-10.3: kstrtouint(), range check, WRITE_ONCE() */
  return -EOPNOTSUPP;
}

static ssize_t KtunInterfaceCountShow(struct device *d,
                                      struct device_attribute *attr,
                                      char *buf) {
  /* TODO */
  return -EOPNOTSUPP;
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

/* ---- interface: /sys/class/net/ktunN/ktun/ (R-10.2) ---- */

static ssize_t KtunQueueLimitShow(struct device *d,
                                  struct device_attribute *attr, char *buf) {
  /* TODO: to_net_dev(d) -> netdev_priv() -> READ_ONCE(_queueLimit) */
  return -EOPNOTSUPP;
}

static ssize_t KtunQueueLimitStore(struct device *d,
                                   struct device_attribute *attr,
                                   const char *buf, size_t count) {
  /* TODO R-10.3, R-10.4 */
  return -EOPNOTSUPP;
}

static ssize_t KtunQueueLenShow(struct device *d, struct device_attribute *attr,
                                char *buf) {
  /* TODO */
  return -EOPNOTSUPP;
}

static ssize_t KtunOwnerPidShow(struct device *d, struct device_attribute *attr,
                                char *buf) {
  /* TODO */
  return -EOPNOTSUPP;
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

/* Goes into dev->sysfs_groups[0] before register_netdev() (R-3.1). */
const struct attribute_group ktunNetGroup = {
    .name = "ktun",
    .attrs = ktunNetAttrs,
};
