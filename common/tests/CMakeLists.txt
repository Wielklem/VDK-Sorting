add_executable(vsort_common_tests
  test_version.cpp
  test_types.cpp
  test_timestamp.cpp
  test_error.cpp
  test_byte_order.cpp
  test_logging.cpp
  test_bounded_queue.cpp
  test_message_bus.cpp)
find_package(Threads REQUIRED)
target_link_libraries(vsort_common_tests PRIVATE vsort::common Threads::Threads GTest::gtest_main)
vsort_set_warnings(vsort_common_tests)
gtest_discover_tests(vsort_common_tests)
