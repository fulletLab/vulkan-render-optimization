add_library(projectunity_physics STATIC
    src/PhysicsWorld.cpp
)

add_library(ProjectUnity::Physics ALIAS projectunity_physics)

target_include_directories(projectunity_physics
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(projectunity_physics
    PUBLIC
        ProjectUnity::Core
        ProjectUnity::Math
        ProjectUnity::Scene
        ProjectUnity::Terrain
)

projectunity_configure_target(projectunity_physics)
