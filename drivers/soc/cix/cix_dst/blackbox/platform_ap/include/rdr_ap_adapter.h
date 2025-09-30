/* SPDX-License-Identifier: GPL-2.0-only */
// Copyright 2025 Cix Technology Group Co., Ltd.
/*
 * rdr_hisi_ap_adapter.h
 *
 * Based on the RDR framework, adapt to the AP side to implement resource
 *
 * Copyright (c) 2012-2019 Huawei Technologies Co., Ltd.
 * Copyright 2024 Cix Technology Group Co., Ltd.
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 */
#ifndef __RDR_AP_ADAPTER_H__
#define __RDR_AP_ADAPTER_H__

#include <linux/thread_info.h>
#include <linux/pstore.h>
#include <linux/soc/cix/rdr_platform.h>
#include <linux/soc/cix/rdr_platform_ap_hook.h>
#include <linux/platform_device.h>
#include "rdr_ap_hook.h"
#include "rdr_ap_moddump.h"
#include "rdr_ap_regdump.h"
#include "rdr_ap_logbuf.h"
#include "rdr_ap_stack.h"
#include "rdr_ap_suspend.h"

#define PRODUCT_VERSION_LEN 32
#define PRODUCT_DEVICE_LEN 32
#define AP_DUMP_MAGIC 0x19283746
#define BBOX_VERSION 0x1001B /* v1.0.11 */
#define AP_DUMP_END_MAGIC 0x1F2E3D4C
#define SIZE_1K 0x400
#define MAX_HOOK_CPU 20
#define PROPERTY_INIT(name) { #name, 0 }
#define STRUCT_PRINT(struct, name, format) \
	rdr_cleartext_print(fp, &error, #name "[" format "]\n", ap_root->name);
#define GET_ADDR_FROM_EHROOT(ehroot, addr) \
	(((void *)ehroot) + ((addr) - (ehroot->mem.vaddr)))
#ifndef MAX
#define MAX(X, Y) ((X) > (Y) ? (X) : (Y))
#endif
#ifndef MIN
#define MIN(X, Y) ((X) < (Y) ? (X) : (Y))
#endif

struct property_table {
	const char *prop_name;
	unsigned int size;
};

struct ap_mem_info {
	u64 paddr;
	void *vaddr;
	u32 size;
};

struct ap_eh_root {
	unsigned int dump_magic;
	unsigned char version[PRODUCT_VERSION_LEN];
	struct ap_mem_info mem;
	/* Reentrant count,The initial value is 0,Each entry++ */
	unsigned int enter_times;
	u64 slice;
	/*reg dump*/
	struct regs_dump regdump;
	/*hook*/
	struct hook_buffer_info hookbuf[HK_MAX];
	/*mod dump*/
	struct module_dump_mem_info module_dump_info[MODU_MAX];
	/*pstore dump*/
	struct pstore_mem pstore_buf[PSTORE_TYPE_MAX];
	/*task stack*/
	struct ap_mem_info stack;
	/*suspend info*/
	struct ap_mem_info suspend_info;
	unsigned char device_id[PRODUCT_DEVICE_LEN];
	/* Indicates the BBox version */
	u64 bbox_version;
	/* End of the tag structure. It is used to determine the scope of the structure */
	unsigned int end_magic;
	char reserved[1];
}; /* The ap_eh_root occupies 2K space and is reserved by using the get_rdr_hisiap_dump_addr function */

int ap_prop_table_init(struct device *dev, struct property_table *table,
		       u32 table_size);
int used_mem_update(void *addr);
void ap_exception_callback(u32 argc, void *argv);
int rdr_exception_init(void);
#endif
