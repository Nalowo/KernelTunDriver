// SPDX-License-Identifier: GPL-2.0
/*
 * ktun -- /proc/ktun: one line per interface (requirements §4.9).
 */
#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/proc_fs.h>
#include <linux/seq_file.h>

#include "ktun.h"

#define KTUN_PROC_NAME "ktun"

static int KtunProcShow(struct seq_file *m, void *v)
{
	seq_puts(m, "name     pid    state  queue   rx_packets  rx_bytes  tx_packets  tx_bytes  tx_dropped  truncated\n");
	/* TODO R-9.2..R-9.4: walk ktunList under ktunListLock, dev_get_stats(). */
	return 0;
}

/* R-9.1 */
int KtunProcInit(void)
{
	if (!proc_create_single(KTUN_PROC_NAME, 0444, NULL, KtunProcShow))
		return -ENOMEM;
	return 0;
}

void KtunProcExit(void)
{
	remove_proc_entry(KTUN_PROC_NAME, NULL);
}
