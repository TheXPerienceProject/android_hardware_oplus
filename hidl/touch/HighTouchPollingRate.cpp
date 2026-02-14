/*
 * Copyright (C) 2022 The LineageOS Project
 * Copyright (C) 2025 The XPerience Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#define LOG_TAG "vendor.lineage.touch@1.0-service.oplus"

#include "HighTouchPollingRate.h"

#include <android-base/file.h>
#include <android-base/logging.h>

using ::android::base::ReadFileToString;
using ::android::base::WriteStringToFile;

namespace {

constexpr const char* kHighRatePath0 = "/sys/devices/platform/soc/ac0000.qcom,qupv3_1_geni_se/a90000.spi/spi_master/spi0/spi0.0/synaptics_tcm_hbp.0/high_rate";
constexpr const char* kHighRatePath1 = "/sys/devices/platform/soc/ac0000.qcom,qupv3_1_geni_se/a90000.spi/spi_master/spi1/spi1.0/synaptics_tcm_hbp.0/high_rate";

constexpr const char* kTouchRatePaths[] = {
    kHighRatePath0,
    kHighRatePath1,
};

constexpr const char* kPrimaryHighRatePath = kHighRatePath0;

}  // anonymous namespace

namespace vendor {
namespace lineage {
namespace touch {
namespace V1_0 {
namespace implementation {

Return<bool> HighTouchPollingRate::isEnabled() {
    std::string value;
    if (!ReadFileToString(kPrimaryHighRatePath, &value)) {
        LOG(ERROR) << "Failure to read the status of the primary route: " << kHighRatePath0;
        return false;
    }
    return value.rfind('1') != std::string::npos;
}

Return<bool> HighTouchPollingRate::setEnabled(bool enabled) {
    const char* value = enabled ? "1" : "0";
    bool all_success = true;

    // Iterate over all known routes and attempt to write the value
    for (const char* path : kTouchRatePaths) {
        if (!WriteStringToFile(value, path, true)) {
            all_success = false;
            // Log the error to assist with debugging
            LOG(ERROR) << "Error when entering the value (" << value << ") on the route: " << path;
        } else {
            LOG(INFO) << "Success when writing the value (" << value << ") on the route: " << path;
        }
    }

    return all_success;
}

}  // namespace implementation
}  // namespace V1_0
}  // namespace touch
}  // namespace lineage
}  // namespace vendor
