// SPDX-License-Identifier: GPL-2.0
/*
 * ktun -- the network interface: net_device_ops, packet queue, flow control.
 *
 * ndo_start_xmit runs in atomic (BH) context: no sleeping, no mutexes,
 * no GFP_KERNEL, no copy_to_user (R-7.4, R-11.1).
 */
#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/if_arp.h>
#include <linux/netdevice.h>
#include <linux/skbuff.h>

#include "ktun.h"

static int KtunNetOpen(struct net_device *dev) {
  netif_start_queue(dev);
  return 0;
}

static int KtunNetStop(struct net_device *dev) {
  netif_stop_queue(dev);
  return 0;
}

// перенос пакета из буфера интерфейса в сетевой буфер
static netdev_tx_t KtunNetStartXmit(struct sk_buff *skb,
                                    struct net_device *dev) {
  struct ktunNet *kn = netdev_priv(dev);
  const unsigned int limit = READ_ONCE(kn->_queueLimit);
  if (skb_queue_len_lockless(&kn->_txQueue) >= limit) {
    DEV_STATS_INC(dev, tx_dropped);
    kfree_skb(skb);
    return NETDEV_TX_OK;
  }

  const unsigned int len = skb->len;
  skb_queue_tail(&kn->_txQueue, skb);
  DEV_STATS_INC(dev, tx_packets);
  DEV_STATS_ADD(dev, tx_bytes, len);
  wake_up_interruptible(&kn->_readWait);

  if (skb_queue_len_lockless(&kn->_txQueue) >= limit) {
    netif_stop_queue(dev);
    smp_mb__after_atomic(); // бит stop виден другим CPU до перечитывания длины
    if (skb_queue_len_lockless(&kn->_txQueue) < limit)
      netif_wake_queue(dev);
  }

  return NETDEV_TX_OK;
}

static const struct net_device_ops ktunNetOps = {
    .ndo_open = KtunNetOpen,
    .ndo_stop = KtunNetStop,
    .ndo_start_xmit = KtunNetStartXmit,
};

static void KtunNetSetup(struct net_device *dev) {
  dev->netdev_ops = &ktunNetOps;
  dev->type = ARPHRD_NONE;
  dev->flags = IFF_POINTOPOINT | IFF_NOARP;
  dev->hard_header_len = 0;
  dev->addr_len = 0;
  dev->mtu = KTUN_MTU_DEFAULT;
  dev->max_mtu = KTUN_MTU_MAX;
  dev->min_mtu = KTUN_MTU_MIN;
  dev->tx_queue_len = KTUN_TX_QUEUE_LEN;
}

// создание сетевого интерфейса
int KtunNetCreate(const char *name, struct net_device **devOut) {
  struct net_device *dev;
  dev = alloc_netdev(sizeof(struct ktunNet), name,
                     strchr(name, '%') ? NET_NAME_ENUM : NET_NAME_USER,
                     KtunNetSetup);
  if (!dev)
    return -ENOMEM;

  struct ktunNet *kn = netdev_priv(dev);
  kn->_dev = dev;
  skb_queue_head_init(&kn->_txQueue);
  init_waitqueue_head(&kn->_readWait);
  kn->_queueLimit = READ_ONCE(ktunDefaultQueueLimit);
  atomic_long_set(&kn->_truncated, 0);
  kn->_ownerPid = task_tgid_vnr(current);
  INIT_LIST_HEAD(&kn->_node);

  dev->sysfs_groups[0] = &ktunNetGroup;
  int err = register_netdev(dev);
  if (err) {
    free_netdev(dev);
    return err;
  }

  mutex_lock(&ktunListLock);
  list_add_tail(&kn->_node, &ktunList);
  mutex_unlock(&ktunListLock);

  *devOut = dev;
  return 0;
}

void KtunNetDestroy(struct net_device *dev) {
  struct ktunNet *kn = netdev_priv(dev);

  mutex_lock(&ktunListLock);
  list_del(&kn->_node);
  mutex_unlock(&ktunListLock);

  unregister_netdev(dev);
  skb_queue_purge(&kn->_txQueue);
  free_netdev(dev);
}
