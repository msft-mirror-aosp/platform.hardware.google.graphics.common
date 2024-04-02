/*
 * Copyright (C) 2024 The Android Open Source Project
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

#ifndef _DISPLAY_TE2_MANAGER_H_
#define _DISPLAY_TE2_MANAGER_H_

#include "ExynosDisplay.h"
#include "ExynosHWCHelper.h"

// TODO: Rename this and integrate with refresh rate throttler related features into this class.
class DisplayTe2Manager {
public:
    DisplayTe2Manager(ExynosDisplay* display, int32_t panelIndex, int fixedTe2DefaultRateHz);
    ~DisplayTe2Manager() = default;

    // Set the rate while option is fixed TE2. This should be set by the sensor.
    int32_t setFixedTe2Rate(int targetTe2RateHz);
    // Set the rate while option is changeable TE2. This should be set by the composer
    // while the display state is idle or active.
    int32_t setChangeableTe2Rate(int targetTe2RateHz);
    // Update TE2 option to either fixed or changeable according to the proximity sensor state.
    // Ideally we should use changeable TE2 if the proximity sensor is active. Also set the min
    // refresh rate of fixed TE2. It equals to the refresh rate while display is idle after
    // switching to changeable TE2, and we can use it for the notification of refresh rate
    // change.
    void updateTe2Option(bool proximityActive, int minRefreshRate);
    bool isOptionFixedTe2() { return mIsOptionFixedTe2; }

    void dump(String8& result) const;

private:
    const char* getPanelString() {
        return (mPanelIndex == 0 ? "primary" : mPanelIndex == 1 ? "secondary" : "unknown");
    }

    const String8 getPanelTe2RatePath() {
        return String8::format(kTe2RateFileNode, getPanelString());
    }

    const String8 getPanelTe2OptionPath() {
        return String8::format(kTe2OptionFileNode, getPanelString());
    }

    int32_t setTe2Rate(int targetTe2RateHz);
    int32_t setFixedTe2RateInternal(int targetTe2RateHz, bool enforce);

    ExynosDisplay* mDisplay;
    int32_t mPanelIndex;
    // The min refresh rate of fixed TE2. For the refresh rates lower than this, the changeable
    // TE2 should be used.
    int mMinRefreshRateForFixedTe2;
    int mFixedTe2RateHz;
    // True when the current option is fixed TE2, otherwise it's changeable TE2.
    bool mIsOptionFixedTe2;

    static constexpr const char* kTe2RateFileNode =
            "/sys/devices/platform/exynos-drm/%s-panel/te2_rate_hz";
    static constexpr const char* kTe2OptionFileNode =
            "/sys/devices/platform/exynos-drm/%s-panel/te2_option";
};

#endif // _DISPLAY_TE2_MANAGER_H_
