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
#include <avr/pgmspace.h>
#include "mappings.h"
#include "gamepads.h"
#include "usbpad.h"

/* Default N64 and Gamecube mappings meant to work together
 * i.e. Controllers should be mostly interchangeable
 *
 *  - Main buttons first
 *  - Common buttons at the same place
 *  - Similar layout for GC Y/X and N64 C-Left and C-Down
 */

const static struct mapping map_gc_default[] PROGMEM = {
	{ GC_BTN_A,		USB_BTN(0) },
	{ GC_BTN_B,		USB_BTN(1) },
	{ GC_BTN_Z,		USB_BTN(2) },
	{ GC_BTN_START,	USB_BTN(3) },

	{ GC_BTN_L,		USB_BTN(4) },
	{ GC_BTN_R,		USB_BTN(5) },

	{ GC_BTN_Y,		USB_BTN(8) }, // N64 C-Left
	{ GC_BTN_X,		USB_BTN(7) }, // N64 C-Down

	{ GC_BTN_DPAD_UP,		USB_BTN(10) },
	{ GC_BTN_DPAD_DOWN,		USB_BTN(11) },
	{ GC_BTN_DPAD_LEFT,		USB_BTN(12) },
	{ GC_BTN_DPAD_RIGHT,	USB_BTN(13) },

	{	} /* terminator */
};

const static struct mapping map_n64_default[] PROGMEM = {
	{ N64_BTN_A,			USB_BTN(0) },
	{ N64_BTN_B,			USB_BTN(1) },
	{ N64_BTN_Z,			USB_BTN(2) },
	{ N64_BTN_START,		USB_BTN(3) },

	{ N64_BTN_L,			USB_BTN(4) },
	{ N64_BTN_R,			USB_BTN(5) },
	{ N64_BTN_C_UP,			USB_BTN(6) },
	{ N64_BTN_C_DOWN,		USB_BTN(7) }, // GC X

	{ N64_BTN_C_LEFT,		USB_BTN(8) }, // GC_Y
	{ N64_BTN_C_RIGHT,		USB_BTN(9) },

	{ N64_BTN_DPAD_UP,		USB_BTN(10) },
	{ N64_BTN_DPAD_DOWN,	USB_BTN(11) },
	{ N64_BTN_DPAD_LEFT,	USB_BTN(12) },
	{ N64_BTN_DPAD_RIGHT,	USB_BTN(13) },

	{	} /* terminator */
};

const static struct mapping map_n64_nsw[] PROGMEM = {
	{ N64_BTN_A,			NSW_BTN_A },
	{ N64_BTN_B,			NSW_BTN_B },
	{ N64_BTN_Z,			NSW_BTN_ZL },
	{ N64_BTN_START,		NSW_BTN_PLUS },
	{ N64_BTN_L,			NSW_BTN_MINUS },
	{ N64_BTN_R,			NSW_BTN_ZR },
// 	{ N64_BTN_C_UP,			NSW_BTN_L }, // SPECIAL KEY
	{ N64_BTN_C_DOWN,		NSW_BTN_Y },
	{ N64_BTN_C_LEFT,		NSW_BTN_X },
// 	{ N64_BTN_C_RIGHT,		NSW_BTN_RCLICK }, // SPECIAL KEY

	{	} /* terminator */
};
const static struct mapping map_n64_nsw_special[] PROGMEM = {
	{ N64_BTN_A,			NSW_BTN_A },
	{ N64_BTN_B,			NSW_BTN_B },
	{ N64_BTN_Z,			NSW_BTN_L },
	{ N64_BTN_START,		NSW_BTN_HOME },
	{ N64_BTN_L,			NSW_BTN_CAPTURE },
	{ N64_BTN_R,			NSW_BTN_R },
// 	{ N64_BTN_C_UP,			NSW_BTN_L }, // SPECIAL KEY
	{ N64_BTN_C_DOWN,		NSW_BTN_RCLICK },
	{ N64_BTN_C_LEFT,		NSW_BTN_LCLICK },
// 	{ N64_BTN_C_RIGHT,		NSW_BTN_LCLICK }, // SPECIAL KEY

	{	} /* terminator */
};
const static struct mapping map_gc_nsw[] PROGMEM = {
	{ GC_BTN_A,		NSW_BTN_A },
	{ GC_BTN_B,		NSW_BTN_B },
	{ GC_BTN_Y,		NSW_BTN_Y },
	{ GC_BTN_X,		NSW_BTN_X },
// 	{ GC_BTN_Z,		NSW_BTN_MINUS }, // SPECIAL KEY
	{ GC_BTN_START,	NSW_BTN_PLUS },
	{ GC_BTN_L,		NSW_BTN_L },
	{ GC_BTN_R,		NSW_BTN_R },
	{	} /* terminator */
};
const static struct mapping map_gc_nsw_l2[] PROGMEM = {
	{ GC_BTN_A,		NSW_BTN_A },
	{ GC_BTN_B,		NSW_BTN_B },
	{ GC_BTN_Y,		NSW_BTN_Y },
	{ GC_BTN_X,		NSW_BTN_X },
// 	{ GC_BTN_Z,		NSW_BTN_MINUS }, // SPECIAL KEY
	{ GC_BTN_START,	NSW_BTN_HOME},
	{ GC_BTN_L,		NSW_BTN_RCLICK },
	{ GC_BTN_R,		NSW_BTN_LCLICK },
	{	} /* terminator */
};

static uint16_t domap(const struct mapping *map, uint16_t input)
{
	const struct mapping *cur = map;
	uint16_t out = 0;
	uint16_t ctl_btn, usb_btn;

	while (1) {
		ctl_btn = pgm_read_word(&cur->ctl_btn);
		usb_btn = pgm_read_word(&cur->usb_btn);

		if (!ctl_btn || !usb_btn)
			break;

		if (input & ctl_btn) {
			out |= usb_btn;
		}
		cur++;
	}

	return out;
}

uint16_t mappings_do(uint8_t mapping_id, uint16_t input)
{
	switch(mapping_id) {
		case MAPPING_GAMECUBE_DEFAULT:
			return domap(map_gc_default, input);
		case MAPPING_N64_DEFAULT:
			return domap(map_n64_default, input);
		case MAPPING_N64_NSW:
			return domap(map_n64_nsw, input);
		case MAPPING_N64_NSW_L2:
			return domap(map_n64_nsw_special, input);
		case MAPPING_GAMECUBE_NSW:
			return domap(map_gc_nsw, input);
		case MAPPING_GAMECUBE_NSW_L2:
			return domap(map_gc_nsw_l2, input);
	}

	return 0;
}
