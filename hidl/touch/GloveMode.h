/*
 * Copyright (C) 2021 The LineageOS Project
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include <vendor/xperience/touch/1.0/IGloveMode.h>

namespace vendor {
namespace xperience {
namespace touch {
namespace V1_0 {
namespace implementation {

using ::android::hardware::Return;

class GloveMode : public IGloveMode {
  public:
    // Methods from ::vendor::xperience::touch::V1_0::IGloveMode follow.
    Return<bool> isEnabled() override;
    Return<bool> setEnabled(bool enabled) override;
};

}  // namespace implementation
}  // namespace V1_0
}  // namespace touch
}  // namespace xperience
}  // namespace vendor