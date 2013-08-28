#
# Copyright (C) 2012 The Cyanogenmod Project
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#      http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.
#

#
# To use this bootimg 
#  
#  Add to your BoardConfig.mk:
#    BOARD_CUSTOM_BOOTIMG_MK := device/common/uboot-bootimg.mk
#  If using uboot multiimage add:
#    BOARD_USES_UBOOT_MULTIIMAGE := true
# 

#
# Ramdisk/boot image
#
LOCAL_PATH := $(call my-dir)

ifneq ($(strip $(TARGET_NO_KERNEL)),true)

    INSTALLED_BOOTIMAGE_TARGET := $(PRODUCT_OUT)/boot.img

    INTERNAL_UMULTIIMAGE_ARGS := -A arm -O linux -T ramdisk -C none -a 0x40800000 -n "ramdisk"

    INSTALLED_RAMDISK_TARGET := $(PRODUCT_OUT)/ramdisk-uboot.img

    INTERNAL_UMULTIIMAGE_ARGS += -d $(PRODUCT_OUT)/ramdisk.img $(INSTALLED_RAMDISK_TARGET)

$(INSTALLED_RAMDISK_TARGET): $(MKIMAGE) $(INTERNAL_RAMDISK_FILES) $(BUILT_RAMDISK_TARGET)
			$(MKIMAGE) $(INTERNAL_UMULTIIMAGE_ARGS)

$(INSTALLED_BOOTIMAGE_TARGET): $(MKBOOTIMG) $(INSTALLED_RAMDISK_TARGET) $(INSTALLED_KERNEL_TARGET)
			$(MKBOOTIMG) --kernel $(INSTALLED_KERNEL_TARGET) --ramdisk $(PRODUCT_OUT)/ramdisk-uboot.img -o $@
			@echo ----- Made fastboot image -------- $@

endif #!TARGET_NO_KERNEL

ifneq ($(strip $(TARGET_NO_RECOVERY)),true)
    INSTALLED_RECOVERYIMAGE_TARGET := $(PRODUCT_OUT)/recovery.img
    recovery_ramdisk := $(PRODUCT_OUT)/ramdisk-recovery.img
    RECOVERYFS_PATH := $(PRODUCT_OUT)/system/pseudorec/

    RCV_INTERNAL_UMULTIIMAGE_ARGS := -A arm -O linux -T ramdisk -C none -a 0x40800000 -n "ramdisk"

    RCV_INTERNAL_UMULTIIMAGE_ARGS += -d $(PRODUCT_OUT)/ramdisk-recovery.img $(RECOVERYFS_PATH)/recovery-uboot.img

$(INSTALLED_RECOVERYIMAGE_TARGET): $(MKBOOTIMG) $(INSTALLED_BOOTIMAGE_TARGET) $(MKIMAGE) $(recovery_ramdisk) $(recovery_kernel)
			mkdir -p $(RECOVERYFS_PATH)
			$(MKIMAGE) $(RCV_INTERNAL_UMULTIIMAGE_ARGS)
			$(MKBOOTIMG) --kernel $(recovery_kernel) --ramdisk $(RECOVERYFS_PATH)/recovery-uboot.img -o $@
			@echo ----- Made fastboot recovery image -------- $@
endif #!TARGET_NO_RECOVERY

