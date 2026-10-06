
add_executable(vsort_ipc_tests
  test_envelope.cpp
  test_preview_ring.cpp)
target_link_libraries(vsort_ipc_tests PRIVATE vsort::ipc GTest::gtest_main)
vsort_set_warnings(vsort_ipc_tests)
gtest_discover_tests(vsort_ipc_tests)
