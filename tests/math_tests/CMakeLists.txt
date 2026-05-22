add_executable(projectunity_math_tests
    math_tests.cpp
)

target_link_libraries(projectunity_math_tests
    PRIVATE
        ProjectUnity::Math
)

projectunity_configure_target(projectunity_math_tests)

add_test(NAME projectunity_math_tests COMMAND projectunity_math_tests)
