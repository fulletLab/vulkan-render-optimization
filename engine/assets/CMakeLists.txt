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
set(PROJECTUNITY_LIBKTX_TARGET "")
if(PROJECTUNITY_ENABLE_LIBKTX)
    projectunity_resolve_libktx(PROJECTUNITY_LIBKTX_TARGET)
endif()

add_library(projectunity_assets STATIC
    src/AssetCache.cpp
    src/AssetImport.cpp
    src/AssetManager.cpp
    src/GltfAttributeReader.cpp
    src/GltfAttributeReader.hpp
    src/GltfImageLoader.cpp
    src/GltfImageLoader.hpp
    src/GltfNodeTransforms.cpp
    src/GltfNodeTransforms.hpp
    src/GltfSceneObjects.cpp
    src/GltfSceneObjects.hpp
    src/GltfSidecarTextureImport.cpp
    src/GltfSidecarTextureImport.hpp
    src/GltfTextureImport.cpp
    src/GltfTextureImport.hpp
    src/KtxTextureImport.cpp
    src/KtxTextureImport.hpp
    src/KtxTextureImportLibktx.cpp
    src/KtxTextureImportLibktx.hpp
    src/MeshBounds.cpp
    src/MeshBounds.hpp
    src/MeshLodGenerator.cpp
    src/MeshLodGenerator.hpp
    src/MeshPrimitiveChunker.cpp
    src/MeshPrimitiveChunker.hpp
    src/MeshPrimitiveBatcher.cpp
    src/MeshPrimitiveBatcher.hpp
    src/MeshPrimitiveSignature.cpp
    src/MeshPrimitiveSignature.hpp
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

if(PROJECTUNITY_LIBKTX_TARGET)
    target_link_libraries(projectunity_assets PRIVATE ${PROJECTUNITY_LIBKTX_TARGET})
    target_compile_definitions(projectunity_assets PUBLIC PROJECTUNITY_HAS_LIBKTX=1)
endif()

projectunity_configure_target(projectunity_assets)
