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

    INTERNAL_UBOOT_MULTIIMAGENAME := $(PRODUCT_VERSION)-$(TARGET_DEVICE)-Multiboot

    INTERNAL_UMULTIIMAGE_ARGS := -A arm -O linux -T ramdisk -C none -a 0x40800000 -n "ramdisk"

    INTERNAL_UMULTIIMAGE_ARGS += -d $(PRODUCT_OUT)/ramdisk.img $(PRODUCT_OUT)/ramdisk-uboot.img

$(INSTALLED_BOOTIMAGE_TARGET): $(MKIMAGE) $(INTERNAL_RAMDISK_FILES) $(BUILT_RAMDISK_TARGET) $(INSTALLED_KERNEL_TARGET)
			$(MKIMAGE) $(INTERNAL_UMULTIIMAGE_ARGS)
			$(MKBOOTIMG) --kernel $(INSTALLED_KERNEL_TARGET) --ramdisk $(PRODUCT_OUT)/ramdisk-uboot.img -o $@
			@echo ----- Made fastboot image -------- $@

endif #!TARGET_NO_KERNEL

