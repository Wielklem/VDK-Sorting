add_executable(vsort_common_tests
  test_version.cpp)
target_link_libraries(vsort_common_tests PRIVATE vsort::common GTest::gtest_main)
vsort_set_warnings(vsort_common_tests)
gtest_discover_tests(vsort_common_tests)
