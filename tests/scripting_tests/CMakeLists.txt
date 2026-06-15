add_executable(projectunity_scripting_tests
    scripting_tests.cpp
)

target_link_libraries(projectunity_scripting_tests
    PRIVATE
        ProjectUnity::Scripting
)

projectunity_configure_target(projectunity_scripting_tests)

add_test(NAME projectunity_scripting_tests COMMAND projectunity_scripting_tests)
