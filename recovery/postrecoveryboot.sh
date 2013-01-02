#!/sbin/sh

sleep 3
mount /dev/block/mmcblk0p1 /sdcard
if [ -f /sdcard/boot.scr-recoverybak ]; then
    cp -f /sdcard/boot.scr-recoverybak /sdcard/boot.scr
else
    rm -f /sdcard/boot.scr
fi

umount /sdcard
