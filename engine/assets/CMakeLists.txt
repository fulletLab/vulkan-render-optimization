include(FetchContent)
include(ProjectUnityDependencies)

if(NOT TARGET nlohmann_json::nlohmann_json)
    FetchContent_Declare(nlohmann_json
        GIT_REPOSITORY https://github.com/nlohmann/json.git
        GIT_TAG v3.11.3
        GIT_SHALLOW TRUE
    )
    FetchContent_MakeAvailable(nlohmann_json)
endif()

projectunity_resolve_tinygltf(PROJECTUNITY_TINYGLTF_TARGET)
projectunity_resolve_mikktspace(PROJECTUNITY_MIKKTSPACE_TARGET)
projectunity_resolve_meshoptimizer(PROJECTUNITY_MESHOPTIMIZER_TARGET)

add_library(projectunity_assets STATIC
    src/AssetCache.cpp
    src/AssetImport.cpp
    src/AssetManager.cpp
    src/GltfAttributeReader.cpp
    src/GltfAttributeReader.hpp
    src/GltfNodeTransforms.cpp
    src/GltfNodeTransforms.hpp
    src/GltfSceneObjects.cpp
    src/GltfSceneObjects.hpp
    src/MeshBounds.cpp
    src/MeshBounds.hpp
    src/StbTextureImport.cpp
    src/StbTextureImport.hpp
    src/TinyGltfImplementation.cpp
    include/projectunity/assets/AssetManager.hpp
)

add_library(ProjectUnity::Assets ALIAS projectunity_assets)

target_include_directories(projectunity_assets
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(projectunity_assets
    PUBLIC
        ProjectUnity::Core
        ProjectUnity::Math
    PRIVATE
        ${PROJECTUNITY_TINYGLTF_TARGET}
        ${PROJECTUNITY_MIKKTSPACE_TARGET}
        ${PROJECTUNITY_MESHOPTIMIZER_TARGET}
        nlohmann_json::nlohmann_json
)

projectunity_configure_target(projectunity_assets)
