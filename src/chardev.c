// SPDX-License-Identifier: GPL-2.0
/*
 * ktun -- /dev/ktun file_operations: open/release, read, write, poll, ioctl.
 * One open file = one packet queue = at most one interface (requirements §3).
 */
#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/fs.h>
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
static int KtunChrRelease(struct inode *inode, struct file *file) {
  struct ktunFile *kf = file->private_data;
  if (kf->_dev)
    KtunNetDestroy(kf->_dev);
  kfree(kf);
  return 0;
}

static ssize_t KtunChrRead(struct file *file, char __user *ubuf, size_t count,
                           loff_t *ppos) {
  /* TODO R-4.1..R-4.8: one read = one packet; copy_to_user outside locks. */
  return -EBADFD;
}

static ssize_t KtunChrWrite(struct file *file, const char __user *ubuf,
                            size_t count, loff_t *ppos) {
  /* TODO R-5.1..R-5.8: one write = one packet -> skb -> netif_rx(). */
  return -EBADFD;
}

static __poll_t KtunChrPoll(struct file *file, poll_table *wait) {
  /* TODO R-6.1..R-6.4 */
  return EPOLLERR;
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
