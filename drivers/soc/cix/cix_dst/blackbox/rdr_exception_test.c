// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright 2024 Cix Technology Group Co., Ltd.
 *
 */

#include <linux/soc/cix/rdr_platform.h>
#include "rdr_print.h"

static struct delayed_work test_wq;
static struct delayed_work test_ap_wq;

#ifdef TEST_RDR_FILE_MAX_SIZE
static int g_count;
#endif

static int module_test_rdr_dump(void *dump_addr, unsigned int size)
{
	struct rdr_exception_info_s *info = NULL;
	unsigned int bufsize = min_t(u32, size, SZ_4K);

	BB_ERR("addr:%px, size:0x%x\n", dump_addr, size);
	info = rdr_get_exce_info();
	if (!info) {
		BB_ERR("rdr_get_exce_info failed!\n");
		return -1;
	}
	BB_PN("modid = 0x%x\n", info->e_modid);

	memset(dump_addr, 0x5A, bufsize);

	return 0;
}

static int module_dump_test_init(void)
{
	BB_PR_START();
	(void)register_module_dump_mem_func(module_test_rdr_dump, "test",
					    MODU_TEST);
	BB_PR_END();
	return 0;
}

static void test_exception_work(struct work_struct *work)
{
	BB_PN("rdr test exception triggered...\n");
	rdr_system_error(MODID_AP_S_TEST, 0, 0);

#ifdef TEST_RDR_FILE_MAX_SIZE
	if (g_count++ < 7)
		schedule_delayed_work(&test_wq, msecs_to_jiffies(10000));
#endif
}

static void test_exception_ap_work(struct work_struct *work)
{
	BB_PN("rdr test ap exception triggered...\n");
	rdr_system_error(MODID_AP_S_PANIC, 0, 0);
}

static int __init rdr_exception_test(void)
{
	INIT_DELAYED_WORK(&test_wq, test_exception_work);
	INIT_DELAYED_WORK(&test_ap_wq, test_exception_ap_work);

	module_dump_test_init();

	schedule_delayed_work(&test_wq, msecs_to_jiffies(20000));

#ifndef TEST_RDR_FILE_MAX_SIZE
	schedule_delayed_work(&test_ap_wq, msecs_to_jiffies(100000));
#endif
	return 0;
}

module_init(rdr_exception_test);
MODULE_LICENSE("GPL");
