add_executable(projectunity_source_rule_tests
    source_rule_tests.cpp
)

target_compile_definitions(projectunity_source_rule_tests
    PRIVATE
        PROJECTUNITY_SOURCE_DIR="${PROJECT_SOURCE_DIR}"
)

projectunity_configure_target(projectunity_source_rule_tests)

add_test(NAME projectunity_source_rule_tests COMMAND projectunity_source_rule_tests)
