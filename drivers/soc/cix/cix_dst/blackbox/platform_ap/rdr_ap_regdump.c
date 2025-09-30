// SPDX-License-Identifier: GPL-2.0-only
/*
 * Stack tracing support
 *
 * Copyright (C) 2025 CIX Ltd.
 */
#include <linux/platform_device.h>
#include <linux/property.h>
#include "include/rdr_ap_adapter.h"
#include "../rdr_print.h"

int regsdump_init(struct platform_device *pdev, struct regs_dump *dump,
		  void *addr)
{
	int ret;
	unsigned int i;
	struct resource *res;
	struct regs_info *regs = NULL;
	void *regdump_addr = NULL;

	BB_PR_START();
	regs = dump->info;
	memset((void *)regs, 0, REGS_DUMP_MAX_NUM * sizeof(*regs));
	ret = device_property_read_u32(&pdev->dev, "reg-dump-regions",
				       &dump->num);
	if (ret) {
		BB_PN("cannot find reg-dump-regions in dts!\n");
		goto ioinit_fail;
	}

	if (dump->num == 0) {
		BB_ERR("reg-dump-regions in zero, so no reg resource to init\n");
		goto ioinit_fail;
	}

	regdump_addr = addr;
	for (i = 0; i < dump->num; i++) {
		res = platform_get_mem_or_io(pdev, i);
		if (IS_ERR_OR_NULL(res)) {
			BB_ERR("get regs[%u] fail!\n", i);
			goto ioinit_fail;
		}

		strncpy(regs[i].name, res->name, REG_NAME_LEN - 1);
		regs[i].name[REG_NAME_LEN - 1] = '\0';
		regs[i].paddr = res->start;
		regs[i].size = resource_size(res);

		if (regs[i].size == 0) {
			BB_ERR("[%s] registers size is 0, skip map!\n",
			       (regs[i].name));
			goto reg_dump_addr_init;
		}

		regs[i].map_addr = devm_platform_ioremap_resource(pdev, i);
		BB_DBG("regs[%u]: name[%s], base[0x%px], size[0x%x], map_addr[0x%px]\n",
		       i, regs[i].name, (void *)(uintptr_t)regs[i].paddr,
		       regs[i].size, regs[i].map_addr);

		if (IS_ERR_OR_NULL(regs[i].map_addr)) {
			BB_ERR("unable to map [%s] registers\n",
			       (regs[i].name));
			goto ioinit_fail;
		}
		BB_DBG("map [%s] registers ok\n", (regs[i].name));

reg_dump_addr_init:
		regs[i].dump_addr = regdump_addr;
		regdump_addr += regs[i].size;
		if (used_mem_update(regdump_addr)) {
			return -1;
		}
	}

ioinit_fail:
	BB_PR_END();
	return 0;
}

void regs_dump(struct regs_dump *dump)
{
	unsigned int i;
	struct regs_info *info = NULL;

	info = dump->info;

	/*
	 * NOTE:sctrl in the power-on area, pctrl, pctrl, pericrg in the peripheral area,
	 * Do not check the domain when accessing the A core
	 */
	for (i = 0; i < dump->num; i++) {
		if (IS_ERR_OR_NULL(info[i].map_addr) ||
		    IS_ERR_OR_NULL(info[i].dump_addr)) {
			info[i].dump_addr = 0;
			BB_ERR("regs_info[%u].reg_map_addr [%px] reg_dump_addr [%px] invalid!\n",
			       i, info[i].map_addr, info[i].dump_addr);
			continue;
		}
		BB_PN("memcpy [0x%x] size from regs_info[%u].reg_map_addr [%px] to reg_dump_addr [%px]\n",
		      info[i].size, i, info[i].map_addr, info[i].dump_addr);
		memcpy(info[i].dump_addr, info[i].map_addr, info[i].size);
	}
}

void regsdump_debug_info(struct regs_dump *dump)
{
	struct regs_info *regs = NULL;

	regs = dump->info;
	BB_PN("num [0x%x]\n", dump->num);
	for (int i = 0; i < dump->num; i++)
		BB_PN("name [%s], paddr [0x%px], size [0x%x], dump_addr [0x%px]\n",
		      regs[i].name, (void *)(uintptr_t)regs[i].paddr,
		      regs[i].size, regs[i].dump_addr);
}

unsigned int get_total_regdump_size(struct regs_dump *dump)
{
	unsigned int i;
	u32 size = 0;
	struct regs_info *regs = dump->info;

	if (!regs) {
		BB_ERR("info is null\n");
		return 0;
	}

	for (i = 0; i < dump->num; i++)
		size += regs[i].size;

	BB_DBG("num [%u], total size [0x%x]\n", dump->num, size);
	return size;
}
