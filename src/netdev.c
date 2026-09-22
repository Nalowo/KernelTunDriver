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
  /* TODO R-7.3: netif_start_queue() */
  return 0;
}

static int KtunNetStop(struct net_device *dev) {
  /* TODO R-7.3: netif_stop_queue(); queued packets stay readable */
  return 0;
}

static netdev_tx_t KtunNetStartXmit(struct sk_buff *skb,
                                    struct net_device *dev) {
  /*
   * TODO R-7.4, R-8.1, R-8.2: enqueue, count TX, wake readers, stop the
   * queue at the limit and re-check. Until then the skb is ours: free it.
   */
  kfree_skb(skb);
  return NETDEV_TX_OK;
}

static const struct net_device_ops ktunNetOps = {
    .ndo_open = KtunNetOpen,
    .ndo_stop = KtunNetStop,
    .ndo_start_xmit = KtunNetStartXmit,
};

/* alloc_netdev() setup callback. __maybe_unused: drop once KtunNetCreate uses
 * it. */
static void __maybe_unused KtunNetSetup(struct net_device *dev) {
  dev->netdev_ops = &ktunNetOps;
  /*
   * TODO R-7.1: TUN parameters from requirements §2 (type, flags, header_len,
   * addr_len), mtu/min_mtu/max_mtu; R-7.2: tx_queue_len.
   */
}

/*
 * R-3.1: alloc_netdev(sizeof(struct ktunNet), ..., KtunNetSetup), init the
 * private part, dev->sysfs_groups[0] = &ktunNetGroup, register_netdev(),
 * add to ktunList. On success *devOut is the registered interface.
 */
int KtunNetCreate(const char *name, struct net_device **devOut) {
  /* TODO */
  return -EOPNOTSUPP;
}

/* R-12.1 steps 1-4: list_del, unregister_netdev, skb_queue_purge, free_netdev.
 */
void KtunNetDestroy(struct net_device *dev) { /* TODO */ }
