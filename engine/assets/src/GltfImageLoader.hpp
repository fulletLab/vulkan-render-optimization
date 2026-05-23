#pragma once

namespace tinygltf {
class TinyGLTF;
}

namespace projectunity::assets::detail {

void configureGltfImageLoader(tinygltf::TinyGLTF& loader);

} // namespace projectunity::assets::detail
