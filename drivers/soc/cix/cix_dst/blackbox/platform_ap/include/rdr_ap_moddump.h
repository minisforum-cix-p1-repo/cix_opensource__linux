/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef __RDR_AP_MODDUMP_H__
#define __RDR_AP_MODDUMP_H__

#include <linux/platform_device.h>
#include <linux/soc/cix/rdr_platform.h>

#define AMNTN_MODULE_NAME_LEN 12

struct module_dump_mem_info {
	ap_dump_func dump_funcptr;
	unsigned char *dump_addr;
	unsigned int dump_size;
	char module_name[AMNTN_MODULE_NAME_LEN];
};

void save_module_dump_mem(void);
int module_dump_init(struct platform_device *pdev,
		     struct module_dump_mem_info *info, void *addr);
void moddump_debug_info(struct module_dump_mem_info *info);

#endif
