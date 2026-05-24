#include <projectunity/assets/AssetManager.hpp>

#include "AssetSidecarFixture.hpp"

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <iostream>

namespace {

int fail(const char* message)
{
    std::cerr << message << '\n';
    return EXIT_FAILURE;
}

} // namespace

int main()
{
    using namespace projectunity::assets;

    const auto root = std::filesystem::temp_directory_path() / "projectunity_sidecar_texture_tests";
    std::error_code errorCode;
    std::filesystem::remove_all(root, errorCode);
    std::filesystem::create_directories(root, errorCode);
    if (errorCode || !projectunity::asset_tests::writeSidecarTextureFixture(root)) {
        return fail("unable to write sidecar texture fixture");
    }

    AssetManager manager(root / "Cache" / "Assets");
    const auto result = manager.importModel(root / "sidecar.gltf");
    const auto model = manager.model(result.record.id);
    auto boundSidecarTexture = false;
    if (model != nullptr) {
        const auto material = std::find_if(model->materials.begin(), model->materials.end(), [](const MaterialAsset& candidate) {
            return candidate.baseColorTexture.has_value();
        });
        boundSidecarTexture = material != model->materials.end()
            && *material->baseColorTexture < model->textures.size()
            && !model->textures[*material->baseColorTexture].rgba8.empty();
    }
    std::filesystem::remove_all(root, errorCode);
    if (!result.success || model == nullptr || !boundSidecarTexture) {
        std::cerr << result.error << '\n';
        return fail("glTF sidecar base-color texture was not bound to the matching mesh");
    }
    return EXIT_SUCCESS;
}
