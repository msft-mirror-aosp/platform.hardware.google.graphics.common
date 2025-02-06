#include "pixel-gralloc/utils.h"
#include <log/log.h>
#include <pixel-gralloc/mapper.h>
#include <pixel-gralloc/metadata.h>
#include <ui/GraphicBuffer.h>

using android::GraphicBuffer;
using android::sp;

namespace pixel::graphics::utils {

std::optional<std::vector<uint16_t>> get_stride_alignment(FrameworkFormat format, uint64_t usage,
                                                          uint32_t width, uint32_t height) {
    usage = usage | Usage::PLACEHOLDER_BUFFER;
    auto f = static_cast<android::PixelFormat>(format);
    auto buffer = sp<GraphicBuffer>::make(width, height, f, /*layerCount=*/1, usage);
    if (!buffer) {
        ALOGE("Failed to allocate buffer");
        return std::nullopt;
    }

    return pixel::graphics::mapper::get<MetadataType::STRIDE_ALIGNMENT>(buffer->handle);
}
} // namespace pixel::graphics::utils
