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

#pragma once

#include <optional>
#include <string>

#include "../display/common/CommonDisplayContextProvider.h"
#include "PowerStatsProfile.h"

namespace android::hardware::graphics::composer {

class PowerStatsProfileTokenGenerator {
public:
    PowerStatsProfileTokenGenerator() = default;

    void setPowerStatsProfile(const PowerStatsProfile* powerStatsProfile) {
        mPowerStatsProfile = powerStatsProfile;
    }

    std::optional<std::string> generateToken(const std::string& tokenLabel);

private:
    std::string generateRefreshSourceToken() const;

    std::string generateModeToken() const;

    std::string generateWidthToken() const;

    std::string generateHeightToken() const;

    std::string generateFpsToken() const;

    const PowerStatsProfile* mPowerStatsProfile;
};

} // namespace android::hardware::graphics::composer
