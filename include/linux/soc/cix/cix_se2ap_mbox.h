#ifndef __CIX_AP2SE_MBOX_H__
#define __CIX_AP2SE_MBOX_H__

#include <linux/types.h>

#define CIX_MBOX_MSG_LEN (32)
#define MBOX_HEADER_NUM (2)
#define MBOX_HEADER_SIZE (sizeof(uint32_t) * MBOX_HEADER_NUM)

struct mbox_msg_t {
	uint32_t size : 7;
	uint32_t type : 3;
	uint32_t reserve1 : 22;
	uint32_t cmd_id;
	uint32_t data[CIX_MBOX_MSG_LEN - MBOX_HEADER_NUM];
};

typedef void (*mbox_rx_callback_t)(char *inbuf, size_t len);

int cix_se2ap_register_rx_cbk(uint32_t cmd_id, mbox_rx_callback_t cbk);
int cix_se2ap_mbox_send(uint32_t cmd_id, char *data, size_t len, bool need_reply);

#endif