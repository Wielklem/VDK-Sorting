add_executable(vsort_platform_tests
  test_paths.cpp
  test_shared_memory.cpp
  test_thread_tuning.cpp
  test_service_host.cpp)
target_link_libraries(vsort_platform_tests PRIVATE vsort::platform GTest::gtest_main)
vsort_set_warnings(vsort_platform_tests)
gtest_discover_tests(vsort_platform_tests)
