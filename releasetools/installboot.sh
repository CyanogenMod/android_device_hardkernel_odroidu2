#!/sbin/sh

dd conv=notrunc seek=1256960 bs=1 if=/tmp/kernel of=/dev/block/mmcblk0
dd conv=notrunc seek=9645568 bs=1 if=/tmp/ramdisk-uboot.img of=/dev/block/mmcblk0
