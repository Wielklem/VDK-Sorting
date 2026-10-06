add_executable(vsort_camera_tests
  test_camera_interface.cpp)
target_link_libraries(vsort_camera_tests PRIVATE vsort::camera GTest::gtest_main)
vsort_set_warnings(vsort_camera_tests)
gtest_discover_tests(vsort_camera_tests)
