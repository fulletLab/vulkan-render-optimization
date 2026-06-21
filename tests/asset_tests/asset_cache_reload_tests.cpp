#include <projectunity/assets/AssetManager.hpp>

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
    using namespace projectunity;

    const auto cacheRoot = std::filesystem::temp_directory_path()
        / "projectunity_asset_cache_reload_test"
        / "Cache"
        / "Assets";
    std::error_code errorCode;
    std::filesystem::remove_all(cacheRoot.parent_path().parent_path(), errorCode);

    const auto sourcePath = std::filesystem::path(PROJECTUNITY_SOURCE_DIR)
        / "examples"
        / "basic_assets"
        / "TexturedTriangle.gltf";

    assets::AssetManager writer(cacheRoot);
    const auto imported = writer.importModel(sourcePath);
    if (!imported.success || !imported.record.id.isValid()) {
        std::cerr << imported.error << '\n';
        return fail("fixture import failed");
    }

    assets::AssetManager reader(cacheRoot);
    const auto records = reader.records();
    bool foundRecord = false;
    for (const auto& record : records) {
        foundRecord = foundRecord || record.id == imported.record.id;
    }
    if (!foundRecord) {
        return fail("asset cache records were not loaded on manager construction");
    }

    const auto model = reader.model(imported.record.id);
    if (model == nullptr || model->id != imported.record.id || model->primitives.empty()) {
        return fail("cached model was not rehydrated lazily from ffult");
    }

    return EXIT_SUCCESS;
}
