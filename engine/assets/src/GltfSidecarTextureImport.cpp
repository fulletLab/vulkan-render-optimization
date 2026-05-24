#include "GltfSidecarTextureImport.hpp"

#include "AssetImportUtils.hpp"
#include "KtxTextureImport.hpp"
#include "StbTextureImport.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <system_error>
#include <utility>

namespace projectunity::assets::detail {
namespace {

constexpr std::size_t kMaxSidecarFiles = 4096;

[[nodiscard]] std::string normalizedName(std::string_view value)
{
    std::string normalized;
    normalized.reserve(value.size());
    for (const auto character : value) {
        const auto byte = static_cast<unsigned char>(character);
        if (std::isalnum(byte)) {
            normalized.push_back(static_cast<char>(std::tolower(byte)));
        }
    }
    return normalized;
}

[[nodiscard]] std::string lowerPathText(const std::filesystem::path& path)
{
    auto text = path.generic_string();
    std::transform(text.begin(), text.end(), text.begin(), [](unsigned char value) {
        return static_cast<char>(std::tolower(value));
    });
    return text;
}

[[nodiscard]] bool supportedTextureExtension(const std::filesystem::path& path)
{
    auto extension = path.extension().string();
    std::transform(extension.begin(), extension.end(), extension.begin(), [](unsigned char value) {
        return static_cast<char>(std::tolower(value));
    });
    return extension == ".png"
        || extension == ".jpg"
        || extension == ".jpeg"
        || extension == ".ktx"
        || extension == ".ktx2";
}

[[nodiscard]] bool likelyBaseColorTexture(const std::filesystem::path& path)
{
    const auto text = lowerPathText(path);
    const std::array reject {
        "normal", "rough", "metal", "height", "bump", "displace", "occlusion", "_ao", "-ao", "spec",
    };
    return std::none_of(reject.begin(), reject.end(), [&text](const char* token) {
        return text.find(token) != std::string::npos;
    });
}

void appendTextureFiles(const std::filesystem::path& directory, std::vector<std::filesystem::path>& output)
{
    std::error_code error;
    if (!std::filesystem::is_directory(directory, error) || output.size() >= kMaxSidecarFiles) {
        return;
    }
    for (const auto& entry : std::filesystem::recursive_directory_iterator(directory, error)) {
        if (error || output.size() >= kMaxSidecarFiles) {
            return;
        }
        if (entry.is_regular_file(error) && supportedTextureExtension(entry.path())) {
            output.push_back(entry.path());
        }
    }
}

[[nodiscard]] std::vector<std::filesystem::path> discoverSidecarTextures(const std::filesystem::path& baseDirectory)
{
    std::vector<std::filesystem::path> files;
    std::error_code error;
    if (!std::filesystem::is_directory(baseDirectory, error)) {
        return files;
    }
    for (const auto& entry : std::filesystem::directory_iterator(baseDirectory, error)) {
        if (error || files.size() >= kMaxSidecarFiles) {
            break;
        }
        if (entry.is_regular_file(error) && supportedTextureExtension(entry.path())) {
            files.push_back(entry.path());
            continue;
        }
        if (entry.is_directory(error) && lowerPathText(entry.path().filename()).find("texture") != std::string::npos) {
            appendTextureFiles(entry.path(), files);
        }
    }
    return files;
}

[[nodiscard]] TextureAsset importSidecarTexture(const std::filesystem::path& path, std::string* errorMessage)
{
    const auto bytes = readBytes(path, errorMessage);
    if (bytes.empty()) {
        return {};
    }
    return isKtxTextureExtension(path)
        ? importKtxTexture(path, bytes, errorMessage)
        : importStbTexture(path, bytes, errorMessage);
}

} // namespace

GltfSidecarTextureCatalog::GltfSidecarTextureCatalog(std::filesystem::path baseDirectory, std::string sourceStem)
    : baseDirectory_(std::move(baseDirectory))
    , sourceStem_(normalizedName(sourceStem))
    , textureFiles_(discoverSidecarTextures(baseDirectory_))
{
}

std::filesystem::path GltfSidecarTextureCatalog::chooseBaseColorTexture(
    std::string_view meshName,
    std::string_view materialName) const
{
    const std::array names {
        normalizedName(meshName),
        normalizedName(materialName),
        sourceStem_,
    };
    const auto exactMatch = std::find_if(textureFiles_.begin(), textureFiles_.end(), [&names](const auto& path) {
        const auto stem = normalizedName(path.stem().string());
        return likelyBaseColorTexture(path)
            && std::any_of(names.begin(), names.end(), [&stem](const auto& name) {
                return !name.empty() && stem == name;
            });
    });
    if (exactMatch != textureFiles_.end()) {
        return *exactMatch;
    }
    const auto namedBaseColor = std::find_if(textureFiles_.begin(), textureFiles_.end(), [&names](const auto& path) {
        const auto stem = normalizedName(path.stem().string());
        const auto text = lowerPathText(path);
        const auto colorMap = text.find("basecolor") != std::string::npos
            || text.find("albedo") != std::string::npos
            || text.find("diffuse") != std::string::npos;
        return colorMap && likelyBaseColorTexture(path)
            && std::any_of(names.begin(), names.end(), [&stem](const auto& name) {
                return !name.empty() && stem.find(name) != std::string::npos;
            });
    });
    if (namedBaseColor != textureFiles_.end()) {
        return *namedBaseColor;
    }
    if (textureFiles_.size() == 1U && likelyBaseColorTexture(textureFiles_.front())) {
        return textureFiles_.front();
    }
    return {};
}

bool GltfSidecarTextureCatalog::applyBaseColorTexture(
    ModelAsset& model,
    MeshPrimitive& primitive,
    std::string_view meshName,
    std::string_view materialName,
    std::string* errorMessage)
{
    if (primitive.materialIndex >= model.materials.size()
        || model.materials[primitive.materialIndex].baseColorTexture.has_value()) {
        return true;
    }
    const auto texturePath = chooseBaseColorTexture(meshName, materialName);
    if (texturePath.empty()) {
        return true;
    }

    const auto key = std::filesystem::absolute(texturePath).generic_string();
    auto textureIt = importedTextureByPath_.find(key);
    if (textureIt == importedTextureByPath_.end()) {
        auto texture = importSidecarTexture(texturePath, errorMessage);
        if (!texture.id.isValid()) {
            return false;
        }
        textureIt = importedTextureByPath_.emplace(key, model.textures.size()).first;
        model.textures.push_back(std::move(texture));
    }

    auto material = model.materials[primitive.materialIndex];
    material.name = material.name.empty() ? texturePath.stem().string() : material.name + " Sidecar";
    material.baseColor = {1.0F, 1.0F, 1.0F, 1.0F};
    material.baseColorTexture = textureIt->second;
    primitive.materialIndex = model.materials.size();
    model.materials.push_back(std::move(material));
    return true;
}

} // namespace projectunity::assets::detail
