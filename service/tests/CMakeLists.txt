find_package(cppzmq CONFIG REQUIRED)

add_executable(vsort_service_tests
  test_camera_manager.cpp
  test_camera_wiring.cpp
  test_cli.cpp
  test_cli_camera.cpp
  test_command_handler.cpp
  test_downscale.cpp
  test_ipc_server.cpp
  test_preview_hub.cpp
  test_frame_sequence_tracker.cpp
  test_lane_crop.cpp
  test_tracking_module.cpp
  test_speed_handling.cpp)
target_link_libraries(vsort_service_tests PRIVATE vsort_service_core GTest::gtest_main)
if(TARGET cppzmq-static)
  target_link_libraries(vsort_service_tests PRIVATE cppzmq-static)
else()
  target_link_libraries(vsort_service_tests PRIVATE cppzmq)
endif()
vsort_set_warnings(vsort_service_tests)
# Fixed TCP ports and shared-memory names: never run these tests in parallel.
gtest_discover_tests(vsort_service_tests PROPERTIES RESOURCE_LOCK ipc_resources)
