# Gamecube/N64 to USB adapter firmware (3rd generation)

(このフォークはraphnet様の成果物を改変してNSWに対応したものです。[本家から改変された内容](#Mod-infomation))

(This fork is a modification of raphnet's work to make it compatible with NSW. [more info](#Mod-infomation))

## Introduction

This is the source code for a Gamecube/N64 controller to USB adapter firmware
meant to run on [raphnet.net Multiuse PCB-X](http://www.raphnet.net/electronique/multiuse_pcbX/index_en.php).

## Homepage

* English: [Gamecube/N64 controller to USB adapter (Third generation)](http://www.raphnet.net/electronique/gcn64_usb_adapter_gen3/index_en.php)
* French: [Adaptateur manette Gamecube/N64 à USB (Troisième génération)](http://www.raphnet.net/electronique/gcn64_usb_adapter_gen3/index.php)

## License

The project is released under the General Public License version 3.

## Compiling the firmware

You will need a working avr-gcc toolchain with avr-libc and standard utilities such as make. Just
type 'make' and it should build just fine. Under Linux at least.
If you are compiling for a custom board or Arduino running on an ATmega32u4, then run 'make -f Makefile.32u4' instead.

## Programming the firmware

The makefile has a convenient 'flash' target which sends a command to the firmware to enter
the bootloader and then executes dfu-programmer (it must of course be installed) with the
correct arguments.

------------------
# Mod infomation
PORTDの4pin(D4)をHighにするとNSWモードになります。
NSWモードではN64コントローラもしくはGCコントローラのどちらかがUSBコントローラとして動作します。

## key mapping
ボタンやアナログスティック不足をカバーするため2種類のキーマップがあります。
SPECIAL KEYを押すとキーマップが切り替わります。

例：
- N64 C↑ + START：NSWのHomeボタン
- N64 C→ + 3Dスティック：NSWの右スティック
  - 通常時はNSWの左スティック
- GC Z+Rトリガー押し込み：NSWの右スティック押し込み

デフォルトのマッピングはN64/GCコントローラとNSWで近い名前のものを対応させるように作っています。
しかし、快適に利用するためにはゲームごとにカスタムする必要があると思われます。

マッピングをカスタムしたい場合、mappings.cとusbpad.cを改変してください。

### N64コントローラの場合
- N64 3Dスティック：NSW左スティック
- C→＋N64 3Dスティック：NSW右スティック
```c
// 通常マッピング
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
// 	{ N64_BTN_C_RIGHT,		NSW_BTN_RCLICK }, // SPECIAL KEY ( Right Stick )
	{	} /* terminator */
};
// SPECIAL KEY(C↑ボタン）押した場合のマッピング
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
// 	{ N64_BTN_C_RIGHT,		NSW_BTN_LCLICK }, // SPECIAL KEY ( Right Stick )
	{	} /* terminator */
};
```

### GCコントローラの場合
- GC 3Dスティック：NSW左スティック
- GC Cスティック：NSW右スティック
- Lトリガ軽く押す：NSW L
  - 押し込みすぎると判定が外れます。L長押しをしたい場合、軽く押した状態をキープする必要があります。
- Rトリガ軽く押す：NSW R
  - Lトリガと同じく判定がシビアです。
```c
// 通常マッピング
const static struct mapping map_gc_nsw[] PROGMEM = {
	{ GC_BTN_A,		NSW_BTN_A },
	{ GC_BTN_B,		NSW_BTN_B },
	{ GC_BTN_Y,		NSW_BTN_Y },
	{ GC_BTN_X,		NSW_BTN_X },
// 	{ GC_BTN_Z,		NSW_BTN_MINUS }, // SPECIAL KEY
	{ GC_BTN_START,	NSW_BTN_PLUS },
	{ GC_BTN_L,		NSW_BTN_ZL },
	{ GC_BTN_R,		NSW_BTN_ZR },
	{	} /* terminator */
};
// SPECIAL KEY(Zボタン）押した場合のマッピング
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
```
