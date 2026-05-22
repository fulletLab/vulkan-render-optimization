add_executable(projectunity_core_tests
    core_tests.cpp
)

target_link_libraries(projectunity_core_tests
    PRIVATE
        ProjectUnity::Core
)

projectunity_configure_target(projectunity_core_tests)

add_test(NAME projectunity_core_tests COMMAND projectunity_core_tests)
