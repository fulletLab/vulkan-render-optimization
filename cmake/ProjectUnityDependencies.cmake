include(FetchContent)

set(PROJECTUNITY_CMAKE_DIR "${CMAKE_CURRENT_LIST_DIR}")

function(projectunity_patch_qtads qtads_source_dir)
    configure_file(
        "${PROJECTUNITY_CMAKE_DIR}/QtAdsVersioning.cmake"
        "${qtads_source_dir}/cmake/modules/Versioning.cmake"
        COPYONLY
    )
endfunction()

function(projectunity_resolve_qtads out_target)
    find_package(ads CONFIG QUIET)

    set(_ads_candidates
        ads::qtadvanceddocking
        ads::qtadvanceddocking-qt6
        qtadvanceddocking-qt6
        qtadvanceddocking
        qt6advanceddocking
    )

    foreach(_candidate IN LISTS _ads_candidates)
        if(TARGET ${_candidate})
            set(${out_target} ${_candidate} PARENT_SCOPE)
            return()
        endif()
    endforeach()

    if(NOT PROJECTUNITY_FETCH_QTADS)
        message(FATAL_ERROR
            "Qt Advanced Docking System was not found. Install ADS, provide its CMake package, "
            "or configure with -DPROJECTUNITY_FETCH_QTADS=ON."
        )
    endif()

    message(STATUS "Fetching Qt Advanced Docking System 4.5.0 (LGPL-2.1).")
    FetchContent_Declare(qtads
        GIT_REPOSITORY https://github.com/githubuser0xFFFF/Qt-Advanced-Docking-System.git
        GIT_TAG 4.5.0
        GIT_SHALLOW TRUE
    )

    set(ADS_BUILD_EXAMPLES OFF CACHE BOOL "Build ADS examples" FORCE)
    set(ADS_BUILD_STATIC OFF CACHE BOOL "Build ADS as a static library" FORCE)
    set(BUILD_EXAMPLES OFF CACHE BOOL "Build ADS examples" FORCE)
    set(BUILD_STATIC OFF CACHE BOOL "Build ADS as a static library" FORCE)
    set(ADS_VERSION 4.5.0 CACHE STRING "Qt Advanced Docking System version" FORCE)
    list(PREPEND CMAKE_MODULE_PATH "${CMAKE_BINARY_DIR}/_deps/qtads-src/cmake/modules")

    FetchContent_GetProperties(qtads)
    if(NOT qtads_POPULATED)
        if(POLICY CMP0169)
            cmake_policy(PUSH)
            cmake_policy(SET CMP0169 OLD)
        endif()
        FetchContent_Populate(qtads)
        if(POLICY CMP0169)
            cmake_policy(POP)
        endif()
    endif()

    projectunity_patch_qtads("${qtads_SOURCE_DIR}")
    add_subdirectory("${qtads_SOURCE_DIR}" "${qtads_BINARY_DIR}")

    foreach(_candidate IN LISTS _ads_candidates)
        if(TARGET ${_candidate})
            set(${out_target} ${_candidate} PARENT_SCOPE)
            return()
        endif()
    endforeach()

    message(FATAL_ERROR "Qt Advanced Docking System was fetched, but no known CMake target was created.")
endfunction()

function(projectunity_resolve_tinygizmo out_target)
    if(TARGET ProjectUnity::TinyGizmo)
        set(${out_target} ProjectUnity::TinyGizmo PARENT_SCOPE)
        return()
    endif()

    if(NOT PROJECTUNITY_FETCH_TINYGIZMO)
        message(FATAL_ERROR
            "tinygizmo was not resolved. Configure with -DPROJECTUNITY_FETCH_TINYGIZMO=ON "
            "or provide a ProjectUnity::TinyGizmo target before editor viewport configuration."
        )
    endif()

    message(STATUS "Fetching tinygizmo at 99c1c418d169774b0b8052b57fd1680b4c4de444 (Unlicense).")
    FetchContent_Declare(tinygizmo
        GIT_REPOSITORY https://github.com/ddiakopoulos/tinygizmo.git
        GIT_TAG 99c1c418d169774b0b8052b57fd1680b4c4de444
    )
    FetchContent_MakeAvailable(tinygizmo)

    add_library(projectunity_tinygizmo STATIC
        "${tinygizmo_SOURCE_DIR}/tiny-gizmo.cpp"
        "${tinygizmo_SOURCE_DIR}/tiny-gizmo.hpp"
    )
    add_library(ProjectUnity::TinyGizmo ALIAS projectunity_tinygizmo)

    target_include_directories(projectunity_tinygizmo
        SYSTEM PUBLIC
            "${tinygizmo_SOURCE_DIR}"
    )

    projectunity_configure_target(projectunity_tinygizmo)
    if(MSVC)
        target_compile_options(projectunity_tinygizmo PRIVATE /W0)
    else()
        target_compile_options(projectunity_tinygizmo PRIVATE -w)
    endif()
    set(${out_target} ProjectUnity::TinyGizmo PARENT_SCOPE)
endfunction()

function(projectunity_resolve_im3d out_target)
    if(TARGET ProjectUnity::Im3d)
        set(${out_target} ProjectUnity::Im3d PARENT_SCOPE)
        return()
    endif()

    if(NOT PROJECTUNITY_FETCH_IM3D)
        message(FATAL_ERROR
            "Im3d was not resolved. Configure with -DPROJECTUNITY_FETCH_IM3D=ON "
            "or provide a ProjectUnity::Im3d target before editor viewport configuration."
        )
    endif()

    message(STATUS "Fetching Im3d at 3fd7afaf3192ed50bca29c191969aa41252b53d5 (MIT).")
    FetchContent_Declare(im3d
        GIT_REPOSITORY https://github.com/john-chapman/im3d.git
        GIT_TAG 3fd7afaf3192ed50bca29c191969aa41252b53d5
    )
    FetchContent_MakeAvailable(im3d)

    add_library(projectunity_im3d STATIC
        "${im3d_SOURCE_DIR}/im3d.cpp"
        "${im3d_SOURCE_DIR}/im3d.h"
        "${im3d_SOURCE_DIR}/im3d_config.h"
        "${im3d_SOURCE_DIR}/im3d_math.h"
    )
    add_library(ProjectUnity::Im3d ALIAS projectunity_im3d)

    target_include_directories(projectunity_im3d
        SYSTEM PUBLIC
            "${im3d_SOURCE_DIR}"
    )

    projectunity_configure_target(projectunity_im3d)
    if(MSVC)
        target_compile_options(projectunity_im3d PRIVATE /W0)
    else()
        target_compile_options(projectunity_im3d PRIVATE -w)
    endif()
    set(${out_target} ProjectUnity::Im3d PARENT_SCOPE)
endfunction()

function(projectunity_resolve_tinygltf out_target)
    if(TARGET ProjectUnity::TinyGLTF)
        set(${out_target} ProjectUnity::TinyGLTF PARENT_SCOPE)
        return()
    endif()

    if(NOT PROJECTUNITY_FETCH_TINYGLTF)
        message(FATAL_ERROR
            "TinyGLTF was not resolved. Configure with -DPROJECTUNITY_FETCH_TINYGLTF=ON "
            "or provide a ProjectUnity::TinyGLTF target before asset configuration."
        )
    endif()

    message(STATUS "Fetching TinyGLTF at d31c16e333a6c8d593cad43f325f4e1825dd4776 (MIT).")
    FetchContent_Declare(tinygltf
        GIT_REPOSITORY https://github.com/syoyo/tinygltf.git
        GIT_TAG d31c16e333a6c8d593cad43f325f4e1825dd4776
    )
    FetchContent_GetProperties(tinygltf)
    if(NOT tinygltf_POPULATED)
        if(POLICY CMP0169)
            cmake_policy(PUSH)
            cmake_policy(SET CMP0169 OLD)
        endif()
        FetchContent_Populate(tinygltf)
        if(POLICY CMP0169)
            cmake_policy(POP)
        endif()
    endif()

    add_library(projectunity_tinygltf INTERFACE)
    add_library(ProjectUnity::TinyGLTF ALIAS projectunity_tinygltf)
    target_include_directories(projectunity_tinygltf
        SYSTEM INTERFACE
            "${tinygltf_SOURCE_DIR}"
    )
    set(${out_target} ProjectUnity::TinyGLTF PARENT_SCOPE)
endfunction()

function(projectunity_resolve_mikktspace out_target)
    if(TARGET ProjectUnity::MikkTSpace)
        set(${out_target} ProjectUnity::MikkTSpace PARENT_SCOPE)
        return()
    endif()

    if(NOT PROJECTUNITY_FETCH_MIKKTSPACE)
        message(FATAL_ERROR
            "MikkTSpace was not resolved. Configure with -DPROJECTUNITY_FETCH_MIKKTSPACE=ON "
            "or provide a ProjectUnity::MikkTSpace target before asset configuration."
        )
    endif()

    message(STATUS "Fetching MikkTSpace at 3e895b49d05ea07e4c2133156cfa94369e19e409 (zlib-style).")
    FetchContent_Declare(mikktspace
        GIT_REPOSITORY https://github.com/mmikk/MikkTSpace.git
        GIT_TAG 3e895b49d05ea07e4c2133156cfa94369e19e409
    )
    FetchContent_MakeAvailable(mikktspace)

    add_library(projectunity_mikktspace STATIC
        "${mikktspace_SOURCE_DIR}/mikktspace.c"
        "${mikktspace_SOURCE_DIR}/mikktspace.h"
    )
    add_library(ProjectUnity::MikkTSpace ALIAS projectunity_mikktspace)
    target_include_directories(projectunity_mikktspace
        SYSTEM PUBLIC
            "${mikktspace_SOURCE_DIR}"
    )
    projectunity_configure_target(projectunity_mikktspace)
    if(MSVC)
        target_compile_options(projectunity_mikktspace PRIVATE /W0)
    else()
        target_compile_options(projectunity_mikktspace PRIVATE -w)
    endif()
    set(${out_target} ProjectUnity::MikkTSpace PARENT_SCOPE)
endfunction()

function(projectunity_resolve_meshoptimizer out_target)
    if(TARGET ProjectUnity::Meshoptimizer)
        set(${out_target} ProjectUnity::Meshoptimizer PARENT_SCOPE)
        return()
    endif()

    if(NOT PROJECTUNITY_FETCH_MESHOPTIMIZER)
        message(FATAL_ERROR
            "meshoptimizer was not resolved. Configure with -DPROJECTUNITY_FETCH_MESHOPTIMIZER=ON "
            "or provide a ProjectUnity::Meshoptimizer target before asset configuration."
        )
    endif()

    message(STATUS "Fetching meshoptimizer at c619e7b941646e72ad1da67c058811207bcbcf88 (MIT).")
    FetchContent_Declare(meshoptimizer
        GIT_REPOSITORY https://github.com/zeux/meshoptimizer.git
        GIT_TAG c619e7b941646e72ad1da67c058811207bcbcf88
    )
    FetchContent_MakeAvailable(meshoptimizer)

    if(NOT TARGET meshoptimizer)
        message(FATAL_ERROR "meshoptimizer was fetched, but its CMake target was not created.")
    endif()

    add_library(ProjectUnity::Meshoptimizer ALIAS meshoptimizer)
    set(${out_target} ProjectUnity::Meshoptimizer PARENT_SCOPE)
endfunction()

function(projectunity_resolve_libktx out_target)
    if(TARGET ProjectUnity::LibKTX)
        set(${out_target} ProjectUnity::LibKTX PARENT_SCOPE)
        return()
    endif()

    find_package(ktx CONFIG QUIET)
    set(_ktx_candidates
        ktx_read
        ktx::ktx_read
        KTX::ktx_read
        ktx
        ktx::ktx
        KTX::ktx
    )
    foreach(_candidate IN LISTS _ktx_candidates)
        if(TARGET ${_candidate})
            set(${out_target} ${_candidate} PARENT_SCOPE)
            return()
        endif()
    endforeach()

    if(NOT PROJECTUNITY_FETCH_LIBKTX)
        message(FATAL_ERROR
            "libktx was not found. Install KTX-Software, provide its CMake package, "
            "or configure with -DPROJECTUNITY_FETCH_LIBKTX=ON."
        )
    endif()

    message(STATUS "Fetching KTX-Software v4.4.2 (Apache-2.0) for KTX2 transcoding.")
    set(_projectunity_build_shared_libs_was_defined FALSE)
    if(DEFINED BUILD_SHARED_LIBS)
        set(_projectunity_build_shared_libs_was_defined TRUE)
        set(_projectunity_build_shared_libs_value "${BUILD_SHARED_LIBS}")
    endif()
    set(KTX_FEATURE_TESTS OFF CACHE BOOL "Build KTX tests" FORCE)
    set(KTX_FEATURE_TOOLS OFF CACHE BOOL "Build KTX tools" FORCE)
    set(KTX_FEATURE_LOADTEST_APPS OFF CACHE STRING "Build KTX load-test apps" FORCE)
    set(KTX_FEATURE_DOC OFF CACHE BOOL "Build KTX docs" FORCE)
    set(KTX_FEATURE_JNI OFF CACHE BOOL "Build KTX Java bindings" FORCE)
    set(KTX_FEATURE_PY OFF CACHE BOOL "Build KTX Python bindings" FORCE)
    set(KTX_FEATURE_VK_UPLOAD OFF CACHE BOOL "Build KTX Vulkan upload helpers" FORCE)
    set(KTX_FEATURE_GL_UPLOAD OFF CACHE BOOL "Build KTX OpenGL upload helpers" FORCE)
    set(KTX_WERROR OFF CACHE BOOL "Treat KTX warnings as errors" FORCE)
    set(BUILD_SHARED_LIBS OFF CACHE BOOL "Build static libraries" FORCE)

    FetchContent_Declare(ktx
        GIT_REPOSITORY https://github.com/KhronosGroup/KTX-Software.git
        GIT_TAG v4.4.2
        GIT_SHALLOW TRUE
    )
    FetchContent_GetProperties(ktx)
    if(NOT ktx_POPULATED)
        if(POLICY CMP0169)
            cmake_policy(PUSH)
            cmake_policy(SET CMP0169 OLD)
        endif()
        FetchContent_Populate(ktx)
        if(POLICY CMP0169)
            cmake_policy(POP)
        endif()
    endif()
    add_subdirectory("${ktx_SOURCE_DIR}" "${ktx_BINARY_DIR}" EXCLUDE_FROM_ALL)
    if(_projectunity_build_shared_libs_was_defined)
        set(BUILD_SHARED_LIBS "${_projectunity_build_shared_libs_value}" CACHE BOOL "Build shared libraries" FORCE)
    else()
        unset(BUILD_SHARED_LIBS CACHE)
    endif()

    if(TARGET ktx_read)
        add_library(ProjectUnity::LibKTX ALIAS ktx_read)
        set(${out_target} ProjectUnity::LibKTX PARENT_SCOPE)
        return()
    endif()
    if(TARGET ktx)
        add_library(ProjectUnity::LibKTX ALIAS ktx)
        set(${out_target} ProjectUnity::LibKTX PARENT_SCOPE)
        return()
    endif()

    message(FATAL_ERROR "KTX-Software was fetched, but no libktx CMake target was created.")
endfunction()
