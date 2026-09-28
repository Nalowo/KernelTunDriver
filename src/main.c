// SPDX-License-Identifier: GPL-2.0
#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/miscdevice.h>
#include <linux/module.h>

#include "ktun.h"

LIST_HEAD(ktunList);
DEFINE_MUTEX(ktunListLock);

static struct miscdevice ktunMiscDev = {
    .minor = MISC_DYNAMIC_MINOR,
    .name = "ktun",
    .fops = &ktunFops,
    .groups = ktunDevGroups,
    .mode = 0600,
};

static int __init KtunInit(void) {
  int err;

  err = misc_register(&ktunMiscDev);
  if (err)
    return err;

  err = KtunProcInit();
  if (err)
    goto errMisc;

  pr_info("loaded: /dev/%s (minor %d)\n", ktunMiscDev.name, ktunMiscDev.minor);
  return 0;

errMisc:
  misc_deregister(&ktunMiscDev);
  return err;
}

static void __exit KtunExit(void) {
  KtunProcExit();
  misc_deregister(&ktunMiscDev);
  pr_info("unloaded\n");
}

module_init(KtunInit);
module_exit(KtunExit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Nalowo");
MODULE_DESCRIPTION("Simplified TUN-type virtual network interface");
MODULE_VERSION("0.1");
