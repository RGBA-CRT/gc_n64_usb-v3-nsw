#include <stdio.h>
#include <string.h>
#include "requests.h"
#include "config.h"
#include "hiddata.h"
#include "bootloader.h"
#include "gcn64_protocol.h"

#define CMDBUF_SIZE	64

#define STATE_IDLE			0
#define STATE_NEW_COMMAND	1	// New command in buffer
#define STATE_COMMAND_DONE	2	// Result in buffer

//#define DEBUG

extern char g_polling_suspended;

static volatile uint8_t state = STATE_IDLE;
static unsigned char cmdbuf[CMDBUF_SIZE];
static volatile unsigned char cmdbuf_len = 0;

/*** Get/Set report called from interrupt context! */
uint16_t hiddata_get_report(struct usb_request *rq, const uint8_t **dat)
{
//	printf("Get data\n");
	if (state == STATE_COMMAND_DONE) {
		*dat = cmdbuf;
		state = STATE_IDLE;
#ifdef DEBUG
		printf("hiddata idle, sent %d bytes\r\n", cmdbuf_len);
#endif
		return cmdbuf_len;
	}
	return 0;
}

/*** Get/Set report called from interrupt context! */
uint8_t hiddata_set_report(const struct usb_request *rq, const uint8_t *dat, uint16_t len)
{
	int i;

#ifdef DEBUG
	printf("Set data %d\n", len);
	for (i=0; i<len; i++) {
		printf("0x%02x ", dat[i]);
	}
	printf("\r\n");
#endif

	state = STATE_NEW_COMMAND;
	memcpy(cmdbuf, dat, len);
	cmdbuf_len = len;

	return 0;
}

static void hiddata_processCommandBuffer(void)
{
	int i;
	int bits;

	if (cmdbuf_len < 1) {
		state = STATE_IDLE;
		return;
	}

//	printf("Process cmd 0x%02x\r\n", cmdbuf[0]);
	switch(cmdbuf[0])
	{
		case RQ_GCN64_JUMP_TO_BOOTLOADER:
			enterBootLoader();
			break;
		case RQ_GCN64_RAW_SI_COMMAND:
			// TODO : Range checking
			// cmd : RQ, LEN, data[]
			bits = gcn64_transaction(cmdbuf+2, cmdbuf[1]);
			cmdbuf_len = bits / 8; // The above return a number of bits
			gcn64_protocol_getBytes(0, cmdbuf_len, cmdbuf + 2);
			cmdbuf_len += 2; // Answer: RQ, LEN, data[]
			break;
		case RQ_GCN64_GET_CONFIG_PARAM:
			// Cmd : RQ, PARAM
			// Answer: RQ, PARAM, data[]
			cmdbuf_len = config_getParam(cmdbuf[1], cmdbuf + 2, CMDBUF_SIZE-2);
			cmdbuf_len += 2; // Datalen + RQ + PARAM
			break;
		case RQ_GCN64_SET_CONFIG_PARAM:
			// Cmd: RQ, PARAM, data[]
			config_setParam(cmdbuf[1], cmdbuf+2);
			// Answer: RQ, PARAM
			cmdbuf_len = 2;
			break;
		case RQ_GCN64_SUSPEND_POLLING:
			g_polling_suspended = 1;
			break;
	}

#ifdef DEBUG
	printf("Pending data %d\n", cmdbuf_len);
	for (i=0; i<cmdbuf_len; i++) {
		printf("0x%02x ", cmdbuf[i]);
	}
	printf("\r\n");
#endif

	state = STATE_COMMAND_DONE;
}

void hiddata_doTask(void)
{
	switch (state)
	{
		default:
			state = STATE_IDLE;
		case STATE_IDLE:
			break;

		case STATE_NEW_COMMAND:
			hiddata_processCommandBuffer();
			break;

		case STATE_COMMAND_DONE:
			break;
	}
}