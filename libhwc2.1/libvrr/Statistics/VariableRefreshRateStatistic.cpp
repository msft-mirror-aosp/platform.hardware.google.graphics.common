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

#include "VariableRefreshRateStatistic.h"

namespace android::hardware::graphics::composer {

VariableRefreshRateStatistic::VariableRefreshRateStatistic(
        CommonDisplayContextProvider* displayContextProvider, EventQueue* eventQueue,
        int maxFrameRate, int maxTeFrequency, int64_t updatePeriodNs)
      : mDisplayContextProvider(displayContextProvider),
        mEventQueue(eventQueue),
        mMaxFrameRate(maxFrameRate),
        mMaxTeFrequency(maxTeFrequency),
        mMinFrameIntervalNs(roundDivide(std::nano::den, static_cast<int64_t>(maxFrameRate))),
        mTeFrequency(maxFrameRate),
        mTeIntervalNs(roundDivide(std::nano::den, static_cast<int64_t>(mTeFrequency))),
        mUpdatePeriodNs(updatePeriodNs) {
    mStartStatisticTimeNs = getBootClockTimeNs();

    // For debugging purposes, this will only be triggered when DEBUG_VRR_STATISTICS is defined.
#ifdef DEBUG_VRR_STATISTICS
    auto configs = mDisplayContextProvider->getDisplayConfigs();
    for (const auto& config : *configs) {
        ALOGI("VariableRefreshRateStatistic: config id = %d : %s", config.first,
              config.second.toString().c_str());
    }
    mUpdateEvent.mEventType = VrrControllerEventType::kStaticticUpdate;
    mUpdateEvent.mFunctor =
            std::move(std::bind(&VariableRefreshRateStatistic::updateStatistic, this));
    mUpdateEvent.mWhenNs = getSteadyClockTimeNs() + mUpdatePeriodNs;
    mEventQueue->mPriorityQueue.emplace(mUpdateEvent);
#endif
    mStatistics[mDisplayRefreshProfile] = DisplayRefreshRecord();
}

uint64_t VariableRefreshRateStatistic::getPowerOffDurationNs() const {
    if (isPowerModeOffNowLocked()) {
        const auto& item = mStatistics.find(mDisplayRefreshProfile);
        if (item == mStatistics.end()) {
            ALOGE("%s We should have inserted power-off item in constructor.", __func__);
            return 0;
        }
        return mPowerOffDurationNs +
                (getBootClockTimeNs() - item->second.mLastTimeStampInBootClockNs);
    } else {
        return mPowerOffDurationNs;
    }
}

uint64_t VariableRefreshRateStatistic::getStartStatisticTimeNs() const {
    return mStartStatisticTimeNs;
}

DisplayRefreshStatistics VariableRefreshRateStatistic::getStatistics() {
    updateIdleStats();
    std::scoped_lock lock(mMutex);
    return mStatistics;
}

DisplayRefreshStatistics VariableRefreshRateStatistic::getUpdatedStatistics() {
    updateIdleStats();
    std::scoped_lock lock(mMutex);
    DisplayRefreshStatistics updatedStatistics;
    for (auto& it : mStatistics) {
        if (it.second.mUpdated) {
            if (it.first.mNumVsync < 0) {
                it.second.mAccumulatedTimeNs = getPowerOffDurationNs();
            }
            updatedStatistics[it.first] = it.second;
            it.second.mUpdated = false;
        }
    }
    if (isPowerModeOffNowLocked()) {
        mStatistics[mDisplayRefreshProfile].mUpdated = true;
    }
    return std::move(updatedStatistics);
}

std::string VariableRefreshRateStatistic::dumpStatistics(RefreshSource refreshSource,
                                                         bool getUpdatedOnly,
                                                         const std::string& delimiter) {
    std::string res;
    updateIdleStats();
    std::scoped_lock lock(mMutex);
    for (auto& it : mStatistics) {
        if ((!getUpdatedOnly) || (it.second.mUpdated)) {
            if (it.first.mRefreshSource & refreshSource) {
                if (it.first.mNumVsync < 0) {
                    it.second.mAccumulatedTimeNs = getPowerOffDurationNs();
                }
                res += "[";
                res += it.first.toString();
                res += " , ";
                res += it.second.toString();
                res += "]";
                res += delimiter;
            }
        }
    }
    return res;
}

void VariableRefreshRateStatistic::onPowerStateChange(int from, int to) {
    if (from == to) {
        return;
    }
    if (mDisplayRefreshProfile.mCurrentDisplayConfig.mPowerMode != from) {
        ALOGE("%s Power mode mismatch between storing state(%d) and actual mode(%d)", __func__,
              mDisplayRefreshProfile.mCurrentDisplayConfig.mPowerMode, from);
    }
    updateIdleStats();
    std::scoped_lock lock(mMutex);
    if (isPowerModeOff(to)) {
        // Currently the for power stats both |HWC_POWER_MODE_OFF| and |HWC_POWER_MODE_DOZE_SUSPEND|
        // are classified as "off" states in power statistics. Consequently,we assign the value of
        // |HWC_POWER_MODE_OFF| to |mPowerMode| when it is |HWC_POWER_MODE_DOZE_SUSPEND|.
        mDisplayRefreshProfile.mCurrentDisplayConfig.mPowerMode = HWC_POWER_MODE_OFF;

        auto& record = mStatistics[mDisplayRefreshProfile];
        ++record.mCount;
        record.mLastTimeStampInBootClockNs = getBootClockTimeNs();
        record.mUpdated = true;

        mLastRefreshTimeInBootClockNs = kDefaultInvalidPresentTimeNs;
    } else {
        if (isPowerModeOff(from)) {
            mPowerOffDurationNs +=
                    (getBootClockTimeNs() -
                     mStatistics[mDisplayRefreshProfile].mLastTimeStampInBootClockNs);
        }
        mDisplayRefreshProfile.mCurrentDisplayConfig.mPowerMode = to;
        if (to == HWC_POWER_MODE_DOZE) {
            mDisplayRefreshProfile.mNumVsync = mTeFrequency;
            auto& record = mStatistics[mDisplayRefreshProfile];
            ++record.mCount;
            record.mLastTimeStampInBootClockNs = getBootClockTimeNs();
            record.mUpdated = true;
        }
    }
}

void VariableRefreshRateStatistic::onPresent(int64_t presentTimeNs, int flag) {
    onRefreshInternal(presentTimeNs, flag, RefreshSource::kRefreshSourceActivePresent);
}

void VariableRefreshRateStatistic::onNonPresentRefresh(int64_t refreshTimeNs,
                                                       RefreshSource refreshSource) {
    onRefreshInternal(refreshTimeNs, 0, refreshSource);
}

void VariableRefreshRateStatistic::onRefreshInternal(int64_t refreshTimeNs, int flag,
                                                     RefreshSource refreshSource) {
    int64_t presentTimeInBootClockNs = steadyClockTimeToBootClockTimeNs(refreshTimeNs);
    if (mLastRefreshTimeInBootClockNs == kDefaultInvalidPresentTimeNs) {
        mLastRefreshTimeInBootClockNs = presentTimeInBootClockNs;
        updateCurrentDisplayStatus();
        // Ignore first refresh after resume
        return;
    }
    updateIdleStats(presentTimeInBootClockNs);
    updateCurrentDisplayStatus();
    if (hasPresentFrameFlag(flag, PresentFrameFlag::kPresentingWhenDoze)) {
        // In low power mode, panel boost to 30 Hz while presenting new frame.
        mDisplayRefreshProfile.mNumVsync = mTeFrequency / kFrameRateWhenPresentAtLpMode;
        mLastRefreshTimeInBootClockNs =
                presentTimeInBootClockNs + (std::nano::den / kFrameRateWhenPresentAtLpMode);
    } else {
        int numVsync = roundDivide((presentTimeInBootClockNs - mLastRefreshTimeInBootClockNs),
                                   mTeIntervalNs);
        // TODO(b/353976456): Implement a scheduler to avoid conflicts between present and
        // non-present refresh. Currently, If a conflict occurs, both present and non-present
        // refresh may request to take effect simultaneously, resulting in a zero duration between
        // them. To address this, we avoid including statistics with zero duration. This issue
        // should be resolved once the scheduler is implemented.
        if (numVsync == 0) return;
        numVsync = std::max(1, std::min(mTeFrequency, numVsync));
        mDisplayRefreshProfile.mNumVsync = numVsync;
        mLastRefreshTimeInBootClockNs = presentTimeInBootClockNs;
        mDisplayRefreshProfile.mRefreshSource = refreshSource;
    }
    {
        std::scoped_lock lock(mMutex);

        auto& record = mStatistics[mDisplayRefreshProfile];
        ++record.mCount;
        record.mAccumulatedTimeNs += (mTeIntervalNs * mDisplayRefreshProfile.mNumVsync);
        record.mLastTimeStampInBootClockNs = presentTimeInBootClockNs;
        record.mUpdated = true;
        if (hasPresentFrameFlag(flag, PresentFrameFlag::kPresentingWhenDoze)) {
            // After presenting a frame in AOD, we revert back to 1 Hz operation.
            mDisplayRefreshProfile.mNumVsync = mTeFrequency;
            auto& record = mStatistics[mDisplayRefreshProfile];
            ++record.mCount;
            record.mLastTimeStampInBootClockNs = mLastRefreshTimeInBootClockNs;
            record.mUpdated = true;
        }
    }
}

void VariableRefreshRateStatistic::setActiveVrrConfiguration(int activeConfigId, int teFrequency) {
    updateIdleStats();
    mDisplayRefreshProfile.mCurrentDisplayConfig.mActiveConfigId = activeConfigId;
    mTeFrequency = teFrequency;
    if (mTeFrequency % mMaxFrameRate != 0) {
        ALOGW("%s TE frequency does not align with the maximum frame rate as a multiplier.",
              __func__);
    }
    mTeIntervalNs = roundDivide(std::nano::den, static_cast<int64_t>(mTeFrequency));
    // TODO(b/333204544): how can we handle the case if mTeFrequency % mMinimumRefreshRate != 0?
    if ((mMinimumRefreshRate > 0) && (mTeFrequency % mMinimumRefreshRate != 0)) {
        ALOGW("%s TE frequency does not align with the lowest frame rate as a multiplier.",
              __func__);
    }
}

void VariableRefreshRateStatistic::setFixedRefreshRate(uint32_t rate) {
    if (mMinimumRefreshRate != rate) {
        updateIdleStats();
        mMinimumRefreshRate = rate;
        if (mMinimumRefreshRate > 1) {
            mMaximumFrameIntervalNs =
                    roundDivide(std::nano::den, static_cast<int64_t>(mMinimumRefreshRate));
            // TODO(b/333204544): how can we handle the case if mTeFrequency % mMinimumRefreshRate
            // != 0?
            if (mTeFrequency % mMinimumRefreshRate != 0) {
                ALOGW("%s TE frequency does not align with the lowest frame rate as a multiplier.",
                      __func__);
            }
        } else {
            mMaximumFrameIntervalNs = kMaxRefreshIntervalNs;
        }
    }
}

bool VariableRefreshRateStatistic::isPowerModeOffNowLocked() const {
    return isPowerModeOff(mDisplayRefreshProfile.mCurrentDisplayConfig.mPowerMode);
}

void VariableRefreshRateStatistic::updateCurrentDisplayStatus() {
    mDisplayRefreshProfile.mCurrentDisplayConfig.mBrightnessMode =
            mDisplayContextProvider->getBrightnessMode();
    if (mDisplayRefreshProfile.mCurrentDisplayConfig.mBrightnessMode ==
        BrightnessMode::kInvalidBrightnessMode) {
        mDisplayRefreshProfile.mCurrentDisplayConfig.mBrightnessMode =
                BrightnessMode::kNormalBrightnessMode;
    }
}

void VariableRefreshRateStatistic::updateIdleStats(int64_t endTimeStampInBootClockNs) {
    if (mDisplayRefreshProfile.isOff()) return;
    if (mLastRefreshTimeInBootClockNs == kDefaultInvalidPresentTimeNs) return;

    endTimeStampInBootClockNs =
            endTimeStampInBootClockNs < 0 ? getBootClockTimeNs() : endTimeStampInBootClockNs;
    auto durationFromLastPresentNs = endTimeStampInBootClockNs - mLastRefreshTimeInBootClockNs;
    durationFromLastPresentNs = durationFromLastPresentNs < 0 ? 0 : durationFromLastPresentNs;
    if (mDisplayRefreshProfile.mCurrentDisplayConfig.mPowerMode == HWC_POWER_MODE_DOZE) {
        mDisplayRefreshProfile.mNumVsync = mTeFrequency;

        std::scoped_lock lock(mMutex);

        auto& record = mStatistics[mDisplayRefreshProfile];
        record.mAccumulatedTimeNs += durationFromLastPresentNs;
        record.mLastTimeStampInBootClockNs = mLastRefreshTimeInBootClockNs;
        mLastRefreshTimeInBootClockNs = endTimeStampInBootClockNs;
        record.mUpdated = true;
    } else {
        int numVsync = roundDivide(durationFromLastPresentNs, mTeIntervalNs);
        mDisplayRefreshProfile.mNumVsync =
                (mMinimumRefreshRate > 1 ? (mTeFrequency / mMinimumRefreshRate) : mTeFrequency);
        if (numVsync <= mDisplayRefreshProfile.mNumVsync) return;

        // Ensure that the last vsync should not be included now, since it would be processed for
        // next update or |onPresent|
        auto count = (numVsync - 1) / mDisplayRefreshProfile.mNumVsync;
        auto alignedDurationNs = mMaximumFrameIntervalNs * count;
        {
            std::scoped_lock lock(mMutex);

            auto& record = mStatistics[mDisplayRefreshProfile];
            record.mCount += count;
            record.mAccumulatedTimeNs += alignedDurationNs;
            mLastRefreshTimeInBootClockNs += alignedDurationNs;
            record.mLastTimeStampInBootClockNs = mLastRefreshTimeInBootClockNs;
            record.mUpdated = true;
        }
    }
}

#ifdef DEBUG_VRR_STATISTICS
int VariableRefreshRateStatistic::updateStatistic() {
    updateIdleStats();
    for (const auto& it : mStatistics) {
        const auto& key = it.first;
        const auto& value = it.second;
        ALOGD("%s: power mode = %d, id = %d, birghtness mode = %d, vsync "
              "= %d : count = %ld, last entry time =  %ld",
              __func__, key.mCurrentDisplayConfig.mPowerMode,
              key.mCurrentDisplayConfig.mActiveConfigId, key.mCurrentDisplayConfig.mBrightnessMode,
              key.mNumVsync, value.mCount, value.mLastTimeStampInBootClockNs);
    }
    // Post next update statistics event.
    mUpdateEvent.mWhenNs = getSteadyClockTimeNs() + mUpdatePeriodNs;
    mEventQueue->mPriorityQueue.emplace(mUpdateEvent);

    return NO_ERROR;
}
#endif

} // namespace android::hardware::graphics::composer
