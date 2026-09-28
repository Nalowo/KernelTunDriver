// SPDX-License-Identifier: GPL-2.0
#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/proc_fs.h>
#include <linux/seq_file.h>

#include "ktun.h"

#define KTUN_PROC_NAME "ktun"

static int KtunProcShow(struct seq_file *m, void *v) {
  struct ktunNet *kn;

  seq_printf(m, "%-8s %-6s %-6s %-7s %-11s %-9s %-11s %-9s %-11s %s\n", "name",
             "pid", "state", "queue", "rx_packets", "rx_bytes", "tx_packets",
             "tx_bytes", "tx_dropped", "truncated");

  mutex_lock(&ktunListLock);
  list_for_each_entry(kn, &ktunList, _node) {
    struct net_device *dev = kn->_dev;
    struct rtnl_link_stats64 stats;
    char queue[24];

    dev_get_stats(dev, &stats);
    snprintf(queue, sizeof(queue), "%u/%u",
             skb_queue_len_lockless(&kn->_txQueue), READ_ONCE(kn->_queueLimit));

    seq_printf(
        m, "%-8s %-6d %-6s %-7s %-11llu %-9llu %-11llu %-9llu %-11llu %ld\n",
        dev->name, kn->_ownerPid, netif_running(dev) ? "up" : "down", queue,
        stats.rx_packets, stats.rx_bytes, stats.tx_packets, stats.tx_bytes,
        stats.tx_dropped, atomic_long_read(&kn->_truncated));
  }
  mutex_unlock(&ktunListLock);
  return 0;
}

int KtunProcInit(void) {
  if (!proc_create_single(KTUN_PROC_NAME, 0444, NULL, KtunProcShow))
    return -ENOMEM;
  return 0;
}

void KtunProcExit(void) { remove_proc_entry(KTUN_PROC_NAME, NULL); }
