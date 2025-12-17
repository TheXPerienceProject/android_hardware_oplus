#
# Copyright (C) 2022-2024 The LineageOS Project
#
# SPDX-License-Identifier: Apache-2.0
#

SEPOLICY_PLATFORM := $(subst device/qcom/sepolicy_vndr/,,$(SEPOLICY_PATH))

BOARD_VENDOR_SEPOLICY_DIRS += \
    hardware/oplus/sepolicy/dodge/vendor \
    hardware/oplus/sepolicy/dodge/vendor/$(SEPOLICY_PLATFORM)

SYSTEM_EXT_PRIVATE_SEPOLICY_DIRS += \
    hardware/oplus/sepolicy/dodge/private \
    hardware/oplus/sepolicy/dodge/private/$(SEPOLICY_PLATFORM)

SYSTEM_EXT_PUBLIC_SEPOLICY_DIRS += \
    hardware/oplus/sepolicy/dodge/public \
    hardware/oplus/sepolicy/dodge/public/$(SEPOLICY_PLATFORM)

ifneq ($(SEPOLICY_PLATFORM), legacy-um)
BOARD_VENDOR_SEPOLICY_DIRS += \
    hardware/oplus/sepolicy/dodge/vendor/common-um

SYSTEM_EXT_PRIVATE_SEPOLICY_DIRS += \
    hardware/oplus/sepolicy/dodge/private/common-um

SYSTEM_EXT_PUBLIC_SEPOLICY_DIRS += \
    hardware/oplus/sepolicy/dodge/public/common-um
endif

include device/xperience/sepolicy/libperfmgr/sepolicy.mk

