@REM poser cycle
@REM 電源ON: boot user program
@REM reset: run bootloader, after 10sec, boot user program

@REM # resetボタン押してから1秒ぐらいの間、LeonardoBootloaderがいるはず。その時実行る。
path %PATH%;H:\dev\arduino-1.8.19\hardware\tools\avr\bin
avrdude -v -p atmega32u4 -P com4 -c avr109 -b 115200 -U "flash:w:gcn64usb.hex:i" -CH:\dev\arduino-1.8.19\hardware\tools\avr\etc\avrdude.conf
