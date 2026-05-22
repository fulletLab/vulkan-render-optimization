add_executable(projectunity_debug_tests
    debug_tests.cpp
)

target_link_libraries(projectunity_debug_tests
    PRIVATE
        ProjectUnity::Debug
)

projectunity_configure_target(projectunity_debug_tests)

add_test(NAME projectunity_debug_tests COMMAND projectunity_debug_tests)
