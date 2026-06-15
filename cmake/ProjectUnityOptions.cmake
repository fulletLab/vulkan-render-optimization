option(PROJECTUNITY_BUILD_EDITOR "Build the Qt/ADS editor application." ON)
option(PROJECTUNITY_BUILD_TESTS "Build CTest based unit tests." ON)
option(PROJECTUNITY_FETCH_QTADS "Fetch Qt Advanced Docking System when an installed package is not found." ON)
option(PROJECTUNITY_FETCH_TINYGIZMO "Fetch tinygizmo for editor transform gizmos." ON)
option(PROJECTUNITY_FETCH_IM3D "Fetch Im3d for editor debug draw rendering." ON)
option(PROJECTUNITY_FETCH_TINYGLTF "Fetch TinyGLTF and its bundled stb image headers for asset import." ON)
option(PROJECTUNITY_FETCH_MIKKTSPACE "Fetch MikkTSpace for imported mesh tangent generation." ON)
option(PROJECTUNITY_FETCH_MESHOPTIMIZER "Fetch meshoptimizer for imported mesh optimization and LODs." ON)
option(PROJECTUNITY_ENABLE_LIBKTX "Enable libktx for KTX2 Basis/UASTC/Zstd texture import." ON)
option(PROJECTUNITY_FETCH_LIBKTX "Fetch libktx when an installed package is not found." ON)
option(PROJECTUNITY_WARNINGS_AS_ERRORS "Treat compiler warnings as errors." OFF)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

function(projectunity_configure_target target_name)
    target_compile_features(${target_name} PUBLIC cxx_std_20)

    if(MSVC)
        target_compile_options(${target_name} PRIVATE /W4 /permissive- /EHsc /FS)
        if(PROJECTUNITY_WARNINGS_AS_ERRORS)
            target_compile_options(${target_name} PRIVATE /WX)
        endif()
    else()
        target_compile_options(${target_name} PRIVATE -Wall -Wextra -Wpedantic)
        if(PROJECTUNITY_WARNINGS_AS_ERRORS)
            target_compile_options(${target_name} PRIVATE -Werror)
        endif()
    endif()

    target_compile_definitions(${target_name}
        PRIVATE
            $<$<CONFIG:Debug>:PROJECTUNITY_DEBUG=1>
    )
endfunction()
