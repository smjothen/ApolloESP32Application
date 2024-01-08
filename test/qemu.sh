#!/bin/sh

# Download latest release from espressif/qemu
QEMU=$HOME/qemu-esp32/bin/qemu-system-xtensa

idf.py build && \
	cd build && \
	esptool.py --chip esp32 merge_bin --fill-flash-size 16MB -o flash_image.bin @flash_args && \
	$QEMU -nographic -machine esp32 -m 4M -drive file=flash_image.bin,if=mtd,format=raw 
