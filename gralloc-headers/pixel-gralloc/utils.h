#pragma once

#include <pixel-gralloc/format.h>
#include <pixel-gralloc/usage.h>

#include <cstdint>
#include <cstring>
#include <optional>
#include <string>
#include <type_traits>
#include <vector>

namespace {

// Trivial type
template <typename T, std::enable_if_t<std::is_trivially_copyable_v<T>, bool> = true>
std::vector<uint8_t> encode_helper(const T& val) {
    auto begin = reinterpret_cast<const uint8_t*>(&val);
    auto end = begin + sizeof(val);
    return {begin, end};
}

// Container type
template <typename Container,
          std::enable_if_t<!std::is_trivially_copyable_v<Container>, bool> = true>
std::vector<uint8_t> encode_helper(const Container& val) {
    // Check comment in decode_helper below
    static_assert(std::is_trivially_copyable_v<typename Container::value_type>,
                  "Can encode only a containers of trivial types currently");

    constexpr auto member_size = sizeof(typename Container::value_type);
    auto n_bytes = member_size * val.size();

    std::vector<uint8_t> out(n_bytes);
    std::memcpy(out.data(), val.data(), n_bytes);

    return out;
}

// Trivial type
template <typename T, std::enable_if_t<std::is_trivially_copyable_v<T>, bool> = true>
std::optional<T> decode_helper(const std::vector<uint8_t>& bytes) {
    T t;

    if (sizeof(t) != bytes.size()) {
        return {};
    }

    std::memcpy(&t, bytes.data(), bytes.size());
    return t;
}

// Container type
template <typename Container,
          std::enable_if_t<!std::is_trivially_copyable_v<Container>, bool> = true>
std::optional<Container> decode_helper(const std::vector<uint8_t>& bytes) {
    Container t;
    size_t member_size = sizeof(typename Container::value_type);

    // NOTE: This can only reconstruct container of trivial types, not a
    // container of non-trivial types. We can either use a standard serializer
    // (like protobuf) or roll one of our own simple ones (like prepending size
    // of the object), but have to be careful about securing such a serializer.
    // But, do we even need that? I do not see any metadata which is either not
    // trivial or a container of trivial type.
    size_t to_copy = bytes.size();
    if (to_copy % member_size != 0) {
        return {};
    }

    size_t members = to_copy / member_size;
    t.resize(members);
    std::memcpy(t.data(), bytes.data(), to_copy);
    return t;
}

} // namespace

namespace pixel::graphics::utils {

// TODO: Setup a fuzzer for encode/decode
template <typename T>
std::vector<uint8_t> encode(const T& val) {
    return encode_helper(val);
}

template <typename T>
std::optional<T> decode(const std::vector<uint8_t>& bytes) {
    return decode_helper<T>(bytes);
}

enum class Compression {
    UNCOMPRESSED,
};

inline Usage get_usage_from_compression(Compression compression) {
    switch (compression) {
        case Compression::UNCOMPRESSED:
            return static_cast<Usage>(Usage::CPU_READ_OFTEN | Usage::CPU_WRITE_OFTEN |
                                      Usage::GPU_TEXTURE | Usage::GPU_RENDER_TARGET |
                                      Usage::COMPOSER_OVERLAY);
    }
}

#define FormatCase(f) \
    case Format::f:   \
        return #f

inline std::string get_string_from_format(Format format) {
    switch (format) {
        FormatCase(UNSPECIFIED);
        FormatCase(RGBA_8888);
        FormatCase(RGBX_8888);
        FormatCase(RGB_888);
        FormatCase(RGB_565);
        FormatCase(BGRA_8888);
        FormatCase(YCBCR_422_SP);
        FormatCase(YCRCB_420_SP);
        FormatCase(YCBCR_422_I);
        FormatCase(RGBA_FP16);
        FormatCase(RAW16);
        FormatCase(BLOB);
        FormatCase(IMPLEMENTATION_DEFINED);
        FormatCase(YCBCR_420_888);
        FormatCase(RAW_OPAQUE);
        FormatCase(RAW10);
        FormatCase(RAW12);
        FormatCase(RGBA_1010102);
        FormatCase(Y8);
        FormatCase(Y16);
        FormatCase(YV12);
        FormatCase(DEPTH_16);
        FormatCase(DEPTH_24);
        FormatCase(DEPTH_24_STENCIL_8);
        FormatCase(DEPTH_32F);
        FormatCase(DEPTH_32F_STENCIL_8);
        FormatCase(STENCIL_8);
        FormatCase(YCBCR_P010);
        FormatCase(HSV_888);
        FormatCase(R_8);
        FormatCase(R_16_UINT);
        FormatCase(RG_1616_UINT);
        FormatCase(RGBA_10101010);

        // Pixel specific formats
        FormatCase(GOOGLE_NV12);
        FormatCase(GOOGLE_R8);

        // Unknown formats
        default:
            return "Unknown";
    }
}

#undef FormatCase

} // namespace pixel::graphics::utils
