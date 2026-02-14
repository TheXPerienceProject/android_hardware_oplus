/*
 * Copyright (C) 2025 The XPerience Project
 * SPDX-License-Identifier: Apache-2.0
 */

#define LOG_TAG "GloveModeService"

#include "GloveMode.h"

#include <android-base/file.h>
#include <android-base/logging.h>
#include <errno.h>
#include <string.h>

using android::base::ReadFileToString;
using android::base::WriteStringToFile;

namespace {

constexpr const char* kGloveModePath0 = "/sys/devices/platform/soc/ac0000.qcom,qupv3_1_geni_se/a90000.spi/spi_master/spi0/spi0.0/synaptics_tcm_hbp.0/glove_mode";
constexpr const char* kGloveModePath1 = "/sys/devices/platform/soc/ac0000.qcom,qupv3_1_geni_se/a90000.spi/spi_master/spi1/spi1.0/synaptics_tcm_hbp.0/glove_mode";

constexpr const char* kGloveModePaths[] = {
    kGloveModePath0,
    kGloveModePath1,
};

}//anonymous namespace

namespace vendor {
namespace lineage {
namespace touch {
namespace V1_0 {
namespace implementation {

Return<bool> GloveMode::isEnabled() {
    std::string value; 
    
    // Iterate over all known paths. The first successfully read path is the source of truth.
    for (const char* path : kGloveModePaths) {
        if (ReadFileToString(path, &value)) {
            // Check if the content contains '1' (handling potential newlines).
            // This path must exist and be readable for the function to return true/false state.
            return value.rfind('1') != std::string::npos;
        }
    }

    // If no path could be read, assume the feature is disabled.
    LOG(ERROR) << "Failed to read status from all known glove mode paths. Assuming disabled.";
    return false;
}

Return<bool> GloveMode::setEnabled(bool enabled) {
    const char* value = enabled ? "1" : "0";
    bool success = false;

    // Iterate over all known paths and attempt to write the value.
    for (const char* path : kGloveModePaths) {
        if (!WriteStringToFile(value, path, true)) {
            LOG(ERROR) << "Failed to write value (" << value << ") to path: " << path
                       << ". Error: " << strerror(errno);
        } else {
            success = true;
            LOG(INFO) << "Successfully wrote value (" << value << ") to path: " << path;
        }
    }

    // Return true if at least one path was successfully written.
    return success;

}

}  // namespace implementation
}  // namespace V1_0
}  // namespace touch
}  // namespace lineage
}  // namespace vendor
