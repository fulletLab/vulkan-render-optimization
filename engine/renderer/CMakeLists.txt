find_package(Vulkan REQUIRED)

set(PROJECTUNITY_RENDERER_GENERATED_DIR "${CMAKE_CURRENT_BINARY_DIR}/generated")

function(projectunity_embed_renderer_shader SHADER_FILE SYMBOL OUTPUT_HEADER)
    set(SHADER_SOURCE "${CMAKE_CURRENT_SOURCE_DIR}/shaders/${SHADER_FILE}")
    set(SPIRV_FILE "${PROJECTUNITY_RENDERER_GENERATED_DIR}/${SHADER_FILE}.spv")
    set(GENERATED_HEADER "${PROJECTUNITY_RENDERER_GENERATED_DIR}/${OUTPUT_HEADER}")
    add_custom_command(
        OUTPUT "${SPIRV_FILE}"
        COMMAND ${CMAKE_COMMAND} -E make_directory "${PROJECTUNITY_RENDERER_GENERATED_DIR}"
        COMMAND Vulkan::glslc --target-env=vulkan1.2 -o "${SPIRV_FILE}" "${SHADER_SOURCE}"
        DEPENDS "${SHADER_SOURCE}"
        COMMENT "Compiling renderer shader ${SHADER_FILE}"
        VERBATIM
    )
    add_custom_command(
        OUTPUT "${GENERATED_HEADER}"
        COMMAND ${CMAKE_COMMAND}
            -DINPUT_FILE=${SPIRV_FILE}
            -DOUTPUT_FILE=${GENERATED_HEADER}
            -DSYMBOL_NAME=${SYMBOL}
            -P "${PROJECT_SOURCE_DIR}/cmake/ProjectUnityEmbedSpirv.cmake"
        DEPENDS "${SPIRV_FILE}" "${PROJECT_SOURCE_DIR}/cmake/ProjectUnityEmbedSpirv.cmake"
        COMMENT "Embedding renderer shader ${SHADER_FILE}"
        VERBATIM
    )
    set(PROJECTUNITY_RENDERER_SHADER_HEADERS
        ${PROJECTUNITY_RENDERER_SHADER_HEADERS}
        "${GENERATED_HEADER}"
        PARENT_SCOPE
    )
endfunction()

projectunity_embed_renderer_shader(TexturedMesh.vert kTexturedMeshVertexSpirv TexturedMeshVertexSpv.hpp)
projectunity_embed_renderer_shader(TexturedMesh.frag kTexturedMeshFragmentSpirv TexturedMeshFragmentSpv.hpp)
projectunity_embed_renderer_shader(ShadowDepth.vert kShadowDepthVertexSpirv ShadowDepthVertexSpv.hpp)
projectunity_embed_renderer_shader(ShadowDepth.frag kShadowDepthFragmentSpirv ShadowDepthFragmentSpv.hpp)
projectunity_embed_renderer_shader(EditorColor.vert kEditorColorVertexSpirv EditorColorVertexSpv.hpp)
projectunity_embed_renderer_shader(EditorColor.frag kEditorColorFragmentSpirv EditorColorFragmentSpv.hpp)

add_library(projectunity_renderer STATIC
    src/RenderDrawOrdering.cpp
    src/RenderShadowSetup.cpp
    src/VulkanRenderer.cpp
    src/VulkanColorMesh.cpp
    src/VulkanColorMesh.hpp
    src/VulkanColorPipeline.cpp
    src/VulkanColorPipeline.hpp
    src/VulkanDebugLabels.hpp
    src/VulkanGpuBuffer.cpp
    src/VulkanGpuBuffer.hpp
    src/VulkanFrameData.cpp
    src/VulkanFrameData.hpp
    src/VulkanMeshCache.cpp
    src/VulkanMeshCache.hpp
    src/VulkanMeshPipeline.cpp
    src/VulkanMeshPipeline.hpp
    src/VulkanResourceContext.hpp
    src/VulkanShadowPipeline.cpp
    src/VulkanShadowPipeline.hpp
    src/VulkanSupport.cpp
    src/VulkanSupport.hpp
    src/VulkanPlatform.hpp
    src/VulkanTextureCache.cpp
    src/VulkanTextureCache.hpp
    src/VulkanUploadContext.cpp
    src/VulkanUploadContext.hpp
    src/VulkanViewportTarget.cpp
    src/VulkanViewportTarget.hpp
    ${PROJECTUNITY_RENDERER_SHADER_HEADERS}
    include/projectunity/renderer/IRenderer.hpp
    include/projectunity/renderer/RenderDrawOrdering.hpp
    include/projectunity/renderer/RenderShadowSetup.hpp
    include/projectunity/renderer/RendererTypes.hpp
    include/projectunity/renderer/VulkanRenderer.hpp
    include/projectunity/renderer/ViewportRenderSurface.hpp
)

add_library(ProjectUnity::Renderer ALIAS projectunity_renderer)

target_include_directories(projectunity_renderer
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
    PRIVATE
        ${PROJECTUNITY_RENDERER_GENERATED_DIR}
    SYSTEM PRIVATE
        ${Vulkan_INCLUDE_DIRS}
)

target_link_libraries(projectunity_renderer
    PUBLIC
        ProjectUnity::Assets
        ProjectUnity::Core
    PRIVATE
        Vulkan::Vulkan
)

projectunity_configure_target(projectunity_renderer)
