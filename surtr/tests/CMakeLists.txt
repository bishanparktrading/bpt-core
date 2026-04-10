include(GoogleTest)

add_executable(surtr_tests
    test_black_scholes.cpp
    test_iv_solver.cpp
    test_forward_curve.cpp
)

target_link_libraries(surtr_tests PRIVATE
    surtr_lib
    GTest::gtest_main
)

gtest_discover_tests(surtr_tests)
