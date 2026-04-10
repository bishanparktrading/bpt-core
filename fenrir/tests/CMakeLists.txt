add_executable(fenrir_unit_tests
    unit/test_instrument_cache.cpp
)

target_link_libraries(fenrir_unit_tests PRIVATE
    fenrir_lib
    GTest::gtest_main
    GTest::gmock
)

include(GoogleTest)
gtest_discover_tests(fenrir_unit_tests)
