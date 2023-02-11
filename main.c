/*	gc_n64_usb : Gamecube or N64 controller to USB adapter firmware
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
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <avr/io.h>
#include <avr/interrupt.h>
#include <util/delay.h>

#include "main.h"
#include "util.h"
#include "usart1.h"
#include "usb.h"
#include "gamepads.h"
#include "bootloader.h"

#include "gcn64_protocol.h"
#include "n64.h"
#include "gamecube.h"
#include "usbpad.h"
#include "eeprom.h"
#include "hiddata.h"
#include "usbstrings.h"
#include "intervaltimer.h"
#include "intervaltimer2.h"
#include "requests.h"
#include "stkchk.h"

#define MAX_PLAYERS		2

#define GCN64_USB_PID	0x0060
#define N64_USB_PID		0x0061
#define GC_USB_PID		0x0062

#define DUAL_GCN64_USB_PID	0x0063
#define DUAL_N64_USB_PID	0x0064
#define DUAL_GC_USB_PID		0x0065

#define KEYBOARD_PID		0x0066
#define KEYBOARD_PID2		0x0067
#define KEYBOARD_JS_PID		0x0068

int keyboard_main(void);

/* Those .c files are included rather than linked for we
 * want the sizeof() operator to work on the arrays */
#include "reportdesc.c"
#include "dataHidReport.c"

#define MAX_READ_ERRORS	30
static uint8_t error_count[MAX_PLAYERS] = { };

struct cfg0 {
	struct usb_configuration_descriptor configdesc;
	struct usb_interface_descriptor interface;
	struct usb_hid_descriptor hid;
	struct usb_endpoint_descriptor ep1_in;

	struct usb_interface_descriptor interface_admin;
	struct usb_hid_descriptor hid_data;
	struct usb_endpoint_descriptor ep2_in;
};

static const struct cfg0 cfg0 PROGMEM = {
	.configdesc = {
		.bLength = sizeof(struct usb_configuration_descriptor),
		.bDescriptorType = CONFIGURATION_DESCRIPTOR,
		.wTotalLength = sizeof(cfg0), // includes all descriptors returned together
		.bNumInterfaces = 1 + 1, // one interface per player + one management interface
		.bConfigurationValue = 1,
		.bmAttributes = CFG_DESC_ATTR_RESERVED, // set Self-powred and remote-wakeup here if needed.
		.bMaxPower = 25, // for 50mA
	},

	// Main interface, HID (player 1)
	.interface = {
		.bLength = sizeof(struct usb_interface_descriptor),
		.bDescriptorType = INTERFACE_DESCRIPTOR,
		.bInterfaceNumber = 0,
		.bAlternateSetting = 0,
		.bNumEndpoints = 1,
		.bInterfaceClass = USB_DEVICE_CLASS_HID,
		.bInterfaceSubClass = HID_SUBCLASS_NONE,
		.bInterfaceProtocol = HID_PROTOCOL_NONE,
	},
	.hid = {
		.bLength = sizeof(struct usb_hid_descriptor),
		.bDescriptorType = HID_DESCRIPTOR,
		.bcdHid = 0x0101,
		.bCountryCode = HID_COUNTRY_NOT_SUPPORTED,
		.bNumDescriptors = 1, // Only a report descriptor
		.bClassDescriptorType = REPORT_DESCRIPTOR,
		.wClassDescriptorLength = sizeof(gcn64_usbHidReportDescriptor),
	},
	.ep1_in = {
		.bLength = sizeof(struct usb_endpoint_descriptor),
		.bDescriptorType = ENDPOINT_DESCRIPTOR,
		.bEndpointAddress = USB_RQT_DEVICE_TO_HOST | 1, // 0x81
		.bmAttributes = TRANSFER_TYPE_INT,
		.wMaxPacketsize = 16,
		.bInterval = LS_FS_INTERVAL_MS(1),
	},

	// Second HID interface for config and update
	.interface_admin = {
		.bLength = sizeof(struct usb_interface_descriptor),
		.bDescriptorType = INTERFACE_DESCRIPTOR,
		.bInterfaceNumber = 1,
		.bAlternateSetting = 0,
		.bNumEndpoints = 1,
		.bInterfaceClass = USB_DEVICE_CLASS_HID,
		.bInterfaceSubClass = HID_SUBCLASS_NONE,
		.bInterfaceProtocol = HID_PROTOCOL_NONE,
	},
	.hid_data = {
		.bLength = sizeof(struct usb_hid_descriptor),
		.bDescriptorType = HID_DESCRIPTOR,
		.bcdHid = 0x0101,
		.bCountryCode = HID_COUNTRY_NOT_SUPPORTED,
		.bNumDescriptors = 1, // Only a report descriptor
		.bClassDescriptorType = REPORT_DESCRIPTOR,
		.wClassDescriptorLength = sizeof(dataHidReport),
	},
	.ep2_in = {
		.bLength = sizeof(struct usb_endpoint_descriptor),
		.bDescriptorType = ENDPOINT_DESCRIPTOR,
		.bEndpointAddress = USB_RQT_DEVICE_TO_HOST | 2, // 0x82
		.bmAttributes = TRANSFER_TYPE_INT,
		.wMaxPacketsize = 64,
		.bInterval = LS_FS_INTERVAL_MS(1),
	},
};

static const struct cfg0 cfg0_kb PROGMEM = {
	.configdesc = {
		.bLength = sizeof(struct usb_configuration_descriptor),
		.bDescriptorType = CONFIGURATION_DESCRIPTOR,
		.wTotalLength = sizeof(cfg0), // includes all descriptors returned together
		.bNumInterfaces = 1 + 1, // one interface per player + one management interface
		.bConfigurationValue = 1,
		.bmAttributes = CFG_DESC_ATTR_RESERVED, // set Self-powred and remote-wakeup here if needed.
		.bMaxPower = 25, // for 50mA
	},

	// Main interface, HID Keyboard
	.interface = {
		.bLength = sizeof(struct usb_interface_descriptor),
		.bDescriptorType = INTERFACE_DESCRIPTOR,
		.bInterfaceNumber = 0,
		.bAlternateSetting = 0,
		.bNumEndpoints = 1,
		.bInterfaceClass = USB_DEVICE_CLASS_HID,
		.bInterfaceSubClass = HID_SUBCLASS_NONE,
		.bInterfaceProtocol = HID_PROTOCOL_NONE,
	},
	.hid = {
		.bLength = sizeof(struct usb_hid_descriptor),
		.bDescriptorType = HID_DESCRIPTOR,
		.bcdHid = 0x0101,
		.bCountryCode = HID_COUNTRY_NOT_SUPPORTED,
		.bNumDescriptors = 1, // Only a report descriptor
		.bClassDescriptorType = REPORT_DESCRIPTOR,
		.wClassDescriptorLength = sizeof(gcKeyboardReport),
	},
	.ep1_in = {
		.bLength = sizeof(struct usb_endpoint_descriptor),
		.bDescriptorType = ENDPOINT_DESCRIPTOR,
		.bEndpointAddress = USB_RQT_DEVICE_TO_HOST | 1, // 0x81
		.bmAttributes = TRANSFER_TYPE_INT,
		.wMaxPacketsize = 16,
		.bInterval = LS_FS_INTERVAL_MS(1),
	},

	// Second HID interface for config and update
	.interface_admin = {
		.bLength = sizeof(struct usb_interface_descriptor),
		.bDescriptorType = INTERFACE_DESCRIPTOR,
		.bInterfaceNumber = 1,
		.bAlternateSetting = 0,
		.bNumEndpoints = 1,
		.bInterfaceClass = USB_DEVICE_CLASS_HID,
		.bInterfaceSubClass = HID_SUBCLASS_NONE,
		.bInterfaceProtocol = HID_PROTOCOL_NONE,
	},
	.hid_data = {
		.bLength = sizeof(struct usb_hid_descriptor),
		.bDescriptorType = HID_DESCRIPTOR,
		.bcdHid = 0x0101,
		.bCountryCode = HID_COUNTRY_NOT_SUPPORTED,
		.bNumDescriptors = 1, // Only a report descriptor
		.bClassDescriptorType = REPORT_DESCRIPTOR,
		.wClassDescriptorLength = sizeof(dataHidReport),
	},
	.ep2_in = {
		.bLength = sizeof(struct usb_endpoint_descriptor),
		.bDescriptorType = ENDPOINT_DESCRIPTOR,
		.bEndpointAddress = USB_RQT_DEVICE_TO_HOST | 2, // 0x82
		.bmAttributes = TRANSFER_TYPE_INT,
		.wMaxPacketsize = 64,
		.bInterval = LS_FS_INTERVAL_MS(1),
	},
};


struct cfg0_2p {
	struct usb_configuration_descriptor configdesc;
	struct usb_interface_descriptor interface;
	struct usb_hid_descriptor hid;
	struct usb_endpoint_descriptor ep1_in;

	struct usb_interface_descriptor interface_p2;
	struct usb_hid_descriptor hid_p2;
	struct usb_endpoint_descriptor ep2_in;

	struct usb_interface_descriptor interface_admin;
	struct usb_hid_descriptor hid_data;
	struct usb_endpoint_descriptor ep3_in;
};

static const struct cfg0_2p cfg0_2p PROGMEM = {
	.configdesc = {
		.bLength = sizeof(struct usb_configuration_descriptor),
		.bDescriptorType = CONFIGURATION_DESCRIPTOR,
		.wTotalLength = sizeof(cfg0_2p), // includes all descriptors returned together
		.bNumInterfaces = 2 + 1, // one interface per player + one management interface
		.bConfigurationValue = 1,
		.bmAttributes = CFG_DESC_ATTR_RESERVED, // set Self-powred and remote-wakeup here if needed.
		.bMaxPower = 25, // for 50mA
	},

	// Main interface, HID (player 1)
	.interface = {
		.bLength = sizeof(struct usb_interface_descriptor),
		.bDescriptorType = INTERFACE_DESCRIPTOR,
		.bInterfaceNumber = 0,
		.bAlternateSetting = 0,
		.bNumEndpoints = 1,
		.bInterfaceClass = USB_DEVICE_CLASS_HID,
		.bInterfaceSubClass = HID_SUBCLASS_NONE,
		.bInterfaceProtocol = HID_PROTOCOL_NONE,
	},
	.hid = {
		.bLength = sizeof(struct usb_hid_descriptor),
		.bDescriptorType = HID_DESCRIPTOR,
		.bcdHid = 0x0101,
		.bCountryCode = HID_COUNTRY_NOT_SUPPORTED,
		.bNumDescriptors = 1, // Only a report descriptor
		.bClassDescriptorType = REPORT_DESCRIPTOR,
		.wClassDescriptorLength = sizeof(gcn64_usbHidReportDescriptor),
	},
	.ep1_in = {
		.bLength = sizeof(struct usb_endpoint_descriptor),
		.bDescriptorType = ENDPOINT_DESCRIPTOR,
		.bEndpointAddress = USB_RQT_DEVICE_TO_HOST | 1, // 0x81
		.bmAttributes = TRANSFER_TYPE_INT,
		.wMaxPacketsize = 16,
		.bInterval = LS_FS_INTERVAL_MS(1),
	},

	// Main interface, HID (player 2)
	.interface_p2 = {
		.bLength = sizeof(struct usb_interface_descriptor),
		.bDescriptorType = INTERFACE_DESCRIPTOR,
		.bInterfaceNumber = 1,
		.bAlternateSetting = 0,
		.bNumEndpoints = 1,
		.bInterfaceClass = USB_DEVICE_CLASS_HID,
		.bInterfaceSubClass = HID_SUBCLASS_NONE,
		.bInterfaceProtocol = HID_PROTOCOL_NONE,
	},
	.hid_p2 = {
		.bLength = sizeof(struct usb_hid_descriptor),
		.bDescriptorType = HID_DESCRIPTOR,
		.bcdHid = 0x0101,
		.bCountryCode = HID_COUNTRY_NOT_SUPPORTED,
		.bNumDescriptors = 1, // Only a report descriptor
		.bClassDescriptorType = REPORT_DESCRIPTOR,
		.wClassDescriptorLength = sizeof(gcn64_usbHidReportDescriptor),
	},
	.ep2_in = {
		.bLength = sizeof(struct usb_endpoint_descriptor),
		.bDescriptorType = ENDPOINT_DESCRIPTOR,
		.bEndpointAddress = USB_RQT_DEVICE_TO_HOST | 2, // 0x82
		.bmAttributes = TRANSFER_TYPE_INT,
		.wMaxPacketsize = 16,
		.bInterval = LS_FS_INTERVAL_MS(1),
	},

	// Second HID interface for config and update
	.interface_admin = {
		.bLength = sizeof(struct usb_interface_descriptor),
		.bDescriptorType = INTERFACE_DESCRIPTOR,
		.bInterfaceNumber = 2,
		.bAlternateSetting = 0,
		.bNumEndpoints = 1,
		.bInterfaceClass = USB_DEVICE_CLASS_HID,
		.bInterfaceSubClass = HID_SUBCLASS_NONE,
		.bInterfaceProtocol = HID_PROTOCOL_NONE,
	},
	.hid_data = {
		.bLength = sizeof(struct usb_hid_descriptor),
		.bDescriptorType = HID_DESCRIPTOR,
		.bcdHid = 0x0101,
		.bCountryCode = HID_COUNTRY_NOT_SUPPORTED,
		.bNumDescriptors = 1, // Only a report descriptor
		.bClassDescriptorType = REPORT_DESCRIPTOR,
		.wClassDescriptorLength = sizeof(dataHidReport),
	},
	.ep3_in = {
		.bLength = sizeof(struct usb_endpoint_descriptor),
		.bDescriptorType = ENDPOINT_DESCRIPTOR,
		.bEndpointAddress = USB_RQT_DEVICE_TO_HOST | 3, // 0x83
		.bmAttributes = TRANSFER_TYPE_INT,
		.wMaxPacketsize = 64,
		.bInterval = LS_FS_INTERVAL_MS(1),
	},
};

static const struct cfg0_2p cfg0_2p_keyboard PROGMEM = {
	.configdesc = {
		.bLength = sizeof(struct usb_configuration_descriptor),
		.bDescriptorType = CONFIGURATION_DESCRIPTOR,
		.wTotalLength = sizeof(cfg0_2p), // includes all descriptors returned together
		.bNumInterfaces = 2 + 1, // one interface per player + one management interface
		.bConfigurationValue = 1,
		.bmAttributes = CFG_DESC_ATTR_RESERVED, // set Self-powred and remote-wakeup here if needed.
		.bMaxPower = 25, // for 50mA
	},

	// Joystick interface
	.interface = {
		.bLength = sizeof(struct usb_interface_descriptor),
		.bDescriptorType = INTERFACE_DESCRIPTOR,
		.bInterfaceNumber = 0,
		.bAlternateSetting = 0,
		.bNumEndpoints = 1,
		.bInterfaceClass = USB_DEVICE_CLASS_HID,
		.bInterfaceSubClass = HID_SUBCLASS_NONE,
		.bInterfaceProtocol = HID_PROTOCOL_NONE,
	},
	.hid = {
		.bLength = sizeof(struct usb_hid_descriptor),
		.bDescriptorType = HID_DESCRIPTOR,
		.bcdHid = 0x0101,
		.bCountryCode = HID_COUNTRY_NOT_SUPPORTED,
		.bNumDescriptors = 1, // Only a report descriptor
		.bClassDescriptorType = REPORT_DESCRIPTOR,
		.wClassDescriptorLength = sizeof(gcn64_usbHidReportDescriptor),
	},
	.ep1_in = {
		.bLength = sizeof(struct usb_endpoint_descriptor),
		.bDescriptorType = ENDPOINT_DESCRIPTOR,
		.bEndpointAddress = USB_RQT_DEVICE_TO_HOST | 1, // 0x81
		.bmAttributes = TRANSFER_TYPE_INT,
		.wMaxPacketsize = 16,
		.bInterval = LS_FS_INTERVAL_MS(1),
	},

	// HID Keyboard interface
	.interface_p2 = {
		.bLength = sizeof(struct usb_interface_descriptor),
		.bDescriptorType = INTERFACE_DESCRIPTOR,
		.bInterfaceNumber = 1,
		.bAlternateSetting = 0,
		.bNumEndpoints = 1,
		.bInterfaceClass = USB_DEVICE_CLASS_HID,
		.bInterfaceSubClass = HID_SUBCLASS_NONE,
		.bInterfaceProtocol = HID_PROTOCOL_NONE,
	},
	.hid_p2 = {
		.bLength = sizeof(struct usb_hid_descriptor),
		.bDescriptorType = HID_DESCRIPTOR,
		.bcdHid = 0x0101,
		.bCountryCode = HID_COUNTRY_NOT_SUPPORTED,
		.bNumDescriptors = 1, // Only a report descriptor
		.bClassDescriptorType = REPORT_DESCRIPTOR,
		.wClassDescriptorLength = sizeof(gcKeyboardReport),
	},
	.ep2_in = {
		.bLength = sizeof(struct usb_endpoint_descriptor),
		.bDescriptorType = ENDPOINT_DESCRIPTOR,
		.bEndpointAddress = USB_RQT_DEVICE_TO_HOST | 2, // 0x82
		.bmAttributes = TRANSFER_TYPE_INT,
		.wMaxPacketsize = 16,
		.bInterval = LS_FS_INTERVAL_MS(1),
	},

	// Second HID interface for config and update
	.interface_admin = {
		.bLength = sizeof(struct usb_interface_descriptor),
		.bDescriptorType = INTERFACE_DESCRIPTOR,
		.bInterfaceNumber = 2,
		.bAlternateSetting = 0,
		.bNumEndpoints = 1,
		.bInterfaceClass = USB_DEVICE_CLASS_HID,
		.bInterfaceSubClass = HID_SUBCLASS_NONE,
		.bInterfaceProtocol = HID_PROTOCOL_NONE,
	},
	.hid_data = {
		.bLength = sizeof(struct usb_hid_descriptor),
		.bDescriptorType = HID_DESCRIPTOR,
		.bcdHid = 0x0101,
		.bCountryCode = HID_COUNTRY_NOT_SUPPORTED,
		.bNumDescriptors = 1, // Only a report descriptor
		.bClassDescriptorType = REPORT_DESCRIPTOR,
		.wClassDescriptorLength = sizeof(dataHidReport),
	},
	.ep3_in = {
		.bLength = sizeof(struct usb_endpoint_descriptor),
		.bDescriptorType = ENDPOINT_DESCRIPTOR,
		.bEndpointAddress = USB_RQT_DEVICE_TO_HOST | 3, // 0x83
		.bmAttributes = TRANSFER_TYPE_INT,
		.wMaxPacketsize = 64,
		.bInterval = LS_FS_INTERVAL_MS(1),
	},
};


struct cfg0_nsw {
	struct usb_configuration_descriptor configdesc;
	struct usb_interface_descriptor interface;
	struct usb_hid_descriptor hid;
	struct usb_endpoint_descriptor ep1_in;
};

static const struct cfg0_nsw cfg0_nsw PROGMEM = {
	.configdesc = {
		.bLength = sizeof(struct usb_configuration_descriptor),
		.bDescriptorType = CONFIGURATION_DESCRIPTOR,
		.wTotalLength = sizeof(cfg0_nsw), // includes all descriptors returned together
		.bNumInterfaces = 1, // one interface per player
		.bConfigurationValue = 1,
		.bmAttributes = CFG_DESC_ATTR_RESERVED, // set Self-powred and remote-wakeup here if needed.
		.bMaxPower = 96, // for 50mA
	},

	// Main interface, HID (player 1)
	.interface = {
		.bLength = sizeof(struct usb_interface_descriptor),
		.bDescriptorType = INTERFACE_DESCRIPTOR,
		.bInterfaceNumber = 0,
		.bAlternateSetting = 0,
		.bNumEndpoints = 1,
		.bInterfaceClass = USB_DEVICE_CLASS_HID,
		.bInterfaceSubClass = HID_SUBCLASS_NONE,
		.bInterfaceProtocol = HID_PROTOCOL_NONE,
		.iInterface=2,
	},
	.hid = {
		.bLength = sizeof(struct usb_hid_descriptor),
		.bDescriptorType = HID_DESCRIPTOR,
		.bcdHid = 0x0111,
		.bCountryCode = HID_COUNTRY_NOT_SUPPORTED,
		.bNumDescriptors = 1, // Only a report descriptor
		.bClassDescriptorType = REPORT_DESCRIPTOR,
		.wClassDescriptorLength = sizeof(gcn64_usbHidReportDescriptorNSW),
	},
	.ep1_in = {
		.bLength = sizeof(struct usb_endpoint_descriptor),
		.bDescriptorType = ENDPOINT_DESCRIPTOR,
		.bEndpointAddress = USB_RQT_DEVICE_TO_HOST | 1, // 0x81
		.bmAttributes = TRANSFER_TYPE_INT,
		.wMaxPacketsize = 64,
		.bInterval = LS_FS_INTERVAL_MS(1),
	}
};

struct usb_device_descriptor device_descriptor = {
	.bLength = sizeof(struct usb_device_descriptor),
	.bDescriptorType = DEVICE_DESCRIPTOR,
	.bcdUSB = 0x0110,
	.bDeviceClass = 0, // set at interface
	.bDeviceSubClass = 0, // set at interface
	.bDeviceProtocol = 0,
	.bMaxPacketSize = 64,
	.idVendor = 0x289B,
	.idProduct = GCN64_USB_PID,
	.bcdDevice = VERSIONBCD,
	.bNumConfigurations = 1,
	.iManufacturer = 1,
	.iProduct = 2,
	.iSerialNumber = 3,
};

/** **/

static uint16_t _usbpad_hid_get_report(void *ctx, struct usb_request *rq, const uint8_t **dat)
{
	return usbpad_hid_get_report((struct usbpad*)ctx, rq, dat);
}

static uint8_t _usbpad_hid_set_report(void *ctx, const struct usb_request *rq, const uint8_t *dat, uint16_t len)
{
	return usbpad_hid_set_report((struct usbpad*)ctx, rq, dat, len);
}

static struct usb_parameters usb_params = {
	.flags = USB_PARAM_FLAG_CONFDESC_PROGMEM |
					USB_PARAM_FLAG_REPORTDESC_PROGMEM,
	.devdesc = (PGM_VOID_P)&device_descriptor,
	.configdesc = (PGM_VOID_P)&cfg0, // Patched in main() for two players
	.configdesc_ttllen = sizeof(cfg0), // Patched in main() for two players
	.num_strings = NUM_USB_STRINGS,
	.strings = g_usb_strings,

	.n_hid_interfaces = 1 + 1, // One per player + one management interface (patched in main() for two players)
	.hid_params = {
		[0] = {
			.reportdesc = gcn64_usbHidReportDescriptor,
			.reportdesc_len = sizeof(gcn64_usbHidReportDescriptor),
			.getReport = _usbpad_hid_get_report,
			.setReport = _usbpad_hid_set_report,
			.endpoint_size = 16,
		},
		[1] = {
			.reportdesc = dataHidReport,
			.reportdesc_len = sizeof(dataHidReport),
			.getReport = hiddata_get_report,
			.setReport = hiddata_set_report,
			.endpoint_size = 64,
		},
	},
};

void hwinit(void)
{
	/* PORTB
	 *
	 * 7: NC    Output low
	 * 6: NC	Output low
	 * 5: NC	Output low
	 * 4: NC	Output low
	 * 3: MISO	Output low
	 * 2: MOSI	Output low
	 * 1: SCK	Output low
	 * 0: NC	Output low
	 */
	PORTB = 0x00;
	DDRB = 0xFF;

	/* PORTC
	 *
	 * 7: NC	Output low
	 * 6: NC	Output low
	 * 5: NC	Output low
	 * 4: NC	Output low
	 * 3: (no such pin)
	 * 2: NC	Output low
	 * 1: RESET	(N/A: Reset input per fuses) (left floating)
	 * 0: XTAL2	(N/A: Crystal oscillator) (left floating)
	 */
	DDRC = 0xfc;
	PORTC = 0x00;

	/* PORTD
	 *
	 * 7: HWB		Input (external pull-up)
	 * 6: NC		Output low
	 * 5: NC		Output low
	 * 4: NSW_MODE  Input(NSW_MODE switch)
	 * 3: IO3_MCU	Input
	 * 2: IO2_MCU	Input
	 * 1: IO1_MCU	Input
	 * 0: IO0_MCU	Input
	 */
	PORTD = 0x00;
	DDRD = 0x71;

	// System clock. External crystal is 16 Mhz and we want
	// to run at max. speed.
	CLKPR = 0x80;
	CLKPR = 0x0; // Division factor of 1
	PRR0 = 0;
	PRR1 = 0;
}

unsigned char g_t;
unsigned char g_t2;

void led_test(){
	g_t++;
	if(g_t % 200==0){
		if(g_t2 & 1){
			PORTD |=0x20;
		}else{
			PORTD &= ~0x20;
		}
		g_t2++;
	}
}
uint8_t is_nsw_mode(){
	return (PIND & 0x10) ? 1 : 0;;
}


uint8_t num_players = 1;
unsigned char current_pad_type[NUM_CHANNELS] = { };

Gamepad *detectPad(unsigned char chn)
{
	current_pad_type[chn] = gcn64_detectController(chn);

	switch (current_pad_type[chn])
	{
		case CONTROLLER_IS_ABSENT:
		case CONTROLLER_IS_UNKNOWN:
			return NULL;

		case CONTROLLER_IS_N64_MOUSE:
		case CONTROLLER_IS_N64:
			return n64GetGamepad();

		case CONTROLLER_IS_GC:
			return gamecubeGetGamepad();

		case CONTROLLER_IS_GC_KEYBOARD:
			return gamecubeGetKeyboard();
	}

	return NULL;
}

/* Called after eeprom content is loaded. */
void eeprom_app_ready(void)
{
	static wchar_t serial_from_eeprom[SERIAL_NUM_LEN+1];
	int i;

	for (i=0; i<SERIAL_NUM_LEN; i++) {
		serial_from_eeprom[i] = g_eeprom_data.cfg.serial[i];
	}
	serial_from_eeprom[i] = 0;
	g_usb_strings[USB_STRING_SERIAL_IDX] = serial_from_eeprom;
}

static struct usbpad usbpads[MAX_PLAYERS];
static char g_polling_suspended = 0;

static void setSuspendPolling(uint8_t suspend)
{
	g_polling_suspended = suspend;
}

static void forceVibration(uint8_t channel, uint8_t force)
{
	if (channel < MAX_PLAYERS) {
		usbpad_forceVibrate(&usbpads[channel], force);
	}
}

static uint8_t getSupportedModes(uint8_t *dst)
{
	uint8_t idx = 0;

	switch (g_eeprom_data.cfg.mode)
	{
		// Allow toggling between keyboard and joystick modes on
		// single-port gamecube adapter
		case CFG_MODE_GC_ONLY:
		case CFG_MODE_KEYBOARD:
			dst[idx++] = CFG_MODE_GC_ONLY;
			dst[idx++] = CFG_MODE_KEYBOARD;
			break;

		// Allow toggling between two joysticks and joystick + keyboard modes
		// on dual-port gamecube adapter
		case CFG_MODE_2P_GC_ONLY:
		case CFG_MODE_KB_AND_JS:
			dst[idx++] = CFG_MODE_2P_GC_ONLY;
			dst[idx++] = CFG_MODE_KB_AND_JS;
			break;

		// On N64/GC adapters, there is a GC port so we should support
		// keyboards there. Use KEYBOARD_2 config here to avoid mixup
		// with the GC-only adapter variation.
		case CFG_MODE_STANDARD:
		case CFG_MODE_KEYBOARD_2:
			dst[idx++] = CFG_MODE_STANDARD;
			dst[idx++] = CFG_MODE_KEYBOARD_2;
			break;

		default:
			dst[idx++] = CFG_MODE_STANDARD;
			dst[idx++] = CFG_MODE_N64_ONLY;
			dst[idx++] = CFG_MODE_GC_ONLY;
			dst[idx++] = CFG_MODE_2P_STANDARD;
			dst[idx++] = CFG_MODE_2P_N64_ONLY;
			dst[idx++] = CFG_MODE_2P_GC_ONLY;
			dst[idx++] = CFG_MODE_KEYBOARD;
			dst[idx++] = CFG_MODE_KB_AND_JS;
			break;
	}

	return idx;
}

static struct hiddata_ops hiddata_ops = {
	.suspendPolling = setSuspendPolling,
	.forceVibration = forceVibration,
	.getSupportedModes = getSupportedModes,
};

#define STATE_WAIT_POLLTIME			0
#define STATE_POLL_PAD				1
#define STATE_WAIT_INTERRUPT_READY	2
#define STATE_TRANSMIT				3
#define STATE_WAIT_INTERRUPT_READY_P2	4
#define STATE_TRANSMIT_P2				5

int main(void)
{
	Gamepad *pads[MAX_PLAYERS] = { };
	gamepad_data pad_data;
	uint8_t gamepad_vibrate = 0;
	uint8_t state = STATE_WAIT_POLLTIME;
	uint8_t channel;
	uint8_t i;
	uint8_t nsw_mode;

	hwinit();
	usart1_init();
	eeprom_init();
	intervaltimer_init();
	intervaltimer2_init();
	stkchk_init();

	nsw_mode = is_nsw_mode();

	switch (g_eeprom_data.cfg.mode)
	{
		default:
		case CFG_MODE_STANDARD:
			usbstrings_changeProductString_P(PSTR("GC/N64 to USB v"VERSIONSTR_SHORT));
			break;

		case CFG_MODE_N64_ONLY:
			usbstrings_changeProductString_P(PSTR("N64 to USB v"VERSIONSTR_SHORT));
			device_descriptor.idProduct = N64_USB_PID;
			break;

		case CFG_MODE_GC_ONLY:
			usbstrings_changeProductString_P(PSTR("Gamecube to USB v"VERSIONSTR_SHORT));
			device_descriptor.idProduct = GC_USB_PID;
			break;

		case CFG_MODE_2P_STANDARD:
			usbstrings_changeProductString_P(PSTR("Dual GC/N64 to USB v"VERSIONSTR_SHORT));
			device_descriptor.idProduct = DUAL_GCN64_USB_PID;
			num_players = 2;
			break;

		case CFG_MODE_2P_N64_ONLY:
			usbstrings_changeProductString_P(PSTR("Dual N64 to USB v"VERSIONSTR_SHORT));
			device_descriptor.idProduct = DUAL_N64_USB_PID;
			num_players = 2;
			break;

		case CFG_MODE_2P_GC_ONLY:
			usbstrings_changeProductString_P(PSTR("Dual Gamecube to USB v"VERSIONSTR_SHORT));
			device_descriptor.idProduct = DUAL_GC_USB_PID;
			num_players = 2;
			break;

		case CFG_MODE_KB_AND_JS:
		case CFG_MODE_KEYBOARD:
		case CFG_MODE_KEYBOARD_2:
			keyboard_main();
			break;
	}

	// 2-players common
	if (num_players == 2) {
		usb_params.configdesc = (PGM_VOID_P)&cfg0_2p;
		usb_params.configdesc_ttllen = sizeof(cfg0_2p);
		usb_params.n_hid_interfaces = 3;
		// Move the management interface is the last position
		memcpy(usb_params.hid_params + 2, usb_params.hid_params + 1, sizeof(struct usb_hid_parameters));
		// Add a second player interface between them
		memcpy(usb_params.hid_params + 1, usb_params.hid_params + 0, sizeof(struct usb_hid_parameters));
	}	

	if(nsw_mode){
		device_descriptor.idVendor = 0x0f0d;
		device_descriptor.idProduct = 0x0092;
		device_descriptor.bcdDevice = 0x0001;
		device_descriptor.bcdUSB = 0x200;
		device_descriptor.iSerialNumber = 0;
		usb_params.configdesc = (PGM_VOID_P)&cfg0_nsw;
		usb_params.configdesc_ttllen = sizeof(cfg0_nsw);
		usb_params.n_hid_interfaces = 1;
		usb_params.hid_params[0].reportdesc = gcn64_usbHidReportDescriptorNSW;
		usb_params.hid_params[0].reportdesc_len = sizeof(gcn64_usbHidReportDescriptorNSW);
	}

	for (i=0; i<num_players; i++) {
		usbpad_init(&usbpads[i], nsw_mode);
		usb_params.hid_params[i].ctx = &usbpads[i];
	}

	sei();
	usb_init(&usb_params);

	// Timebase for force feedback 'loop count'
	intervaltimer2_set16ms();

	while (1)
	{
		static char last_v[MAX_PLAYERS] = { };

		if (stkchk_verify()) {
			enterBootLoader();
		}

		usb_doTasks();
		hiddata_doTask(&hiddata_ops);
		// Run vibration tasks
		if (intervaltimer2_get()) {
			for (channel=0; channel < num_players; channel++) {
				usbpad_vibrationTask(&usbpads[channel]);
			}
		}

		switch(state)
		{
			case STATE_WAIT_POLLTIME:
				if (!g_polling_suspended) {
					intervaltimer_set(g_eeprom_data.cfg.poll_interval[0]);
					if (intervaltimer_get()) {
						state = STATE_POLL_PAD;
					}
				}
				break;

			case STATE_POLL_PAD:
					led_test();
				for (channel=0; channel<num_players; channel++)
				{
					/* Try to auto-detect controller if none*/
					if (!pads[channel]) {
						pads[channel] = detectPad(channel);
						if (pads[channel] && (pads[channel]->hotplug)) {
							// For gamecube, this make sure the next
							// analog values we read become the center
							// reference.
							pads[channel]->hotplug(channel);
						}
					}

					/* Read from the pad by calling update */
					if (pads[channel]) {
						if (pads[channel]->update(channel)) {
							error_count[channel]++;
							if (error_count[channel] > MAX_READ_ERRORS) {
								pads[channel] = NULL;
								error_count[channel] = 0;
								continue;
							}
						} else {
							error_count[channel]=0;
						}

						if (pads[channel]->changed(channel) || nsw_mode)
						{
							pads[channel]->getReport(channel, &pad_data);
							usbpad_update(&usbpads[channel], &pad_data);
							state = STATE_WAIT_INTERRUPT_READY;
							continue;
						}
					} else {
						/* Just make sure the gamepad state holds valid data
						 * to appear inactive (no buttons and axes in neutral) */
						usbpad_update(&usbpads[channel], NULL);
					}
				}
				/* If there were change on any of the gamepads, state will
				 * be set to STATE_WAIT_INTERRUPT_READY. Otherwise, go back
				 * to WAIT_POLLTIME. */
				if (state == STATE_POLL_PAD) {
					state = STATE_WAIT_POLLTIME;
				}
				break;

			case STATE_WAIT_INTERRUPT_READY:
				/* Wait until one of the interrupt endpoint is ready */
				if (usb_interruptReady_ep1() || (num_players>1 && usb_interruptReady_ep2())) {
					state = STATE_TRANSMIT;
				}
				break;

			case STATE_TRANSMIT:
				if (usb_interruptReady_ep1()) {
					usb_interruptSend_ep1(usbpad_getReportBuffer(&usbpads[0]), usbpad_getReportSize());
				}
				if (num_players>1 && usb_interruptReady_ep2()) {
					usb_interruptSend_ep2(usbpad_getReportBuffer(&usbpads[1]), usbpad_getReportSize());
				}
				state = STATE_WAIT_POLLTIME;
				break;

		}

		for (channel=0; channel < num_players; channel++) {
			gamepad_vibrate = usbpad_mustVibrate(&usbpads[channel]);
			if (last_v[channel] != gamepad_vibrate) {
				if (pads[channel] && pads[channel]->setVibration) {
					pads[channel]->setVibration(channel, gamepad_vibrate);
				}
				last_v[channel] = gamepad_vibrate;
			}
		}
	}

	return 0;
}

int keyboard_main(void)
{
	Gamepad *pads[MAX_PLAYERS] = { };
	gamepad_data pad_data;
	uint8_t gamepad_vibrate = 0;
	uint8_t state = STATE_WAIT_POLLTIME;
	uint8_t channel;
	uint8_t i;

	hwinit();
	usart1_init();
	eeprom_init();
	intervaltimer_init();
	intervaltimer2_init();
	stkchk_init();

	switch (g_eeprom_data.cfg.mode)
	{
		default:
		case CFG_MODE_KEYBOARD_2:
			usbstrings_changeProductString_P(PSTR("KB to USB v"VERSIONSTR_SHORT));
			device_descriptor.idProduct = KEYBOARD_PID2;

			usb_params.configdesc = (PGM_VOID_P)&cfg0_kb;
			usb_params.configdesc_ttllen = sizeof(cfg0_kb);

			// replace Joystick report descriptor by keyboard
			usb_params.hid_params[0].reportdesc = gcKeyboardReport;
			usb_params.hid_params[0].reportdesc_len = sizeof(gcKeyboardReport);
			break;

		case CFG_MODE_KEYBOARD:
			usbstrings_changeProductString_P(PSTR("GC KB to USB v"VERSIONSTR_SHORT));
			device_descriptor.idProduct = KEYBOARD_PID;

			usb_params.configdesc = (PGM_VOID_P)&cfg0_kb;
			usb_params.configdesc_ttllen = sizeof(cfg0_kb);

			// replace Joystick report descriptor by keyboard
			usb_params.hid_params[0].reportdesc = gcKeyboardReport;
			usb_params.hid_params[0].reportdesc_len = sizeof(gcKeyboardReport);
			break;

		case CFG_MODE_KB_AND_JS:
			usbstrings_changeProductString_P(PSTR("GC KB+JS to USB v"VERSIONSTR_SHORT));
			device_descriptor.idProduct = KEYBOARD_JS_PID;

			usb_params.configdesc = (PGM_VOID_P)&cfg0_2p_keyboard;
			usb_params.configdesc_ttllen = sizeof(cfg0_2p_keyboard);

			// Move the management interface to the last position
			memcpy(usb_params.hid_params + 2, usb_params.hid_params + 1, sizeof(struct usb_hid_parameters));
			// Add a second player interface between them (still a joystick)
			memcpy(usb_params.hid_params + 1, usb_params.hid_params + 0, sizeof(struct usb_hid_parameters));
			// Convert second Joystick report descriptor to a keyboard
			usb_params.hid_params[1].reportdesc = gcKeyboardReport;
			usb_params.hid_params[1].reportdesc_len = sizeof(gcKeyboardReport);

			usb_params.n_hid_interfaces = 3;
			num_players = 2;

			break;
	}

	for (i=0; i<num_players; i++) {
		usbpad_init(&usbpads[i], 0);
		usb_params.hid_params[i].ctx = &usbpads[i];
	}

	sei();
	usb_init(&usb_params);

	// Timebase for force feedback 'loop count'
	intervaltimer2_set16ms();

	while (1)
	{
		static char last_v[MAX_PLAYERS] = { };

		if (stkchk_verify()) {
			enterBootLoader();
		}

		usb_doTasks();
		hiddata_doTask(&hiddata_ops);
		// Run vibration tasks
		if (intervaltimer2_get()) {
			for (channel=0; channel < num_players; channel++) {
				usbpad_vibrationTask(&usbpads[channel]);
			}
		}

		switch(state)
		{
			case STATE_WAIT_POLLTIME:
				if (!g_polling_suspended) {
					intervaltimer_set(g_eeprom_data.cfg.poll_interval[0]);
					if (intervaltimer_get()) {
						state = STATE_POLL_PAD;
					}
				}
				break;

			case STATE_POLL_PAD:
				for (channel=0; channel<num_players; channel++)
				{
					/* Try to auto-detect controller if none*/
					if (!pads[channel]) {
						pads[channel] = detectPad(channel);
						if (pads[channel] && (pads[channel]->hotplug)) {
							// For gamecube, this make sure the next
							// analog values we read become the center
							// reference.
							pads[channel]->hotplug(channel);
						}
					}

					/* Read from the pad by calling update */
					if (pads[channel]) {
						if (pads[channel]->update(channel)) {
							error_count[channel]++;
							if (error_count[channel] > MAX_READ_ERRORS) {
								pads[channel] = NULL;
								error_count[channel] = 0;
								continue;
							}
						} else {
							error_count[channel]=0;
						}

						if (pads[channel]->changed(channel))
						{
							pads[channel]->getReport(channel, &pad_data);

							if ((num_players == 1) && (channel == 0)) {
								// single-port adapter in keyboard mode (kb in port 1)
								usbpad_update_kb(&usbpads[channel], &pad_data);
							} else if ((num_players == 2) && (channel == 1)) {
								// dual-port adapter in keyboard mode (kb in port 2)
								usbpad_update_kb(&usbpads[channel], &pad_data);
							} else {
								usbpad_update(&usbpads[channel], &pad_data);
							}
							state = STATE_WAIT_INTERRUPT_READY;
							continue;
						}
					} else {
						/* Just make sure the gamepad state holds valid data
						 * to appear inactive (no buttons and axes in neutral) */
						usbpad_update(&usbpads[channel], NULL);
					}
				}
				/* If there were change on any of the gamepads, state will
				 * be set to STATE_WAIT_INTERRUPT_READY. Otherwise, go back
				 * to WAIT_POLLTIME. */
				if (state == STATE_POLL_PAD) {
					state = STATE_WAIT_POLLTIME;
				}
				break;

			case STATE_WAIT_INTERRUPT_READY:
				/* Wait until one of the interrupt endpoint is ready */
				if (usb_interruptReady_ep1() || (num_players>1 && usb_interruptReady_ep2())) {
					state = STATE_TRANSMIT;
				}
				break;

			case STATE_TRANSMIT:
				if (usb_interruptReady_ep1()) {
					if (num_players == 1) {
						// Single-port adapters have the keyboard in port 1
						usb_interruptSend_ep1(usbpad_getReportBuffer(&usbpads[0]), usbpad_getReportSizeKB());
					} else {
						usb_interruptSend_ep1(usbpad_getReportBuffer(&usbpads[0]), usbpad_getReportSize());
					}
				}
				// Keyboard is always in second port on dual port adapters
				if (num_players>1 && usb_interruptReady_ep2()) {
					usb_interruptSend_ep2(usbpad_getReportBuffer(&usbpads[1]), usbpad_getReportSizeKB());
				}
				state = STATE_WAIT_POLLTIME;
				break;

		}

		for (channel=0; channel < num_players; channel++) {
			gamepad_vibrate = usbpad_mustVibrate(&usbpads[channel]);
			if (last_v[channel] != gamepad_vibrate) {
				if (pads[channel] && pads[channel]->setVibration) {
					pads[channel]->setVibration(channel, gamepad_vibrate);
				}
				last_v[channel] = gamepad_vibrate;
			}
		}
	}

	return 0;
}

