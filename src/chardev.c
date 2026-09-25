// SPDX-License-Identifier: GPL-2.0
/*
 * ktun -- /dev/ktun file_operations: open/release, read, write, poll, ioctl.
 * One open file = one packet queue = at most one interface (requirements §3).
 */
#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/fs.h>
#include <linux/ip.h>
#include <linux/module.h>
#include <linux/poll.h>
#include <linux/uaccess.h>

#include "ktun.h"
#include "ktun_ioctl.h"

// вызывается на open(), устанавливает локальный контекст
static int KtunChrOpen(struct inode *inode, struct file *file) {
  struct ktunFile *kf = kzalloc(sizeof(*kf), GFP_KERNEL);
  if (kf == NULL)
    return -ENOMEM;

  mutex_init(&kf->_lock);
  file->private_data = kf;

  return 0;
}

// вызывается на close()
static int KtunChrRelease(struct inode *, struct file *file) {
  struct ktunFile *kf = file->private_data;
  if (kf->_dev)
    KtunNetDestroy(kf->_dev);
  kfree(kf);
  return 0;
}

// выборка пакета из буфера и отправка его вызвавшему read()
static ssize_t KtunChrRead(struct file *file, char __user *ubuf, size_t count,
                           loff_t *) {
  struct ktunFile *kf = file->private_data;
  if (kf->_dev == NULL)
    return -EBADFD;

  if (count == 0)
    return 0;

  ssize_t err = 0;
  struct ktunNet *kn = netdev_priv(kf->_dev);
  struct sk_buff *skb = NULL;
  while (true) {
    skb = skb_dequeue(&kn->_txQueue); // вынуть из головы под спинлоком очереди
    if (skb != NULL)
      break;                        // есть пакет — дальше
    if (file->f_flags & O_NONBLOCK) // неблокирующий режим — сразу "пусто"
      return -EAGAIN;
    err = wait_event_interruptible(kn->_readWait,
                                   !skb_queue_empty_lockless(&kn->_txQueue));
    if (err) // разбудил сигнал (Ctrl+C) — -ERESTARTSYS
      return err;
  }

  smp_mb();
  if (netif_queue_stopped(kf->_dev) &&
      (skb_queue_len_lockless(&kn->_txQueue) < READ_ONCE(kn->_queueLimit)))
    netif_wake_queue(kf->_dev);

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
  struct net_device *dev = kf->_dev;
  if (dev == NULL)
    return -EBADFD;

  if (!(dev->flags & IFF_UP))
    return -EIO;

  if (count < sizeof(struct iphdr) || count > READ_ONCE(dev->mtu))
    return -EINVAL;

  // процессный контекст: можно спать, поэтому GFP_KERNEL
  struct sk_buff *skb = alloc_skb(count, GFP_KERNEL);
  if (skb == NULL)
    return -ENOMEM;

  if (copy_from_user(skb_put(skb, count), ubuf, count)) {
    kfree_skb(skb);
    return -EFAULT;
  }

  // версия IP из уже скопированных байт, а не повторным чтением из userspace
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
  skb_reset_mac_header(skb); // L2-заголовка у TUN нет: mac = network = data
  skb_reset_network_header(skb);

  netif_rx(skb); // skb теперь принадлежит стеку, дальше его не трогаем
  DEV_STATS_INC(dev, rx_packets);
  DEV_STATS_ADD(dev, rx_bytes, count);
  return count;
}

static __poll_t KtunChrPoll(struct file *file, poll_table *wait) {
  struct ktunFile *kf = file->private_data;
  if (kf == NULL)
    return EPOLLERR;

  struct ktunNet *kn = netdev_priv(kf->_dev);
  poll_wait(file, &kn->_readWait, wait);

  __poll_t mask = EPOLLOUT | EPOLLWRNORM;
  if (!skb_queue_empty_lockless(&kn->_txQueue))
    mask |= EPOLLIN | EPOLLRDNORM;

  return mask;
}

// открытый файл получает свой сетевой интерфейс
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

  err = KtunNetCreate(req.name, &kf->_dev);
  if (err)
    goto out;

  strscpy(req.name, kf->_dev->name, IFNAMSIZ);
  if (copy_to_user(uarg, &req, sizeof(req)))
    err = -EFAULT;

out:
  mutex_unlock(&kf->_lock);
  return err;
}

static long KtunChrIoctl(struct file *file, unsigned int cmd,
                         unsigned long arg) {
  switch (cmd) {
  case KTUN_IOC_ATTACH:
    return KtunChrAttach(file->private_data, (void __user *)arg);
  case KTUN_IOC_GET_INFO:
    /* TODO R-3.2 */
    return -EOPNOTSUPP;
  case KTUN_IOC_SET_MTU:
    /* TODO R-3.3: dev_set_mtu() under rtnl_lock() */
    return -EOPNOTSUPP;
  default:
    return -ENOTTY; /* R-3.4 */
  }
}

const struct file_operations ktunFops = {
    .owner = THIS_MODULE, /* pins the module while a file is open */
    .open = KtunChrOpen,
    .release = KtunChrRelease,
    .read = KtunChrRead,
    .write = KtunChrWrite,
    .poll = KtunChrPoll,
    .unlocked_ioctl = KtunChrIoctl,
    .llseek = noop_llseek, /* a packet stream has no position */
};
