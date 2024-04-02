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

#include "DisplayTe2Manager.h"

DisplayTe2Manager::DisplayTe2Manager(ExynosDisplay* display, int32_t panelIndex,
                                     int fixedTe2DefaultRateHz)
      : mDisplay(display),
        mPanelIndex(panelIndex),
        mMinRefreshRateForFixedTe2(0),
        mFixedTe2RateHz(fixedTe2DefaultRateHz),
        mIsOptionFixedTe2(true) {}

int32_t DisplayTe2Manager::setTe2Rate(int targetTe2RateHz) {
    int32_t ret = writeIntToFile(getPanelTe2RatePath(), targetTe2RateHz);
    if (!ret) {
        ALOGI("DisplayTe2Manager::%s writes te2_rate_hz(%d) to the sysfs node", __func__,
              targetTe2RateHz);
    } else {
        ALOGW("DisplayTe2Manager::%s failed to write te2_rate_hz(%d) to the sysfs node", __func__,
              targetTe2RateHz);
    }
    return ret;
}

int32_t DisplayTe2Manager::setFixedTe2Rate(int targetTe2RateHz) {
    return setFixedTe2RateInternal(targetTe2RateHz, false);
}

int32_t DisplayTe2Manager::setFixedTe2RateInternal(int targetTe2RateHz, bool enforce) {
    if (!mIsOptionFixedTe2) {
        ALOGW("DisplayTe2Manager::%s current option is not fixed TE2", __func__);
        return -EINVAL;
    }
    if (targetTe2RateHz == mFixedTe2RateHz && !enforce) {
        return NO_ERROR;
    } else {
        int32_t ret = setTe2Rate(targetTe2RateHz);
        if (!ret) mFixedTe2RateHz = targetTe2RateHz;
        return ret;
    }
}

int32_t DisplayTe2Manager::setChangeableTe2Rate(int targetTe2RateHz) {
    if (mIsOptionFixedTe2) {
        ALOGW("DisplayTe2Manager::%s current option is not changeable", __func__);
        return -EINVAL;
    }
    if (!mDisplay) {
        ALOGW("DisplayTe2Manager::%s unable to get peak refresh rate", __func__);
        return -EINVAL;
    }

    // While the proximity sensor is active, changeable TE2 should be used. In this case, it
    // should have the tolerance to receive only min (idle) and peak (active) notifications of
    // refresh rate changes and ignore the intermediate values.
    if (targetTe2RateHz == mMinRefreshRateForFixedTe2 ||
        targetTe2RateHz == mDisplay->getPeakRefreshRate()) {
        return setTe2Rate(targetTe2RateHz);
    } else {
        return NO_ERROR;
    }
}

void DisplayTe2Manager::updateTe2Option(bool proximityActive, int minRefreshRate) {
    bool isOptionFixed = !proximityActive;
    // update the min refresh rate for changeable TE2 usage
    if (minRefreshRate) mMinRefreshRateForFixedTe2 = minRefreshRate;
    if (isOptionFixed == mIsOptionFixedTe2) return;

    int32_t ret = writeIntToFile(getPanelTe2OptionPath(), isOptionFixed);
    if (!ret) {
        ALOGI("DisplayTe2Manager::%s writes te2_option(%d) to the sysfs node", __func__,
              isOptionFixed);
        mIsOptionFixedTe2 = isOptionFixed;
        if (isOptionFixed) {
            setFixedTe2RateInternal(mFixedTe2RateHz, true);
        }
    } else {
        ALOGW("DisplayTe2Manager::%s failed to write te2_option(%d) to the sysfs node", __func__,
              isOptionFixed);
    }
}

void DisplayTe2Manager::dump(String8& result) const {
    result.appendFormat("DisplayTe2Manager:\n");
    result.appendFormat("\tmin refresh rate for fixed TE2: %d\n", mMinRefreshRateForFixedTe2);
    if (!mIsOptionFixedTe2) {
        result.appendFormat("\tcurrent TE2: changeable\n");
    } else {
        result.appendFormat("\tcurrent TE2: fixed %d Hz\n", mFixedTe2RateHz);
    }
    result.appendFormat("\n");
}
