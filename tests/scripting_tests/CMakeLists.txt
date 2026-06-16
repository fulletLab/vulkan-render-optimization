add_executable(projectunity_scripting_tests
    scripting_tests.cpp
)

target_link_libraries(projectunity_scripting_tests
    PRIVATE
        ProjectUnity::Scripting
)

add_dependencies(projectunity_scripting_tests ProjectUnityGameScripts)

projectunity_configure_target(projectunity_scripting_tests)

add_test(NAME projectunity_scripting_tests COMMAND projectunity_scripting_tests)
set_tests_properties(projectunity_scripting_tests PROPERTIES
    ENVIRONMENT "PROJECTUNITY_TEST_SCRIPTS_DLL=$<TARGET_FILE:ProjectUnityGameScripts>"
)
