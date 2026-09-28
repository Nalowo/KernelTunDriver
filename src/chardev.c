// SPDX-License-Identifier: GPL-2.0
#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/fs.h>
#include <linux/ip.h>
#include <linux/module.h>
#include <linux/poll.h>
#include <linux/rtnetlink.h>
#include <linux/uaccess.h>

#include "ktun.h"
#include "ktun_ioctl.h"

static int KtunChrOpen(struct inode *inode, struct file *file) {
  struct ktunFile *kf = kzalloc(sizeof(*kf), GFP_KERNEL);
  if (kf == NULL)
    return -ENOMEM;

  mutex_init(&kf->_lock);
  file->private_data = kf;
  return 0;
}

static int KtunChrRelease(struct inode *inode, struct file *file) {
  struct ktunFile *kf = file->private_data;
  if (kf->_dev)
    KtunNetDestroy(kf->_dev);
  mutex_destroy(&kf->_lock);
  kfree(kf);
  return 0;
}

static ssize_t KtunChrRead(struct file *file, char __user *ubuf, size_t count,
                           loff_t *ppos) {
  struct ktunFile *kf = file->private_data;
  struct net_device *dev = READ_ONCE(kf->_dev);
  if (dev == NULL)
    return -EBADFD;

  if (count == 0)
    return 0;

  struct ktunNet *kn = netdev_priv(dev);
  struct sk_buff *skb;
  while (true) {
    skb = skb_dequeue(&kn->_txQueue);
    if (skb != NULL)
      break;
    if (file->f_flags & O_NONBLOCK)
      return -EAGAIN;
    int err = wait_event_interruptible(
        kn->_readWait, !skb_queue_empty_lockless(&kn->_txQueue));
    if (err)
      return err;
  }

  smp_mb();
  if (netif_running(dev) && netif_queue_stopped(dev) &&
      skb_queue_len_lockless(&kn->_txQueue) < READ_ONCE(kn->_queueLimit))
    netif_wake_queue(dev);

  const size_t n = min_t(size_t, count, skb->len);
  if (n < skb->len)
    atomic_long_inc(&kn->_truncated);

  if (copy_to_user(ubuf, skb->data, n)) {
    kfree_skb(skb);
    return -EFAULT;
  }

  consume_skb(skb);
  return n;
}

static ssize_t KtunChrWrite(struct file *file, const char __user *ubuf,
                            size_t count, loff_t *ppos) {
  struct ktunFile *kf = file->private_data;
  struct net_device *dev = READ_ONCE(kf->_dev);
  if (dev == NULL)
    return -EBADFD;

  if (!(dev->flags & IFF_UP))
    return -EIO;

  if (count < sizeof(struct iphdr) || count > READ_ONCE(dev->mtu))
    return -EINVAL;

  struct sk_buff *skb = alloc_skb(count, GFP_KERNEL);
  if (skb == NULL)
    return -ENOMEM;

  if (copy_from_user(skb_put(skb, count), ubuf, count)) {
    kfree_skb(skb);
    return -EFAULT;
  }

  switch (skb->data[0] >> 4) {
  case 4:
    skb->protocol = htons(ETH_P_IP);
    break;
  case 6:
    skb->protocol = htons(ETH_P_IPV6);
    break;
  default:
    kfree_skb(skb);
    return -EINVAL;
  }

  skb->dev = dev;
  skb_reset_mac_header(skb);
  skb_reset_network_header(skb);

  netif_rx(skb);
  DEV_STATS_INC(dev, rx_packets);
  DEV_STATS_ADD(dev, rx_bytes, count);
  return count;
}

static __poll_t KtunChrPoll(struct file *file, poll_table *wait) {
  struct ktunFile *kf = file->private_data;
  struct net_device *dev = READ_ONCE(kf->_dev);
  if (dev == NULL)
    return EPOLLERR;

  struct ktunNet *kn = netdev_priv(dev);
  poll_wait(file, &kn->_readWait, wait);

  __poll_t mask = EPOLLOUT | EPOLLWRNORM;
  if (!skb_queue_empty_lockless(&kn->_txQueue))
    mask |= EPOLLIN | EPOLLRDNORM;
  return mask;
}

static long KtunChrAttach(struct ktunFile *kf, struct ktunAttach __user *uarg) {
  if (!capable(CAP_NET_ADMIN))
    return -EPERM;

  struct ktunAttach req;
  if (copy_from_user(&req, uarg, sizeof(req)))
    return -EFAULT;
  if (strnlen(req.name, IFNAMSIZ) == IFNAMSIZ)
    return -EINVAL;
  if (req.name[0] == '\0')
    strscpy(req.name, "ktun%d", IFNAMSIZ);

  mutex_lock(&kf->_lock);
  long err;
  if (kf->_dev) {
    err = -EBUSY;
    goto out;
  }

  struct net_device *dev;
  err = KtunNetCreate(req.name, &dev);
  if (err)
    goto out;
  smp_store_release(&kf->_dev, dev);

  strscpy(req.name, dev->name, IFNAMSIZ);
  if (copy_to_user(uarg, &req, sizeof(req)))
    err = -EFAULT;

out:
  mutex_unlock(&kf->_lock);
  return err;
}

static long KtunChrGetInfo(struct net_device *dev,
                           struct ktunInfo __user *uarg) {
  struct ktunNet *kn = netdev_priv(dev);
  struct ktunInfo info = {};

  strscpy(info.name, dev->name, IFNAMSIZ);
  info.ifindex = dev->ifindex;
  info.mtu = READ_ONCE(dev->mtu);
  info.queueLen = skb_queue_len_lockless(&kn->_txQueue);
  info.queueLimit = READ_ONCE(kn->_queueLimit);

  if (copy_to_user(uarg, &info, sizeof(info)))
    return -EFAULT;
  return 0;
}

static long KtunChrSetMtu(struct net_device *dev, __u32 __user *uarg) {
  __u32 mtu;
  if (get_user(mtu, uarg))
    return -EFAULT;
  if (mtu < KTUN_MTU_MIN || mtu > KTUN_MTU_MAX)
    return -EINVAL;

  rtnl_lock();
  int err = dev_set_mtu(dev, mtu);
  rtnl_unlock();
  return err;
}

static long KtunChrIoctl(struct file *file, unsigned int cmd,
                         unsigned long arg) {
  struct ktunFile *kf = file->private_data;
  void __user *uarg = (void __user *)arg;

  if (cmd == KTUN_IOC_ATTACH)
    return KtunChrAttach(kf, uarg);

  if (cmd != KTUN_IOC_GET_INFO && cmd != KTUN_IOC_SET_MTU)
    return -ENOTTY;

  struct net_device *dev = READ_ONCE(kf->_dev);
  if (dev == NULL)
    return -EBADFD;

  return cmd == KTUN_IOC_GET_INFO ? KtunChrGetInfo(dev, uarg)
                                  : KtunChrSetMtu(dev, uarg);
}

const struct file_operations ktunFops = {
    .owner = THIS_MODULE,
    .open = KtunChrOpen,
    .release = KtunChrRelease,
    .read = KtunChrRead,
    .write = KtunChrWrite,
    .poll = KtunChrPoll,
    .unlocked_ioctl = KtunChrIoctl,
    .llseek = noop_llseek,
};
