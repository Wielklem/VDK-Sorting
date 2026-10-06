add_executable(vsort_camera_tests
  test_camera_interface.cpp
  test_camera_map.cpp
  test_discovery.cpp
  test_frame_pool.cpp
  test_genicam.cpp
  test_health.cpp
  test_ip_config.cpp
  test_resilient_camera.cpp)
find_package(Threads REQUIRED)
target_link_libraries(vsort_camera_tests PRIVATE vsort::camera Threads::Threads GTest::gtest_main)
vsort_set_warnings(vsort_camera_tests)
gtest_discover_tests(vsort_camera_tests)
