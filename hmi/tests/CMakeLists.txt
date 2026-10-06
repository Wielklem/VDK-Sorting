add_executable(vsort_hmi_tests
  test_page_registry.cpp
  test_plugin_loader.cpp)
target_link_libraries(vsort_hmi_tests PRIVATE vsort_hmi_core GTest::gtest_main)
vsort_set_warnings(vsort_hmi_tests)
gtest_discover_tests(vsort_hmi_tests)
