// SPDX-License-Identifier: GPL-2.0
/*
 * ktun -- a simplified TUN-type virtual network interface driver.
 *
 *   write() to /dev/ktun -> sk_buff -> netif_rx()      -> stack sees an RX
 * packet stack sends to ktunN -> ndo_start_xmit() -> queue  -> read() from
 * /dev/ktun
 *
 * This file: module init/exit, the misc device and the global interface list.
 * R-*, T-* and §-numbers refer to the project requirements document.
 */
#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/miscdevice.h>
#include <linux/module.h>

#include "ktun.h"

LIST_HEAD(ktunList);
DEFINE_MUTEX(ktunListLock);

/* R-2.1: misc, dynamic minor, /dev/ktun, 0600; R-10.1: device attributes */
static struct miscdevice ktunMiscDev = {
    .minor = MISC_DYNAMIC_MINOR,
    .name = "ktun",
    .fops = &ktunFops,
    .groups = ktunDevGroups,
    .mode = 0600,
};

/* R-1.1: misc device first, then /proc/ktun; roll back on failure. */
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

/*
 * R-1.2: reverse order. No live interfaces can exist here: every open
 * /dev/ktun holds a module reference via .owner (requirements §3).
 */
static void __exit KtunExit(void) {
  KtunProcExit();
  misc_deregister(&ktunMiscDev);
  pr_info("unloaded\n");
}

module_init(KtunInit);
module_exit(KtunExit);

MODULE_LICENSE("GPL"); /* R-1.4: most of the netdev API is EXPORT_SYMBOL_GPL */
MODULE_AUTHOR("Nalowo");
MODULE_DESCRIPTION("Simplified TUN-type virtual network interface");
MODULE_VERSION("0.1");
