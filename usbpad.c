/*	gc_n64_usb : Gamecube or N64 controller to USB firmware
	Copyright (C) 2007-2016  Raphael Assenat <raph@raphnet.net>

	This program is free software: you can redistribute it and/or modify
	it under the terms of the GNU General Public License as published by
	the Free Software Foundation, either version 3 of the License, or
	(at your option) any later version.

	This program is distributed in the hope that it will be useful,
	but WITHOUT ANY WARRANTY; without even the implied warranty of
	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
	GNU General Public License for more details.

	You should have received a copy of the GNU General Public License
	along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <avr/interrupt.h>
#include "usb.h"
#include "gamepads.h"
#include "usbpad.h"
#include "mappings.h"
#include "eeprom.h"
#include "config.h"
#include "hid_keycodes.h"
#include "gc_kb.h"

#define STICK_TO_BTN_THRESHOLD	40

#define REPORT_ID	1

// Output Report IDs for various functions
#define REPORT_SET_EFFECT			0x01
#define REPORT_SET_STATUS			0x02
#define	REPORT_SET_PERIODIC			0x04
#define REPORT_SET_CONSTANT_FORCE	0x05
#define REPORT_EFFECT_OPERATION		0x0A
#define REPORT_EFFECT_BLOCK_IDX		0x0B
#define REPORT_DISABLE_ACTUATORS	0x0C
#define REPORT_PID_POOL				0x0D

// Feature reports
#define REPORT_CREATE_EFFECT		0x09

// For the 'Usage Effect Operation' report
#define EFFECT_OP_START			1
#define EFFECT_OP_START_SOLO	2
#define EFFECT_OP_STOP			3

// Feature report
#define PID_SIMULTANEOUS_MAX	3
#define PID_BLOCK_LOAD_REPORT	2

#undef DEBUG

#ifdef DEBUG
static void hexdump(const unsigned char *ptr, int len)
{
	int i;

	for (i=0; i<len; i++) {
		printf_P(PSTR("%02x "), ptr[i]);
	}
	printf_P(PSTR("\r\n"));
}
#else
// #define printf_P(...)
#define hexdump(...)
#endif

static void buildIdleReport(unsigned char dstbuf[USBPAD_REPORT_SIZE]);
static uint8_t s_nsw_mode;

void usbpad_init(struct usbpad *pad, uint8_t nsw_mode)
{
	memset(pad, 0, sizeof(struct usbpad));
	buildIdleReport(pad->gamepad_report0);
	s_nsw_mode = nsw_mode;
}

int usbpad_getReportSize(void)
{
	if(s_nsw_mode){
		return 8;
	}else{
		return USBPAD_REPORT_SIZE;
	}
}

static int16_t minmax(int16_t input, int16_t min, int16_t max)
{
	if (input > max)
		return max;
	if (input < min)
		return min;

	return input;
}

static void btnsToReport(unsigned short buttons, unsigned char dstbuf[2])
{
	dstbuf[0] = buttons & 0xff;
	dstbuf[1] = buttons >> 8;
}

static uint8_t N64DpadToNswHat(uint8_t dpad){
	enum NSW_HAT{
		NSW_HAT_UP = 0,
		NSW_HAT_UP_RIGHT,
		NSW_HAT_RIGHT,
		NSW_HAT_DOWN_RIGHT,
		NSW_HAT_DOWN,
		NSW_HAT_DOWN_LEFT,
		NSW_HAT_LEFT,
		NSW_HAT_UP_LEFT,
		NSW_HAT_NUTRAL,
	};
	const static uint8_t dpad_hat_table[] PROGMEM = {
		/* 0x0 */ NSW_HAT_NUTRAL,
		/* 0x1 */ NSW_HAT_RIGHT,
		/* 0x2 */ NSW_HAT_LEFT,
		/* 0x3 */ NSW_HAT_NUTRAL, // LEFT&RIGHT
		/* 0x4 */ NSW_HAT_DOWN,
		/* 0x5 */ NSW_HAT_DOWN_RIGHT,
		/* 0x6 */ NSW_HAT_DOWN_LEFT,
		/* 0x7 */ NSW_HAT_NUTRAL, //LEFT&RIGHT&DOWN

		/* 0x8 */ NSW_HAT_UP,
		/* 0x9 */ NSW_HAT_UP_RIGHT,
		/* 0xA */ NSW_HAT_UP_LEFT,
		/* 0xB */ NSW_HAT_NUTRAL, // UP&LEFT&RIGHT
		/* 0xC */ NSW_HAT_NUTRAL, // UP&DOWN
		/* 0xD */ NSW_HAT_NUTRAL, // UP&DOWN&RIGHT
		/* 0xE */ NSW_HAT_NUTRAL, // UP&DOWN&LEFT
		/* 0xF */ NSW_HAT_NUTRAL, // UP&DOWN&RIGHT&LIGHT	
	};

	return pgm_read_byte(&dpad_hat_table[dpad]); // hat
}

static void buildIdleReport(unsigned char dstbuf[USBPAD_REPORT_SIZE])
{
	int i;

	if(!s_nsw_mode){
		dstbuf[0] = REPORT_ID;

		/* Inactive and centered axis */
		for (i=0; i<6; i++) {
			dstbuf[1+i*2] = 0x80;
			dstbuf[2+i*2] = 0x3e;
		}

		/* Inactive buttons */
		dstbuf[13] = 0;
		dstbuf[14] = 0;
	}
}

int usbpad_getReportSizeKB(void)
{
	return 3;
}

static void buildIdleReportKB(unsigned char dstbuf[USBPAD_REPORT_SIZE])
{
	dstbuf[0] = HID_KB_NOEVENT;
	dstbuf[1] = HID_KB_NOEVENT;
	dstbuf[2] = HID_KB_NOEVENT;
}

static void buildReportFromGC(const gc_pad_data *gc_data, unsigned char dstbuf[USBPAD_REPORT_SIZE])
{
	int16_t xval,yval,cxval,cyval,ltrig,rtrig;
	uint16_t buttons;
	uint16_t gcbuttons = gc_data->buttons;

	/* Force official range */
	xval = minmax(gc_data->x, -100, 100);
	yval = minmax(gc_data->y, -100, 100);
	cxval = minmax(gc_data->cx, -100, 100);
	cyval = minmax(gc_data->cy, -100, 100);
	ltrig = gc_data->lt;
	rtrig = gc_data->rt;

	if (g_eeprom_data.cfg.flags & FLAG_SWAP_STICK_AND_DPAD) {

		// Generate new D-Pad button status based on stick
		gcbuttons &= ~(GC_BTN_DPAD_UP|GC_BTN_DPAD_DOWN|GC_BTN_DPAD_LEFT|GC_BTN_DPAD_RIGHT);
		if (xval <= -STICK_TO_BTN_THRESHOLD) { gcbuttons |= GC_BTN_DPAD_LEFT; }
		if (xval >= STICK_TO_BTN_THRESHOLD) { gcbuttons |= GC_BTN_DPAD_RIGHT; }
		if (yval <= -STICK_TO_BTN_THRESHOLD) { gcbuttons |= GC_BTN_DPAD_DOWN; }
		if (yval >= STICK_TO_BTN_THRESHOLD) { gcbuttons |= GC_BTN_DPAD_UP; }

		// Generate new stick values based on button (use gc_data here)
		xval = 0; yval = 0;
		if (gc_data->buttons & GC_BTN_DPAD_UP) { yval = 100; }
		if (gc_data->buttons & GC_BTN_DPAD_DOWN) { yval = -100; }
		if (gc_data->buttons & GC_BTN_DPAD_LEFT) { xval = -100; }
		if (gc_data->buttons & GC_BTN_DPAD_RIGHT) { xval = 100; }
	}


	/* Scale -100 ... + 1000 to -16000 ... +16000 */
	xval *= 160;
	yval *= -160;
	// TODO : Is C-stick different?
	cxval *= 160;
	cyval *= -160;

	if (g_eeprom_data.cfg.flags & FLAG_GC_SLIDERS_AS_BUTTONS) {
		/* In this mode, the sliders control buttons */
		if (ltrig > 64)
			gcbuttons |= GC_BTN_L;
		if (rtrig > 64)
			gcbuttons |= GC_BTN_R;

		/* And the sliders analog values are fixed. */
		ltrig = rtrig = 0;
	}
	else {
		if (g_eeprom_data.cfg.flags & FLAG_GC_FULL_SLIDERS) {
			int16_t lts = (int16_t)ltrig - 127;
			int16_t rts = (int16_t)rtrig - 127;
			lts *= 126;
			ltrig = lts;
			rts *= 126;
			rtrig = rts;

		} else {
			/* Scale 0...255 to 0...16000 */
			ltrig *= 63;
			if (ltrig > 16000) ltrig=16000;
			rtrig *= 63;
			if (rtrig > 16000) rtrig=16000;
		}

		if (g_eeprom_data.cfg.flags & FLAG_GC_INVERT_TRIGS) {
			ltrig = -ltrig;
			rtrig = -rtrig;
		}
	}

	if (g_eeprom_data.cfg.flags & FLAG_DISABLE_ANALOG_TRIGGERS) {
		ltrig = rtrig = 0;
	}

	/* Unsign for HID report */
	xval += 16000;
	yval += 16000;
	cxval += 16000;
	cyval += 16000;
	ltrig += 16000;
	rtrig += 16000;

	dstbuf[1] = ((uint8_t*)&xval)[0];
	dstbuf[2] = ((uint8_t*)&xval)[1];
	dstbuf[3] = ((uint8_t*)&yval)[0];
	dstbuf[4] = ((uint8_t*)&yval)[1];

	dstbuf[5] = ((uint8_t*)&cxval)[0];
	dstbuf[6] = ((uint8_t*)&cxval)[1];
	dstbuf[7] = ((uint8_t*)&cyval)[0];
	dstbuf[8] = ((uint8_t*)&cyval)[1];

	dstbuf[9] = ((uint8_t*)&ltrig)[0];
	dstbuf[10] = ((uint8_t*)&ltrig)[1];

	dstbuf[11] = ((uint8_t*)&rtrig)[0];
	dstbuf[12] = ((uint8_t*)&rtrig)[1];

	buttons = mappings_do(MAPPING_GAMECUBE_DEFAULT, gcbuttons);
	btnsToReport(buttons, dstbuf+13);
}

#define GC_ANALOG_SAFE_AREA_THRESHOLD 8
static uint8_t buildAnalogValueGc2NswHid(int8_t analog){
	int16_t aval = analog;
	
	// 0~100 * 1.5 = 150
	aval+=aval>>1;

	// safe area
	if(abs(aval) < GC_ANALOG_SAFE_AREA_THRESHOLD) aval = 0;	

	// signed to unsigned
	aval += 128;

	// clamp uint8_t
	aval = minmax(aval, 0, 255);

	return (uint8_t)aval;
}

static void buildReportFromGC_NSW(const gc_pad_data *gc_data, unsigned char dstbuf[USBPAD_REPORT_SIZE])
{
	uint16_t buttons;
	uint16_t gcbuttons = gc_data->buttons;
	
	if(gcbuttons & GC_BTN_Z){
		buttons = mappings_do(MAPPING_GAMECUBE_NSW_L2, gcbuttons);
	}else{
		buttons = mappings_do(MAPPING_GAMECUBE_NSW, gcbuttons);
	}

	if(!(gcbuttons & GC_BTN_Z)){
		int8_t ltrig = gc_data->lt;
		int8_t rtrig = gc_data->rt;
		if ((ltrig > 64) && (ltrig < 190)){
			buttons |= NSW_BTN_L;
		}
		if ((rtrig > 64) && (rtrig < 190)){
			buttons |= NSW_BTN_R;
		}
	}

	btnsToReport(buttons, dstbuf);
	
	uint8_t dpad_bits=((gcbuttons>>8) & 0xC)
					  | ((gcbuttons & GC_BTN_DPAD_LEFT) ? 0x02 : 0)
					  | ((gcbuttons & GC_BTN_DPAD_RIGHT) ? 0x01 : 0);
	dstbuf[2] = N64DpadToNswHat(dpad_bits); // hat

	dstbuf[3] = buildAnalogValueGc2NswHid(gc_data->x);
	dstbuf[4] = buildAnalogValueGc2NswHid(-gc_data->y);
	dstbuf[5] = buildAnalogValueGc2NswHid(gc_data->cx);
	dstbuf[6] = buildAnalogValueGc2NswHid(-gc_data->cy);
	dstbuf[7] = 0x00; // dummy

	printf("%4d %4d %4d %4d %4d %4d| %4d %4d %4d %4d\r\n",
		gc_data->x, gc_data->y, gc_data->cx, gc_data->cy, gc_data->lt, gc_data->rt,
		dstbuf[3], dstbuf[4], dstbuf[5],dstbuf[6]);
}


static void buildReportFromN64(const n64_pad_data *n64_data, unsigned char dstbuf[USBPAD_REPORT_SIZE])
{
	int16_t xval, yval;
	uint16_t usb_buttons, n64_buttons = n64_data->buttons;

	/* Force official range */
	xval = minmax(n64_data->x, -80, 80);
	yval = minmax(n64_data->y, -80, 80);

	if (g_eeprom_data.cfg.flags & FLAG_SWAP_STICK_AND_DPAD) {

		// Generate new D-Pad button status based on stick
		n64_buttons &= ~(N64_BTN_DPAD_UP|N64_BTN_DPAD_DOWN|N64_BTN_DPAD_LEFT|N64_BTN_DPAD_RIGHT);
		if (xval <= -STICK_TO_BTN_THRESHOLD) { n64_buttons |= N64_BTN_DPAD_LEFT; }
		if (xval >= STICK_TO_BTN_THRESHOLD) { n64_buttons |= N64_BTN_DPAD_RIGHT; }
		if (yval <= -STICK_TO_BTN_THRESHOLD) { n64_buttons |= N64_BTN_DPAD_DOWN; }
		if (yval >= STICK_TO_BTN_THRESHOLD) { n64_buttons |= N64_BTN_DPAD_UP; }

		// Generate new stick values based on button (use n64_data here)
		xval = 0; yval = 0;
		if (n64_data->buttons & N64_BTN_DPAD_UP) { yval = 80; }
		if (n64_data->buttons & N64_BTN_DPAD_DOWN) { yval = -80; }
		if (n64_data->buttons & N64_BTN_DPAD_LEFT) { xval = -80; }
		if (n64_data->buttons & N64_BTN_DPAD_RIGHT) { xval = 80; }
	}

	/* Scale -80 ... +80 to -16000 ... +16000 */
	xval *= 200;
	yval *= 200;
	yval = -yval;

	/* Unsign for HID report */
	xval += 16000;
	yval += 16000;

	dstbuf[1] = ((uint8_t*)&xval)[0];
	dstbuf[2] = ((uint8_t*)&xval)[1];
	dstbuf[3] = ((uint8_t*)&yval)[0];
	dstbuf[4] = ((uint8_t*)&yval)[1];

	usb_buttons = mappings_do(MAPPING_N64_DEFAULT, n64_buttons);
	btnsToReport(usb_buttons, dstbuf+13);
}

extern void led_test();
static void buildReportFromN64_NSW(const n64_pad_data *n64_data, unsigned char dstbuf[USBPAD_REPORT_SIZE])
{
	int16_t xval, yval;
	uint16_t usb_buttons, n64_buttons = n64_data->buttons;

	xval = n64_data->x << 1;
	yval = n64_data->y << 1;
	yval = -yval;

	/* convert unsigned */
	xval += 0x80;
	yval += 0x80;
	
	xval = minmax(xval, 0, 0xFF);
	yval = minmax(yval, 0, 0xFF);

	if(!(n64_buttons & N64_BTN_C_RIGHT)){
		dstbuf[3] = ((uint8_t)xval);
		dstbuf[4] = ((uint8_t)yval);
		dstbuf[5] = 0x80; // stick 2 x
		dstbuf[6] = 0x80; // stick 2 y
	}else{
		dstbuf[3] = 0x80; // stick 1 x
		dstbuf[4] = 0x80; // stick 1 y
		dstbuf[5] = ((uint8_t)xval);
		dstbuf[6] = ((uint8_t)yval);
	}
	if(!(n64_buttons & N64_BTN_C_UP)){
		usb_buttons = mappings_do(MAPPING_N64_NSW, n64_buttons);
	}else{
		usb_buttons = mappings_do(MAPPING_N64_NSW_L2, n64_buttons);
	}

	btnsToReport(usb_buttons, dstbuf);

	uint8_t dpad_bits=(n64_buttons>>8) & 0xF;
	dstbuf[2] = N64DpadToNswHat(dpad_bits);
	dstbuf[7] = 0x00; // dummy
	
	// printf_P(PSTR("n64_dpad_bits=%02x hat=%d %d %d\r\n"),dpad_bits,dstbuf[2], dpad_hat_table[dpad_bits]);
}

void usbpad_update(struct usbpad *pad, const gamepad_data *pad_data)
{
	/* Always start with an idle report. Specific report builders can just
	 * simply ignore unused parts */
	buildIdleReport(pad->gamepad_report0);

	if (pad_data)
	{
		switch (pad_data->pad_type)
		{
			case PAD_TYPE_N64:
				if(s_nsw_mode)
					buildReportFromN64_NSW(&pad_data->n64, pad->gamepad_report0);
				else
					buildReportFromN64(&pad_data->n64, pad->gamepad_report0);
				break;

			case PAD_TYPE_GAMECUBE:
				if(s_nsw_mode)
					buildReportFromGC_NSW(&pad_data->gc, pad->gamepad_report0);
				else
					buildReportFromGC(&pad_data->gc, pad->gamepad_report0);
				break;

			default:
				break;
		}
	}
}

void usbpad_update_kb(struct usbpad *pad, const gamepad_data *pad_data)
{
	unsigned char i;

	/* Always start with an idle report. Specific report builders can just
	 * simply ignore unused parts */
	buildIdleReportKB(pad->gamepad_report0);

	if (pad_data->pad_type == PAD_TYPE_GC_KB) {
		for (i=0; i<3; i++) {
			pad->gamepad_report0[i] = gcKeycodeToHID(pad_data->gckb.keys[i]);
		}
	}
}


void usbpad_forceVibrate(struct usbpad *pad, char force)
{
	pad->force_vibrate = force;
}

void usbpad_vibrationTask(struct usbpad *pad)
{
	uint8_t sreg;

	sreg = SREG;
	cli();
	if (pad->_loop_count) {
		pad->_loop_count--;
	}
	SREG = sreg;
}

char usbpad_mustVibrate(struct usbpad *pad)
{
	if (pad->force_vibrate) {
		return 1;
	}

	if (!pad->vibration_on) {
		pad->gamepad_vibrate = 0;
	} else {
		if (pad->_loop_count > 0) {
			if (pad->constant_force > 0x7f) {
				pad->gamepad_vibrate = 1;
			} else if (pad->periodic_magnitude > 0x7f) {
				pad->gamepad_vibrate = 1;
			} else {
				pad->gamepad_vibrate = 0;
			}
		} else {
			// Loop count = 0 -> Stop
			pad->gamepad_vibrate = 0;
		}
	}

	return pad->gamepad_vibrate;
}

unsigned char *usbpad_getReportBuffer(struct usbpad *pad)
{
	return pad->gamepad_report0;
}

uint16_t usbpad_hid_get_report(struct usbpad *pad, struct usb_request *rq, const uint8_t **dat)
{
	uint8_t report_id = (rq->wValue & 0xff);

	// USB HID 1.11 section 7.2.1 Get_Report
	// wValue high byte : report type
	// wValue low byte : report id
	// wIndex Interface
	switch (rq->wValue >> 8)
	{
		case HID_REPORT_TYPE_INPUT:
			{
				if (report_id == 1) { // Joystick
					// report_id = rq->wValue & 0xff
					// interface = rq->wIndex
					*dat = pad->gamepad_report0;
					printf_P(PSTR("Get joy report\r\n"));
					return USBPAD_REPORT_SIZE
				;
				} else if (report_id == 2) { // 2 : ES playing
					pad->hid_report_data[0] = report_id;
					pad->hid_report_data[1] = 0;
					pad->hid_report_data[2] = pad->_FFB_effect_index;
					printf_P(PSTR("ES playing\r\n"));
					*dat = pad->hid_report_data;
					return 3;
				} else {
					printf_P(PSTR("Get input report %d ??\r\n"), rq->wValue & 0xff);
				}
			}
			break;

		case HID_REPORT_TYPE_FEATURE:
			if (report_id == PID_BLOCK_LOAD_REPORT) {
				pad->hid_report_data[0] = report_id;
				pad->hid_report_data[1] = 0x1; // Effect block index
				pad->hid_report_data[2] = 0x1; // (1: success, 2: oom, 3: load error)
				pad->hid_report_data[3] = 10;
				pad->hid_report_data[4] = 10;
				printf_P(PSTR("block load\r\n"));
				*dat = pad->hid_report_data;
				return 5;
			}
			else if (report_id == PID_SIMULTANEOUS_MAX) {
				pad->hid_report_data[0] = report_id;
				// ROM Effect Block count
				pad->hid_report_data[1] = 0x1;
				pad->hid_report_data[2] = 0x1;
				// PID pool move report?
				pad->hid_report_data[3] = 0xff;
				pad->hid_report_data[4] = 1;
				printf_P(PSTR("simultaneous max\r\n"));
				*dat = pad->hid_report_data;
				return 5;
			}
			else if (report_id == REPORT_CREATE_EFFECT) {
				pad->hid_report_data[0] = report_id;
				pad->hid_report_data[1] = 1;
				printf_P(PSTR("create effect\r\n"));
				*dat = pad->hid_report_data;
				return 2;
			} else {
				printf_P(PSTR("Unknown feature %d\r\n"), rq->wValue & 0xff);
			}
			break;
	}

	printf_P(PSTR("Unhandled hid get report type=0x%02x, rq=0x%02x, wVal=0x%04x, wLen=0x%04x\r\n"), rq->bmRequestType, rq->bRequest, rq->wValue, rq->wLength);
	return 0;
}

uint8_t usbpad_hid_set_report(struct usbpad *pad, const struct usb_request *rq, const uint8_t *data, uint16_t len)
{
	if (len < 1) {
		printf_P(PSTR("shrt\n"));
		return -1;
	}

	if ((rq->wValue >> 8) == HID_REPORT_TYPE_OUTPUT) {

		switch(data[0])
		{
			case REPORT_SET_STATUS:
				printf_P(PSTR("eff. set stat 0x%02x 0x%02x\r\n"),data[1],data[2]);
				break;
			case REPORT_EFFECT_BLOCK_IDX:
				printf_P(PSTR("eff. blk. idx %d\r\n"), data[1]);
				break;
			case REPORT_DISABLE_ACTUATORS:
				printf_P(PSTR("disable actuators\r\n"));
				pad->periodic_magnitude = 0;
				pad->constant_force = 0;
				pad->vibration_on = 0;
				break;
			case REPORT_PID_POOL:
				printf_P(PSTR("pid pool\r\n"));
				break;
			case REPORT_SET_EFFECT:
				pad->_FFB_effect_index = data[1];
				pad->_FFB_effect_duration = data[3] | (data[4]<<8);
				printf_P(PSTR("set effect %d. duration: %u\r\n"), data[1], pad->_FFB_effect_duration);
				hexdump(data, len);
				break;
			case REPORT_SET_PERIODIC:
				pad->periodic_magnitude = data[2];
				printf_P(PSTR("Set periodic - mag: %d, period: %u\r\n"), data[2], data[5] | (data[6]<<8));
				hexdump(data, len);
				break;
			case REPORT_SET_CONSTANT_FORCE:
				if (data[1] == 1) {
					pad->constant_force = data[2];
					printf_P(PSTR("Constant force %d\r\n"), data[2]);
				}
				hexdump(data, len);
				break;
			case REPORT_EFFECT_OPERATION:
				if (len != 4) {
					printf_P(PSTR("Hey!\r\n"));
					return -1;
				}
				/* Byte 0 : report ID
				 * Byte 1 : bit 7=rom flag, bits 6-0=effect block index
				 * Byte 2 : Effect operation
				 * Byte 3 : Loop count */


				printf_P(PSTR("EFFECT OP: rom=%s, idx=0x%02x : "), data[1] & 0x80 ? "Yes":"No", data[1] & 0x7F);

				// With dolphin, an "infinite" duration is set. The effect is started, then never
				// stopped. Maybe I misunderstood something? In any case, the following works
				// and feels about right.
				if (pad->_FFB_effect_duration == 0xffff) {
					if (data[3]) {
						pad->_loop_count = data[3] + 1; // +1 for a bit more strength
					} else {
						pad->_loop_count = 0;
					}
				} else {
			 		// main.c uses a 16ms interval timer for vibration "loops"
					pad->_loop_count = (pad->_FFB_effect_duration / 16) * data[3];
					printf_P(PSTR("%d loops for %d ms\r\n"), data[3], pad->_loop_count * 16);
				}

				switch(data[1] & 0x7F) // Effect block index
				{
					case 1: // constant force
					case 3: // square
					case 4: // sine
						switch (data[2]) // effect operation
						{
							case EFFECT_OP_START:
								printf_P(PSTR("Start (lp=%d)\r\n"), pad->_loop_count);
								pad->vibration_on = 1;
								break;

							case EFFECT_OP_START_SOLO:
								printf_P(PSTR("Start solo (lp=%d)\r\n"), pad->_loop_count);
								pad->vibration_on = 1;
								break;

							case EFFECT_OP_STOP:
								printf_P(PSTR("Stop (lp=%d)\r\n"), pad->_loop_count);
								pad->vibration_on = 0;
								break;
							default:
								printf_P(PSTR("OP?? %02x (lp=%d)\r\n"), data[2], pad->_loop_count);
								break;
						}
						break;

					// TODO : should probably drop these from the descriptor since they are
					default:
					case 2: // ramp
					case 5: // triangle
					case 6: // sawtooth up
					case 7: // sawtooth down
					case 8: // spring
					case 9: // damper
					case 10: // inertia
					case 11: // friction
					case 12: // custom force data
						printf_P(PSTR("Ununsed effect %d\n"), data[1] & 0x7F);
						break;
				}
				break;
			default:
				printf_P(PSTR("Set output report 0x%02x\r\n"), data[0]);
		}
	}
	else if ((rq->wValue >> 8) == HID_REPORT_TYPE_FEATURE) {
		switch(data[0])
		{
			case REPORT_CREATE_EFFECT:
				pad->_FFB_effect_index = data[1];
				printf_P(PSTR("create effect %d\n"), data[1]);
				break;

			default:
				printf_P(PSTR("What?\n"));
		}
	}
	else {
		printf_P(PSTR("impossible\n"));
	}
	return 0;
}
