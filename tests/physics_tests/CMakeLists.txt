add_executable(projectunity_physics_tests
    physics_tests.cpp
)

target_link_libraries(projectunity_physics_tests
    PRIVATE
        ProjectUnity::Physics
)

projectunity_configure_target(projectunity_physics_tests)

add_test(NAME projectunity_physics_tests COMMAND projectunity_physics_tests)
