#pragma once

#include "mapper4.h"
#include "mapper5.h"

namespace pixel::graphics::mapper {

// TODO: b/384593969: Fix mapper5 selection based on allocator version
template <MetadataType meta>
std::optional<typename metadata::ReturnType<meta>::type> get(buffer_handle_t handle) {
    static bool use_v5 = [&handle]() {
        return v5::get<MetadataType::PIXEL_FORMAT_ALLOCATED>(handle).has_value();
    }();

    if (use_v5) {
        return v5::get<meta>(handle);
    }
    return v4::get<meta>(handle);
}

template <MetadataType meta>
int32_t set(buffer_handle_t handle, typename metadata::ReturnType<meta>::type data) {
    // Using v5::get() just to set the boolean
    static bool use_v5 = [&handle]() {
        return v5::get<MetadataType::PIXEL_FORMAT_ALLOCATED>(handle).has_value();
    }();

    if (use_v5) {
        return v5::set<meta>(handle, data);
    }
    return v4::set<meta>(handle, data);
}
} // namespace pixel::graphics::mapper
