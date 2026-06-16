add_library(projectunity_scripting STATIC
    src/BuiltInScripts.cpp
    src/ScriptRuntime.cpp
)

add_library(ProjectUnity::Scripting ALIAS projectunity_scripting)

target_include_directories(projectunity_scripting
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(projectunity_scripting
    PUBLIC
        ProjectUnity::Core
        ProjectUnity::Math
        ProjectUnity::Physics
        ProjectUnity::Scene
    PRIVATE
        ${CMAKE_DL_LIBS}
)

projectunity_configure_target(projectunity_scripting)
