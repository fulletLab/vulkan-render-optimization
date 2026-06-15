add_library(projectunity_terrain STATIC
    src/TerrainGenerator.cpp
)

add_library(ProjectUnity::Terrain ALIAS projectunity_terrain)

target_include_directories(projectunity_terrain
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(projectunity_terrain
    PUBLIC
        ProjectUnity::Core
        ProjectUnity::Math
)

projectunity_configure_target(projectunity_terrain)
