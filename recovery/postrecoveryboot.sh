#!/sbin/sh

while [ ! -b /dev/block/mmcblk0p1 ]; do
   sleep 2
done
mkdir /tmp/sdr
mount /dev/block/mmcblk0p1 /tmp/sdr
if [ -f /tmp/sdr/boot.scr-recoverybak ]; then
    cp -f /tmp/sdr/boot.scr-recoverybak /tmp/sdr/boot.scr
else
    rm -f /tmp/sdr/boot.scr
fi

umount /tmp/sdr
rmdir /tmp/sdr
