#
# Copyright (C) 2022-2024 The LineageOS Project
#
# SPDX-License-Identifier: Apache-2.0
#

SEPOLICY_PLATFORM := $(subst device/qcom/sepolicy_vndr/,,$(SEPOLICY_PATH))

BOARD_VENDOR_SEPOLICY_DIRS += \
    hardware/oplus_dodge/sepolicy/qti/vendor \
    hardware/oplus_dodge/sepolicy/qti/vendor/$(SEPOLICY_PLATFORM)

SYSTEM_EXT_PRIVATE_SEPOLICY_DIRS += \
    hardware/oplus_dodge/sepolicy/qti/private \
    hardware/oplus_dodge/sepolicy/qti/private/$(SEPOLICY_PLATFORM)

SYSTEM_EXT_PUBLIC_SEPOLICY_DIRS += \
    hardware/oplus_dodge/sepolicy/qti/public \
    hardware/oplus_dodge/sepolicy/qti/public/$(SEPOLICY_PLATFORM)

ifneq ($(SEPOLICY_PLATFORM), legacy-um)
BOARD_VENDOR_SEPOLICY_DIRS += \
    hardware/oplus_dodge/sepolicy/qti/vendor/common-um

SYSTEM_EXT_PRIVATE_SEPOLICY_DIRS += \
    hardware/oplus_dodge/sepolicy/qti/private/common-um

SYSTEM_EXT_PUBLIC_SEPOLICY_DIRS += \
    hardware/oplus_dodge/sepolicy/qti/public/common-um
endif

ifeq ($(TARGET_BOARD_PLATFORM),sun)
    BOARD_SEPOLICY_M4DEFS += \
        vendor_hal_drm_widevine_exec=hal_drm_widevine_exec \
        vendor_hal_esepowermanager_qti_exec=hal_secure_element_default_exec
endif

include device/xperience/sepolicy/libperfmgr/sepolicy.mk
