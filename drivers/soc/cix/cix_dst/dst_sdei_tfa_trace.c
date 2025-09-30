// SPDX-License-Identifier: GPL-2.0
/* Copyright 2025 Cix Technology Group Co., Ltd.*/

#include <linux/arm_sdei.h>
#include <linux/cacheflush.h>
#include <linux/soc/cix/rdr_platform.h>
#include "dst_print.h"

static int tfa_trace_dump(void *dump_addr, unsigned int size)
{
	/*invalid cache*/
	dcache_inval_poc((unsigned long)dump_addr,
			 (unsigned long)((char *)dump_addr + size));

	return 0;
}

static int __init dst_tfa_trace_init(void)
{
	unsigned char *virt_addr;
	uintptr_t phy_addr;
	u32 size, base;
	u64 os_size = 0;

	DST_PR_START();
	if (get_module_dump_mem_addr(MODU_TFA, &virt_addr, &size)) {
		DST_ERR("get module memory failed.\n");
		return 0;
	}

	phy_addr = (vmalloc_to_pfn(virt_addr) << PAGE_SHIFT) +
		   ((u64)virt_addr & ((1 << PAGE_SHIFT) - 1));
	DST_PN("phys memory address=0x%lx, size=0x%x\n", phy_addr, size);

	if (sdei_api_event_set_info(size, SDEI_EVENT_SET_TFA_TRACE_MEMORY,
				    (u64)phy_addr)) {
		DST_ERR("set sdei tfa trace memory failed.\n");
		return 0;
	}
	register_module_dump_mem_func(tfa_trace_dump, "tfa", MODU_TFA);

#ifdef CONFIG_PLAT_KERNELDUMP
	/*set tfa flush cache size*/
	base = memblock_start_of_DRAM();
	os_size = memblock_end_of_DRAM() - base;
	if (sdei_api_event_set_info(base, SDEI_EVENT_SET_OS_MEM_SIZE, os_size))
		DST_ERR("set os memory failed.\n");
#endif

	DST_PR_END();

	return 0;
}

late_initcall(dst_tfa_trace_init);
