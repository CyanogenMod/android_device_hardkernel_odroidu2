=========================================
Installing CyanogenMod onto the U2's EMMC
=========================================


Since the U2 doesn't have a standard recovery mechanism, the
most universal method of flashing builds onto it is to use
a variation of ODROID's recommended procedure: fuse the data
directly into the eMMC adapter.

To do so, plug the adapter into your PC and run the installation
script like this:

$ sh ./install-emmc.sh /dev/sdX 

(where sdX is the device where your adapter may be found)
