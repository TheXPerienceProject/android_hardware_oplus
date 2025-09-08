/*
 * Copyright (C) 2021-2024 The LineageOS Project
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "AlsCorrection.h"

#include <android-base/properties.h>
#include <android/binder_manager.h>
#include <binder/IBinder.h>
#include <binder/IServiceManager.h>
#include <cmath>
#include <fstream>
#include <log/log.h>
#include <utils/Timers.h>
#include <mutex>
#include <atomic>

using aidl::vendor::lineage::oplus_als::AreaRgbCaptureResult;
using aidl::vendor::lineage::oplus_als::IAreaCapture;
using android::base::GetBoolProperty;
using android::base::GetIntProperty;
using android::base::GetProperty;

#define ALS_CALI_DIR "/proc/sensor/als_cali/"
#define BRIGHTNESS_DIR "/sys/class/backlight/panel0-backlight/"

namespace android {
namespace hardware {
namespace sensors {
namespace V2_1 {
namespace implementation {

enum class ColorChannel : int {
    RED = 0,
    GREEN = 1,
    BLUE = 2,
    WHITE = 3,
};

static const std::string rgbw_max_lux_paths[4] = {
    ALS_CALI_DIR "red_max_lux",
    ALS_CALI_DIR "green_max_lux",
    ALS_CALI_DIR "blue_max_lux",
    ALS_CALI_DIR "white_max_lux",
};

struct als_config {
    bool hbr{false};
    float rgbw_max_lux[4]{0.0f,0.0f,0.0f,0.0f};
    float rgbw_max_lux_div[4]{1.0f,1.0f,1.0f,1.0f};
    float rgbw_lux_postmul[4]{0.0f,0.0f,0.0f,0.0f};
    float rgbw_poly[4][4]{{0.0f}};
    float grayscale_weights[3]{0.0f,0.0f,0.0f};
    float sensor_gaincal_points[4]{0.0f};
    float sensor_inverse_gain[4]{1.0f,1.0f,1.0f,1.0f};
    float agc_threshold{0.0f};
    float calib_gain{1.0f};
    float bias{0.0f};
    float max_brightness{1023.0f};
};

static struct {
    float middle;
    float min, max;
} hysteresis_ranges[] = {
    { 0.f, 0.f, 4.f },
    { 7.f, 1.f, 12.f },
    { 15.f, 5.f, 30.f },
    { 30.f, 10.f, 50.f },
    { 360.f, 25.f, 700.f },
    { 1200.f, 300.f, 1600.f },
    { 2250.f, 1000.f, 2940.f },
    { 4600.f, 2000.f, 5900.f },
    { 10000.f, 4000.f, 80000.f },
    { HUGE_VALF, 8000.f, HUGE_VALF },
};

struct als_state {
    nsecs_t last_update{0};
    nsecs_t last_forced_update{0};
    bool force_update{true};
    float hyst_min{-1.f};
    float hyst_max{-1.f};
    float last_corrected_value{0.f};
    float last_agc_gain{0.f};
};

static als_state state;
static als_config conf;
static std::shared_ptr<IAreaCapture> service;
static std::mutex state_mutex;

template <typename T>
static T get(const std::string& path, const T& def) {
    std::ifstream file(path);
    T result{};
    file >> result;
    return file.fail() ? def : result;
}

static float polyEval(const float coef[], size_t count, float x) {
    float result = 0.0f;
    for (size_t i = 0; i < count; ++i) {
        result = result * x + coef[i];
    }
    return result;
}

static void parseProperty(const std::string& key, float* arr, size_t count) {
    std::istringstream is(GetProperty(key, ""));
    for (size_t i = 0; i < count; ++i) {
        if (!(is >> arr[i])) {
            ALOGW("Failed to parse property %s at index %zu, defaulting to 0.0f", key.c_str(), i);
            arr[i] = 0.0f;
        }
    }
}

void AlsCorrection::reloadConfig() {
    bool hbr = GetBoolProperty("vendor.sensors.als_correction.hbr", false);
    int bias = GetIntProperty("vendor.sensors.als_correction.bias", 0);

    float tmp_rgbw_max_lux_div[4];
    float tmp_rgbw_poly[4][4];
    float tmp_grayscale_weights[3];
    float tmp_sensor_gaincal_points[4];
    float tmp_sensor_inverse_gain[4];

    parseProperty("vendor.sensors.als_correction.rgbw_max_lux_div", tmp_rgbw_max_lux_div, 4);
    parseProperty("vendor.sensors.als_correction.rgbw_poly1", tmp_rgbw_poly[0], 4);
    parseProperty("vendor.sensors.als_correction.rgbw_poly2", tmp_rgbw_poly[1], 4);
    parseProperty("vendor.sensors.als_correction.rgbw_poly3", tmp_rgbw_poly[2], 4);
    parseProperty("vendor.sensors.als_correction.rgbw_poly4", tmp_rgbw_poly[3], 4);
    parseProperty("vendor.sensors.als_correction.grayscale_weights", tmp_grayscale_weights, 3);
    parseProperty("vendor.sensors.als_correction.sensor_gaincal_points", tmp_sensor_gaincal_points, 4);
    parseProperty("vendor.sensors.als_correction.sensor_inverse_gain", tmp_sensor_inverse_gain, 4);

    std::lock_guard<std::mutex> lock(state_mutex);
    conf.hbr = hbr;
    conf.bias = static_cast<float>(bias);

    for (int i = 0; i < 4; i++) {
        conf.rgbw_max_lux_div[i] = tmp_rgbw_max_lux_div[i] == 0.f ? 1.f : tmp_rgbw_max_lux_div[i];
        for (int j = 0; j < 4; j++) {
            conf.rgbw_poly[i][j] = tmp_rgbw_poly[i][j];
        }
        conf.sensor_gaincal_points[i] = tmp_sensor_gaincal_points[i];
        conf.sensor_inverse_gain[i] = tmp_sensor_inverse_gain[i] == 0.f ? 1.f : tmp_sensor_inverse_gain[i];
    }
    for (int i = 0; i < 3; i++) {
        conf.grayscale_weights[i] = tmp_grayscale_weights[i];
    }

    for (int i = 0; i < 4; i++) {
        float max_lux = get(rgbw_max_lux_paths[i], 0.f);
        conf.rgbw_max_lux[i] = max_lux > 0.f ? max_lux : 0.f;
    }

    for (int i = 0; i < 4; i++) {
        if (conf.rgbw_max_lux_div[i] <= 0.f) {
            ALOGW("Invalid rgbw_max_lux_div[%d]=%f, setting to 1.0f", i, conf.rgbw_max_lux_div[i]);
            conf.rgbw_max_lux_div[i] = 1.0f;
        }
    }

    float rgbw_acc = 0.f;
    for (int i = 0; i < 4; i++) {
        if (i < 3) {
            rgbw_acc += conf.rgbw_max_lux[i];
            conf.rgbw_lux_postmul[i] = conf.rgbw_max_lux[i] / conf.rgbw_max_lux_div[i];
        } else {
            rgbw_acc -= conf.rgbw_max_lux[i];
            conf.rgbw_lux_postmul[i] = rgbw_acc / conf.rgbw_max_lux_div[i];
        }
    }

    float row_coe = get(ALS_CALI_DIR "row_coe", 0.f);
    if (row_coe != 0.f) {
        conf.sensor_inverse_gain[0] = row_coe / 1000.f;
    }

    conf.agc_threshold = 800.f / conf.sensor_inverse_gain[0];

    float cali_coe = get(ALS_CALI_DIR "cali_coe", 0.f);
    conf.calib_gain = (cali_coe > 0.f) ? cali_coe / 1000.f : 1.f;

    conf.max_brightness = get(BRIGHTNESS_DIR "max_brightness", 1023.f);

    for (auto& range : hysteresis_ranges) {
        range.min /= conf.calib_gain * conf.sensor_inverse_gain[0];
        range.max /= conf.calib_gain * conf.sensor_inverse_gain[0];
    }
    hysteresis_ranges[0].min = -1.f;

    ALOGI("ALS config reloaded: calib_gain=%.4f max_brightness=%.0f", conf.calib_gain, conf.max_brightness);
}

void AlsCorrection::init() {
    reloadConfig();

    const auto instancename = std::string(IAreaCapture::descriptor) + "/default";

    if (AServiceManager_isDeclared(instancename.c_str())) {
        service = IAreaCapture::fromBinder(::ndk::SpAIBinder(
            AServiceManager_waitForService(instancename.c_str())));
    } else {
        ALOGE("Service %s not registered", instancename.c_str());
    }
}

bool AlsCorrection::process(Event& event) {
    static AreaRgbCaptureResult screenshot = {0.f, 0.f, 0.f};

    ALOGV("Raw sensor reading: %.0f", event.u.scalar);

    if (event.u.scalar > conf.bias) {
        event.u.scalar -= conf.bias;
    }

    nsecs_t now = systemTime(SYSTEM_TIME_BOOTTIME);
    float brightness = get(BRIGHTNESS_DIR "brightness", 0.f);

    {
        std::lock_guard<std::mutex> lock(state_mutex);

        if (state.last_update == 0) {
            state.last_update = now;
            state.last_forced_update = now;
            state.force_update = true;
        } else {
            if (brightness > 0.f && (now - state.last_forced_update) > s2ns(3000)) {
                ALOGV("Forcing screenshot due to timeout");
                state.last_forced_update = now;
                state.force_update = true;
            }
            if ((now - state.last_update) < ms2ns(100)) {
                ALOGV("Events arriving too fast, dropping");
                return false;
            }
            state.last_update = now;
        }
    }

    float sensor_raw_calibrated = event.u.scalar * conf.calib_gain * state.last_agc_gain;

    bool need_update = false;
    {
        std::lock_guard<std::mutex> lock(state_mutex);
        need_update = state.force_update || ((event.u.scalar < state.hyst_min || event.u.scalar > state.hyst_max)
            && (sensor_raw_calibrated < 10.f || sensor_raw_calibrated > (5.f / 0.07f)));
    }

    if (!need_update) {
        std::lock_guard<std::mutex> lock(state_mutex);
        event.u.scalar = state.last_corrected_value;
        ALOGV("Reusing cached corrected value: %.0f", event.u.scalar);
        return true;
    }

    if (service == nullptr || !service->getAreaBrightness(&screenshot).isOk()) {
        ALOGE("Failed to get area brightness from service");
        return false;
    }

    if ((screenshot.r + screenshot.g + screenshot.b) == 0.f) {
        std::lock_guard<std::mutex> lock(state_mutex);
        event.u.scalar = state.last_corrected_value;
        return true;
    }

    ALOGV("Screen color above sensor: R=%.2f G=%.2f B=%.2f", screenshot.r, screenshot.g, screenshot.b);

    float rgbw[4];
    rgbw[static_cast<int>(ColorChannel::RED)] = screenshot.r;
    rgbw[static_cast<int>(ColorChannel::GREEN)] = screenshot.g;
    rgbw[static_cast<int>(ColorChannel::BLUE)] = screenshot.b;
    rgbw[static_cast<int>(ColorChannel::WHITE)] =
        screenshot.r * conf.grayscale_weights[0]
        + screenshot.g * conf.grayscale_weights[1]
        + screenshot.b * conf.grayscale_weights[2];

    float cumulative_correction = 0.f;
    for (int i = 0; i < 4; ++i) {
        float corr = polyEval(conf.rgbw_poly[i], 4, rgbw[i]);
        corr *= conf.rgbw_lux_postmul[i];
        if (i < 3) {
            cumulative_correction += std::max(corr, 0.f);
        } else {
            cumulative_correction -= corr;
        }
    }

    cumulative_correction *= brightness / conf.max_brightness;

    float brightness_fullwhite = conf.rgbw_max_lux[static_cast<int>(ColorChannel::WHITE)] * brightness / conf.max_brightness;
    float brightness_grayscale_gamma = std::pow(rgbw[static_cast<int>(ColorChannel::WHITE)] / 255.f, 2.2f) * brightness_fullwhite;

    cumulative_correction = std::min(cumulative_correction, brightness_fullwhite);
    cumulative_correction = std::max(cumulative_correction, brightness_grayscale_gamma);

    ALOGV("Estimated screen brightness correction: %.0f", cumulative_correction);

    float sensor_raw_corrected = std::max(event.u.scalar - cumulative_correction, 0.f);

    float agc_gain = conf.sensor_inverse_gain[0];
    if (sensor_raw_corrected > conf.agc_threshold) {
        float gain_estimate = 0.f;
        if (conf.hbr) {
            if (event.u.data[2] != 0.f) {
                gain_estimate = event.u.data[2] * 1000.f / sensor_raw_corrected;
            }
        } else {
            if (event.u.data[2] != 0.f) {
                gain_estimate = sensor_raw_corrected / event.u.data[2];
            }
        }

        for (int i = 0; i < 4; ++i) {
            if (gain_estimate > conf.sensor_gaincal_points[i]) {
                agc_gain = conf.sensor_inverse_gain[i];
            }
        }
    }

    ALOGV("AGC gain: %f", agc_gain);

    {
        std::lock_guard<std::mutex> lock(state_mutex);

        if (cumulative_correction <= event.u.scalar * 1.35f
            || event.u.scalar * conf.calib_gain * agc_gain < 10000.f
            || state.force_update) {

            float sensor_corrected = sensor_raw_corrected * conf.calib_gain * agc_gain;

            state.last_agc_gain = agc_gain;

            for (auto& range : hysteresis_ranges) {
                if (sensor_corrected <= range.middle) {
                    state.hyst_min = range.min;
                    state.hyst_max = range.max + brightness_fullwhite;
                    break;
                }
            }

            sensor_corrected = std::max(sensor_corrected - 14.f, 0.f);
            event.u.scalar = sensor_corrected;

            state.last_corrected_value = sensor_corrected;
            state.force_update = false;

            ALOGV("Updated corrected sensor value: %.0f", sensor_corrected);
        } else {
            event.u.scalar = state.last_corrected_value;
            ALOGV("Reusing cached corrected sensor value due to correction check fail: %.0f", event.u.scalar);
        }
    }

    return true;
}

}  // namespace implementation
}  // namespace V2_1
}  // namespace sensors
}  // namespace hardware
}  // namespace android
