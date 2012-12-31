#!/bin/bash

if [ -z $1 ]
then
    echo "usage: ./install-emmc.sh <SD Reader's device file>"
    exit 0
fi

if [ -b $1 ]
then
    echo "$1 reader is identified."
else
    echo "$1 is NOT identified."
    exit 0
fi

ramdisk_offset=9645568
kernel_offset=1256960
system_partition=2

echo "Fusing kernel"
sudo dd conv=notrunc seek=$kernel_offset bs=1 if=kernel of=$1

echo "Fusing root filesystem"
sudo dd conv=notrunc seek=$ramdisk_offset bs=1 if=ramdisk-uboot.img of=$1

echo "Fusing main system partition"
sudo dd bs=1M if=system.img of=${1}${system_partition}
