// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright 2024 Cix Technology Group Co., Ltd.
 *
 */

#include <linux/delay.h>
#include <linux/syscalls.h>
#include "rdr_print.h"
#include "rdr_inner.h"

int rdr_wait_partition(const char *path, int timeouts)
{
	struct kstat m_stat;
	int timeo;

	BB_PR_START();
	if (path == NULL) {
		BB_ERR("invalid  parameter path\n");
		BB_PR_END();
		return -1;
	}

	for (;;) {
		if (rdr_get_suspend_state()) {
			BB_PN("wait for suspend\n");
			msleep(WAIT_TIME);
		} else if (rdr_get_reboot_state()) {
			BB_PN("wait for reboot\n");
			msleep(WAIT_TIME);
		} else {
			break;
		}
	}

	timeo = timeouts;

	while (rdr_vfs_stat(path, &m_stat) != 0) {
		set_current_state(TASK_INTERRUPTIBLE);
		(void)schedule_timeout(HZ / 10); /* wait for 1/10 second */
		BB_DBG("path=%s\n", path);
		if (timeouts-- < 0) {
			BB_ERR("wait partiton[%s] fail. use [%d]'s . skip!\n",
			       path, timeo);
			BB_PR_END();
			return -1;
		}
	}

	BB_PR_END();
	return 0;
}

void rdr_sys_sync(void)
{
	if (!in_atomic() && !irqs_disabled() && !in_irq())
		/* Ensure all previous file system related operations can be completed */
		ksys_sync();
}
