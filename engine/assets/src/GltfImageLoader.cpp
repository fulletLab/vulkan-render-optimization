#include "GltfImageLoader.hpp"

#include <tiny_gltf.h>

#include <algorithm>
#include <array>

namespace projectunity::assets::detail {
namespace {

constexpr std::array<unsigned char, 12> kKtx1Identifier {{
    0xAB, 0x4B, 0x54, 0x58, 0x20, 0x31, 0x31, 0xBB, 0x0D, 0x0A, 0x1A, 0x0A,
}};
constexpr std::array<unsigned char, 12> kKtx2Identifier {{
    0xAB, 0x4B, 0x54, 0x58, 0x20, 0x32, 0x30, 0xBB, 0x0D, 0x0A, 0x1A, 0x0A,
}};

[[nodiscard]] bool hasIdentifier(const unsigned char* bytes, int size, const std::array<unsigned char, 12>& identifier)
{
    return bytes != nullptr
        && size >= static_cast<int>(identifier.size())
        && std::equal(identifier.begin(), identifier.end(), bytes);
}

bool loadImageKeepingEncodedBytes(
    tinygltf::Image* image,
    int imageIndex,
    std::string* error,
    std::string* warning,
    int requestedWidth,
    int requestedHeight,
    const unsigned char* bytes,
    int size,
    void* userData)
{
    (void)imageIndex;
    (void)error;
    (void)warning;
    (void)requestedWidth;
    (void)requestedHeight;
    (void)userData;

    if (bytes == nullptr || size <= 0) {
        return false;
    }
    if (image != nullptr && image->bufferView >= 0) {
        image->width = 0;
        image->height = 0;
        image->component = 0;
        image->bits = 8;
        return true;
    }

    if (hasIdentifier(bytes, size, kKtx1Identifier) || hasIdentifier(bytes, size, kKtx2Identifier)) {
        image->image.assign(bytes, bytes + size);
        image->width = 0;
        image->height = 0;
        image->component = 0;
        image->bits = 8;
        return true;
    }

    image->image.assign(bytes, bytes + size);
    image->width = 0;
    image->height = 0;
    image->component = 0;
    image->bits = 8;
    return true;
}

} // namespace

void configureGltfImageLoader(tinygltf::TinyGLTF& loader)
{
    loader.SetImageLoader(loadImageKeepingEncodedBytes, nullptr);
}

} // namespace projectunity::assets::detail
