// SPDX-License-Identifier: GPL-2.0-only
/*
 * Stack tracing support
 *
 * Copyright (C) 2025 CIX Ltd.
 */
#include "include/rdr_ap_adapter.h"
#include "include/rdr_ap_moddump.h"
#include "../rdr_print.h"

#define MODMEM_PROP_INIT(name) PROPERTY_INIT(ap_dump_mem_modu_##name##_size)

static struct module_dump_mem_info *g_mod_meminfo;
static struct mutex g_modmem_mutex;
/*
 * The following table lists the dump memory used by other maintenance
 * and test modules and IP addresses of the AP.
 */
static struct property_table g_modmem_prop[MODU_MAX] = {
#ifdef CONFIG_PLAT_BBOX_TEST
	[MODU_TEST] = MODMEM_PROP_INIT(test),
#endif
	[MODU_NOC] = { NULL, 0 },
	[MODU_DDR] = { NULL, 0 },
	[MODU_TZC400] = MODMEM_PROP_INIT(tzc400),
	[MODU_IDM] = MODMEM_PROP_INIT(idm),
	[MODU_SMMU] = MODMEM_PROP_INIT(smmu),
	[MODU_TFA] = MODMEM_PROP_INIT(tfa),
	[MODU_GAP] = MODMEM_PROP_INIT(gap),
};

int module_dump_init(struct platform_device *pdev,
		     struct module_dump_mem_info *info, void *addr)
{
	int i, ret;
	void *mod_addr = addr;

	g_mod_meminfo = info;
	mutex_init(&g_modmem_mutex);
	ret = ap_prop_table_init(&pdev->dev, g_modmem_prop,
				 ARRAY_SIZE(g_modmem_prop));
	if (ret) {
		BB_ERR("g_modmem_prop init failed!\n");
		return ret;
	}

	BB_PR_START();
	for (i = 0; i < MODU_MAX; i++) {
		info[i].dump_addr = mod_addr;
		mod_addr += g_modmem_prop[i].size;
		if (used_mem_update(mod_addr)) {
			BB_ERR("there is no enough space for modu [%d] to dump mem!\n",
			       i);
			break;
		}

		info[i].dump_size = g_modmem_prop[i].size;
		BB_DBG("dump_addr [0x%px] dump_size [0x%x]!\n",
		       info[i].dump_addr, info[i].dump_size);
	}
	BB_PR_END();
	return 0;
}

/*
 * Description:  Obtains the dump start address of the dump module
 * Input:        modu:Module ID,This is a unified allocation;
 * Output:       dump_addr:Start address of the dump memory allocated to the module MODU
 * Return:       0:The data is successfully obtained ,Smaller than 0:Obtaining failed
 */
int get_module_dump_mem_addr(dump_mem_module modu, unsigned char **dump_addr,
			     u32 *size)
{
	if (!rdr_get_ap_init_done()) {
		BB_ERR("rdr not init\n");
		return -EPERM;
	}

	if (modu >= MODU_MAX) {
		BB_ERR("modu [%u] is invalid\n", modu);
		return -EINVAL;
	}

	if (!dump_addr) {
		BB_ERR("dump_addr is invalid\n");
		return -EINVAL;
	}

	if (g_mod_meminfo[modu].dump_size == 0) {
		BB_ERR("modu[%u] dump_size is zero\n", modu);
		return -EPERM;
	}

	*dump_addr = g_mod_meminfo[modu].dump_addr;
	if (!(*dump_addr)) {
		BB_ERR("*dump_addr is invalid\n");
		return -EINVAL;
	}
	*size = g_mod_meminfo[modu].dump_size;

	return 0;
}

/*
 * Description:    Memory dump registration interface provided for the AP maintenance
 *                 and test module and IP address
 * Input:          func:Registered dump function, module_name, mod
 * Output:         NA
 * Return:         0:Registration succeeded, <0:fail
 */
int register_module_dump_mem_func(ap_dump_func func, const char *module_name,
				  dump_mem_module modu)
{
	int ret = -1;

	if (modu >= MODU_MAX) {
		BB_ERR("modu [%u] is invalid!\n", modu);
		return -EINVAL;
	}

	if (IS_ERR_OR_NULL(func)) {
		BB_ERR("func [0x%px] is invalid!\n", func);
		return -EINVAL;
	}

	if (IS_ERR_OR_NULL(module_name)) {
		BB_ERR("module_name is invalid!\n");
		return -EINVAL;
	}

	if (!g_mod_meminfo) {
		BB_ERR("g_rdr_ap_root is null!\n");
		return -1;
	}

	BB_PN("module_name [%s]\n", module_name);
	mutex_lock(&g_modmem_mutex);
	if (g_mod_meminfo[modu].dump_size != 0) {
		g_mod_meminfo[modu].dump_funcptr = func;
		strncpy(g_mod_meminfo[modu].module_name, module_name,
			AMNTN_MODULE_NAME_LEN - 1);
		g_mod_meminfo[modu].module_name[AMNTN_MODULE_NAME_LEN - 1] =
			'\0';
		ret = 0;
	}
	mutex_unlock(&g_modmem_mutex);

	if (ret)
		BB_ERR("func[0x%px], size[%u], [%s] register failed!\n", func,
		       g_mod_meminfo[modu].dump_size, module_name);
	return ret;
}

/*
 * Description:   Before the abnormal reset, the AP maintenance and test
 *                module and the memory dump registration function provided
 *                by the IP are invoked
 */
void save_module_dump_mem(void)
{
	int i;
	void *addr = NULL;
	unsigned int size = 0;

	BB_PR_START();
	for (i = 0; i < MODU_MAX; i++) {
		if (g_mod_meminfo[i].dump_funcptr != NULL) {
			addr = (void *)g_mod_meminfo[i].dump_addr;
			size = g_mod_meminfo[i].dump_size;
			if (g_mod_meminfo[i].dump_funcptr(addr, size))
				BB_ERR("[%s] dump failed!\n",
				       g_mod_meminfo[i].module_name);
		}
	}
	BB_PR_END();
}

void moddump_debug_info(struct module_dump_mem_info *info)
{
	for (int i = 0; i < MODU_MAX; i++) {
		if (info[i].dump_size != 0)
			BB_DBG("info[%u].dump_addr [0x%px]\n", i,
			      info[i].dump_addr);
	}
}