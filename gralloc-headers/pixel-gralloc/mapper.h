#pragma once

#include <android/hardware/graphics/mapper/4.0/IMapper.h>
#include <log/log.h>

#include "format.h"
#include "format_type.h"
#include "metadata.h"
#include "utils.h"

namespace pixel::graphics::mapper {

namespace {

using ::aidl::android::hardware::graphics::common::PlaneLayout;
using android::hardware::graphics::mapper::V4_0::Error;
using android::hardware::graphics::mapper::V4_0::IMapper;
using HidlPixelFormat = ::android::hardware::graphics::common::V1_2::PixelFormat;
using namespace ::pixel::graphics;

android::sp<IMapper> get_mapper() {
    static android::sp<IMapper> mapper = []() {
        auto mapper = IMapper::getService();
        if (!mapper) {
            ALOGE("Failed to get mapper service");
        }
        return mapper;
    }();

    return mapper;
}

} // namespace

template <MetadataType T>
static std::optional<typename metadata::ReturnType<T>::type> get(buffer_handle_t /*handle*/) {
    static_assert(always_false<T>::value, "Unspecialized get is not supported");
    return {};
}

// TODO: Add support for stable-c mapper
#define GET(meta_name)                                                                           \
                                                                                                 \
    template <>                                                                                  \
    [[maybe_unused]] std::optional<typename metadata::ReturnType<MetadataType::meta_name>::type> \
    get<MetadataType::meta_name>(buffer_handle_t handle) {                                       \
        auto mapper = get_mapper();                                                              \
        IMapper::MetadataType type = {                                                           \
                .name = kPixelMetadataTypeName,                                                  \
                .value = static_cast<int64_t>(MetadataType::meta_name),                          \
        };                                                                                       \
                                                                                                 \
        android::hardware::hidl_vec<uint8_t> vec;                                                \
        Error error;                                                                             \
        auto ret = mapper->get(const_cast<native_handle_t*>(handle), type,                       \
                               [&](const auto& tmpError,                                         \
                                   const android::hardware::hidl_vec<uint8_t>& tmpVec) {         \
                                   error = tmpError;                                             \
                                   vec = tmpVec;                                                 \
                               });                                                               \
        if (!ret.isOk()) {                                                                       \
            return {};                                                                           \
        }                                                                                        \
                                                                                                 \
        return utils::decode<metadata::ReturnType<MetadataType::meta_name>::type>(vec);          \
    }

#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wunused-function"

GET(PLANE_DMA_BUFS);
GET(VIDEO_HDR);
GET(VIDEO_ROI);
GET(VIDEO_GMV);

GET(COMPRESSED_PLANE_LAYOUTS);
GET(PIXEL_FORMAT_ALLOCATED);
GET(FORMAT_TYPE);

#pragma clang diagnostic pop

#undef GET

template <MetadataType T>
static Error set(buffer_handle_t /*handle*/, typename metadata::ReturnType<T>::type /*data*/) {
    static_assert(always_false<T>::value, "Unspecialized set is not supported");
    return {};
}

#define SET(meta_name)                                                                        \
    template <>                                                                               \
    [[maybe_unused]] Error                                                                    \
    set<MetadataType::meta_name>(buffer_handle_t handle,                                      \
                                 typename metadata::ReturnType<MetadataType::meta_name>::type \
                                         data) {                                              \
        auto mapper = get_mapper();                                                           \
        auto encoded_data =                                                                   \
                utils::encode<metadata::ReturnType<MetadataType::meta_name>::type>(data);     \
        IMapper::MetadataType type = {                                                        \
                .name = kPixelMetadataTypeName,                                               \
                .value = static_cast<int64_t>(MetadataType::meta_name),                       \
        };                                                                                    \
                                                                                              \
        auto ret = mapper->set(const_cast<native_handle_t*>(handle), type, encoded_data);     \
                                                                                              \
        return ret;                                                                           \
    }

SET(VIDEO_GMV);
#undef SET

} // namespace pixel::graphics::mapper
