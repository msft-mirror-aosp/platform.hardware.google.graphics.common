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

#include <stdlib.h>

#include "DisplayStateResidencyProvider.h"

namespace android::hardware::graphics::composer {

// Currently, the FPS ranges from [1, |kMaxFrameRate| = 120], and the maximum TE
// frequency(|kMaxTefrequency|) = 240. We express fps by dividing the maximum TE by the number of
// vsync. Here, the numerator is set to |kMaxTefrequency|, fraction reduction is not needed here.
const std::set<Fraction<int>> DisplayStateResidencyProvider::kFpsMappingTable =
        {{240, 240}, {240, 120}, {240, 24}, {240, 10}, {240, 8}, {240, 7},
         {240, 6},   {240, 5},   {240, 4},  {240, 3},  {240, 2}};

const std::vector<int> DisplayStateResidencyProvider::kFpsLowPowerModeMappingTable = {1, 30};

const std::vector<int> DisplayStateResidencyProvider::kActivePowerModes = {HWC2_POWER_MODE_DOZE,
                                                                           HWC2_POWER_MODE_ON};

const std::vector<RefreshSource> DisplayStateResidencyProvider::kRefreshSource =
        {kRefreshSourceActivePresent, kRefreshSourceIdlePresent, kRefreshSourceFrameInsertion,
         kRefreshSourceBrightness};

namespace {

static constexpr uint64_t MilliToNano = 1000000;

}

DisplayStateResidencyProvider::DisplayStateResidencyProvider(
        std::shared_ptr<CommonDisplayContextProvider> displayContextProvider,
        std::shared_ptr<StatisticsProvider> statisticsProvider)
      : mDisplayContextProvider(displayContextProvider), mStatisticsProvider(statisticsProvider) {
    if (parseDisplayStateResidencyPattern()) {
        generatePowerStatsStates();
    }
    mStartStatisticTimeNs = mStatisticsProvider->getStartStatisticTimeNs();
}

void DisplayStateResidencyProvider::getStateResidency(std::vector<StateResidency>* stats) {
    mapStatistics();

    int64_t powerStatsTotalTimeNs = aggregateStatistics();
#ifdef DEBUG_VRR_POWERSTATS
    uint64_t statisticDurationNs = getBootClockTimeNs() - mStartStatisticTimeNs;
    ALOGD("DisplayStateResidencyProvider: total power stats time = %ld ms, time lapse = %ld ms",
          powerStatsTotalTimeNs / MilliToNano, statisticDurationNs / MilliToNano);
    if (mLastGetStateResidencyTimeNs != -1) {
        int64_t timePassedNs = (getSteadyClockTimeNs() - mLastGetStateResidencyTimeNs);
        int64_t statisticAccumulatedTimeNs = (powerStatsTotalTimeNs - mLastPowerStatsTotalTimeNs);
        ALOGD("DisplayStateResidencyProvider: The time interval between successive calls to "
              "getStateResidency() = %ld ms",
              (timePassedNs / MilliToNano));
        ALOGD("DisplayStateResidencyProvider: The accumulated statistic time interval between "
              "successive calls to "
              "getStateResidency() = %ld ms",
              (statisticAccumulatedTimeNs / MilliToNano));
    }
    mLastGetStateResidencyTimeNs = getSteadyClockTimeNs();
    mLastPowerStatsTotalTimeNs = powerStatsTotalTimeNs;
#endif
    *stats = mStateResidency;
}

const std::vector<State>& DisplayStateResidencyProvider::getStates() {
    return mStates;
}

void DisplayStateResidencyProvider::mapStatistics() {
    auto mUpdatedStatistics = mStatisticsProvider->getUpdatedStatistics();
#ifdef DEBUG_VRR_POWERSTATS
    for (const auto& item : mUpdatedStatistics) {
        ALOGI("DisplayStateResidencyProvider : update key %s value %s",
              item.first.toString().c_str(), item.second.toString().c_str());
    }
#endif
    mRemappedStatistics.clear();
    for (const auto& item : mUpdatedStatistics) {
        mStatistics[item.first] = item.second;
    }

    for (const auto& item : mStatistics) {
        const auto& displayPresentProfile = item.first;
        PowerStatsProfile powerStatsProfile;
        if (displayPresentProfile.mNumVsync <
            0) { // To address the specific scenario of powering off.
            powerStatsProfile.mFps = -1;
            mRemappedStatistics[powerStatsProfile] += item.second;
            mRemappedStatistics[powerStatsProfile].mUpdated = true;
            continue;
        }
        const auto& configId = displayPresentProfile.mCurrentDisplayConfig.mActiveConfigId;
        powerStatsProfile.mWidth = mDisplayContextProvider->getWidth(configId);
        powerStatsProfile.mHeight = mDisplayContextProvider->getHeight(configId);
        powerStatsProfile.mPowerMode = displayPresentProfile.mCurrentDisplayConfig.mPowerMode;
        powerStatsProfile.mBrightnessMode =
                displayPresentProfile.mCurrentDisplayConfig.mBrightnessMode;
        powerStatsProfile.mRefreshSource = displayPresentProfile.mRefreshSource;

        auto teFrequency = mDisplayContextProvider->getTeFrequency(configId);
        Fraction fps(teFrequency, displayPresentProfile.mNumVsync);
        if ((kFpsMappingTable.count(fps) > 0)) {
            powerStatsProfile.mFps = fps.round();
            mRemappedStatistics[powerStatsProfile] += item.second;
            mRemappedStatistics[powerStatsProfile].mUpdated = true;
        } else {
            // Others.
            auto key = powerStatsProfile;
            const auto& value = item.second;
            key.mFps = 0;
            mRemappedStatistics[key].mUpdated = true;
            mRemappedStatistics[key].mCount += value.mCount;
            mRemappedStatistics[key].mAccumulatedTimeNs += value.mAccumulatedTimeNs;
            mRemappedStatistics[key].mLastTimeStampInBootClockNs =
                    std::max(mRemappedStatistics[key].mLastTimeStampInBootClockNs,
                             value.mLastTimeStampInBootClockNs);
        }
    }
}

uint64_t DisplayStateResidencyProvider::aggregateStatistics() {
    uint64_t totalTimeNs = 0;
    std::set<int> firstIteration;
    for (auto& statistic : mRemappedStatistics) {
        if (!statistic.second.mUpdated) {
            continue;
        }
        auto it = mPowerStatsProfileToIdMap.find(statistic.first);
        if (it == mPowerStatsProfileToIdMap.end()) {
            ALOGE("DisplayStateResidencyProvider %s(): unregistered powerstats state [%s]",
                  __func__, statistic.first.toString().c_str());
            continue;
        }
        int id = it->second;
        const auto& displayPresentRecord = statistic.second;

        auto& stateResidency = mStateResidency[id];
        if (firstIteration.count(id) > 0) {
            stateResidency.totalStateEntryCount += displayPresentRecord.mCount;
            stateResidency.lastEntryTimestampMs =
                    displayPresentRecord.mLastTimeStampInBootClockNs / MilliToNano;
            stateResidency.totalTimeInStateMs +=
                    displayPresentRecord.mAccumulatedTimeNs / MilliToNano;
        } else {
            stateResidency.totalStateEntryCount = displayPresentRecord.mCount;
            stateResidency.lastEntryTimestampMs =
                    displayPresentRecord.mLastTimeStampInBootClockNs / MilliToNano;
            stateResidency.totalTimeInStateMs =
                    displayPresentRecord.mAccumulatedTimeNs / MilliToNano;
            firstIteration.insert(id);
        }

        statistic.second.mUpdated = false;
        totalTimeNs += displayPresentRecord.mAccumulatedTimeNs;
    }
    return totalTimeNs;
}

std::string DisplayStateResidencyProvider::generateStateName(PowerStatsProfile* profile) {
    mPowerStatsProfileTokenGenerator.setPowerStatsProfile(profile);
    std::string stateName;

    const std::vector<std::pair<std::string, std::string>>& residencyPattern =
            (!isPresentRefresh(profile->mRefreshSource)) ? mNonPresentDisplayStateResidencyPattern
                                                         : mPresentDisplayStateResidencyPattern;

    for (const auto& pattern : residencyPattern) {
        const auto token = mPowerStatsProfileTokenGenerator.generateToken(pattern.first);
        if (token.has_value()) {
            stateName += token.value();
            if (pattern.first == "mode" && token.value() == "OFF") {
                break;
            }
        } else {
            ALOGE("DisplayStateResidencyProvider %s(): cannot find token with label %s", __func__,
                  pattern.first.c_str());
            continue;
        }
        stateName += pattern.second;
    }

    return stateName;
}

void DisplayStateResidencyProvider::generateUniqueStates() {
    auto configs = mDisplayContextProvider->getDisplayConfigs();
    if (!configs) return; // Early return if no configs

    // Special case: Power mode OFF
    mUniqueStates.emplace(PowerStatsProfile{.mPowerMode = HWC2_POWER_MODE_OFF}, "OFF");

    // Iterate through all combinations
    for (auto refreshSource : kRefreshSource) {
        for (auto powerMode : kActivePowerModes) {
            // LPM and NP is not possible. skipping
            if (!isPresentRefresh(refreshSource) && powerMode == HWC2_POWER_MODE_DOZE) {
                continue;
            }
            for (const auto& config : *configs) {
                for (int brightnessMode = static_cast<int>(BrightnessMode::kNormalBrightnessMode);
                     brightnessMode < static_cast<int>(BrightnessMode::kInvalidBrightnessMode);
                     ++brightnessMode) {
                    PowerStatsProfile
                            profile{.mWidth = mDisplayContextProvider->getWidth(config.first),
                                    .mHeight = mDisplayContextProvider->getHeight(config.first),
                                    .mFps = 0, // Initially set to 0
                                    .mPowerMode = powerMode,
                                    .mBrightnessMode = static_cast<BrightnessMode>(brightnessMode),
                                    .mRefreshSource = refreshSource};

                    if (powerMode == HWC_POWER_MODE_DOZE) {
                        for (auto fps : kFpsLowPowerModeMappingTable) {
                            profile.mFps = fps;
                            mUniqueStates.emplace(profile, generateStateName(&profile));
                        }
                    } else {
                        mUniqueStates.emplace(profile, generateStateName(&profile));
                        for (auto fps : kFpsMappingTable) {
                            profile.mFps = fps.round();
                            mUniqueStates.emplace(profile, generateStateName(&profile));
                        }
                    }
                }
            }
        }
    }
}

void DisplayStateResidencyProvider::generatePowerStatsStates() {
    generateUniqueStates();

    // Sort and assign a unique identifier to each state string.
    std::map<std::string, int> stateNameIDMap;
    int index = 0;
    for (const auto& state : mUniqueStates) {
        auto it = stateNameIDMap.find(state.second);
        int id = index;
        // If the stateName already exists, update mPowerStatsProfileToIdMap, and skip
        // updating mStates/Residency
        if (it != stateNameIDMap.end()) {
            id = it->second;
        } else {
            stateNameIDMap.insert({state.second, id});
            index++;
            mStates.push_back({id, state.second});
            mStateResidency.emplace_back();
            mStateResidency.back().id = id;
        }
        mPowerStatsProfileToIdMap[state.first] = id;
    }

#ifdef DEBUG_VRR_POWERSTATS
    for (const auto& state : mStates) {
        ALOGI("DisplayStateResidencyProvider state id = %d, content = %s, len = %ld", state.id,
              state.name.c_str(), state.name.length());
    }
#endif
}

bool DisplayStateResidencyProvider::parseResidencyPattern(
        std::vector<std::pair<std::string, std::string>>& mResidencyPattern,
        const std::string_view residencyPattern) {
    size_t start, end;
    start = 0;
    end = -1;
    while (true) {
        start = residencyPattern.find_first_of(kTokenLabelStart, end + 1);
        if (start == std::string::npos) {
            break;
        }
        ++start;
        end = residencyPattern.find_first_of(kTokenLabelEnd, start);
        if (end == std::string::npos) {
            break;
        }
        std::string tokenLabel(residencyPattern.substr(start, end - start));

        start = residencyPattern.find_first_of(kDelimiterStart, end + 1);
        if (start == std::string::npos) {
            break;
        }
        ++start;
        end = residencyPattern.find_first_of(kDelimiterEnd, start);
        if (end == std::string::npos) {
            break;
        }
        std::string delimiter(residencyPattern.substr(start, end - start));
        mResidencyPattern.emplace_back(std::make_pair(tokenLabel, delimiter));
    }
    return (end == residencyPattern.length() - 1);
}

bool DisplayStateResidencyProvider::parseDisplayStateResidencyPattern() {
    return parseResidencyPattern(mPresentDisplayStateResidencyPattern,
                                 kPresentDisplayStateResidencyPattern) &&
            parseResidencyPattern(mNonPresentDisplayStateResidencyPattern,
                                  kNonPresentDisplayStateResidencyPattern);
}

} // namespace android::hardware::graphics::composer
