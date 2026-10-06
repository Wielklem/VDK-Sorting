find_package(cppzmq CONFIG REQUIRED)

add_executable(vsort_service_tests
  test_cli.cpp
  test_command_handler.cpp
  test_downscale.cpp
  test_ipc_server.cpp
  test_preview_hub.cpp)
target_link_libraries(vsort_service_tests PRIVATE vsort_service_core GTest::gtest_main)
if(TARGET cppzmq-static)
  target_link_libraries(vsort_service_tests PRIVATE cppzmq-static)
else()
  target_link_libraries(vsort_service_tests PRIVATE cppzmq)
endif()
vsort_set_warnings(vsort_service_tests)
# Fixed TCP ports and shared-memory names: never run these tests in parallel.
gtest_discover_tests(vsort_service_tests PROPERTIES RESOURCE_LOCK ipc_resources)
