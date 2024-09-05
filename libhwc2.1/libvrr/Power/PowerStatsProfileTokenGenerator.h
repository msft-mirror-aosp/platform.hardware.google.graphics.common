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
    PowerStatsProfileTokenGenerator();

    std::optional<std::string> generateToken(const std::string& tokenLabel,
                                             PowerStatsProfile* profile);

    std::string generateStateName(PowerStatsProfile* profile);

private:
    // The format of pattern is: ([token label]'delimiter'?)*
    static constexpr std::string_view kPresentDisplayStateResidencyPattern =
            "[mode](:)[width](x)[height](@)[fps]()";

    // The format of pattern is: ([token label]'delimiter'?)*
    static constexpr std::string_view kNonPresentDisplayStateResidencyPattern =
            "[mode](:)[width](x)[height](@)[refreshSource]()";

    static constexpr char kTokenLabelStart = '[';
    static constexpr char kTokenLabelEnd = ']';
    static constexpr char kDelimiterStart = '(';
    static constexpr char kDelimiterEnd = ')';

    bool parseDisplayStateResidencyPattern();

    bool parseResidencyPattern(
            std::vector<std::pair<std::string, std::string>>& residencyPatternMap,
            const std::string_view residencyPattern);

    std::string generateRefreshSourceToken(PowerStatsProfile* profile) const;

    std::string generateModeToken(PowerStatsProfile* profile) const;

    std::string generateWidthToken(PowerStatsProfile* profile) const;

    std::string generateHeightToken(PowerStatsProfile* profile) const;

    std::string generateFpsToken(PowerStatsProfile* profile) const;

    std::vector<std::pair<std::string, std::string>> mNonPresentDisplayStateResidencyPatternList;
    std::vector<std::pair<std::string, std::string>> mPresentDisplayStateResidencyPatternList;
};

} // namespace android::hardware::graphics::composer
