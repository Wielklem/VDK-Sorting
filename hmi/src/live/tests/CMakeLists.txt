find_package(Qt6 REQUIRED COMPONENTS Core)

# Stand-in for the service IPC server, also used by the GUI tests.
add_library(vsort_hmi_test_support STATIC fake_service.cpp)
target_include_directories(vsort_hmi_test_support PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(vsort_hmi_test_support PUBLIC vsort::ipc vsort_hmi_live)
vsort_set_warnings(vsort_hmi_test_support)

# Needs only QCoreApplication (no platform plugin), so it runs on Windows too. On Windows the
# tests skip themselves until the shared-memory layer exists (M15.20).
add_executable(vsort_hmi_live_tests
  main.cpp
  test_live_view_model.cpp)
target_link_libraries(vsort_hmi_live_tests PRIVATE vsort_hmi_test_support GTest::gtest)
vsort_set_warnings(vsort_hmi_live_tests)
# Fixed TCP ports and shared-memory names: never run in parallel with the other IPC tests.
gtest_discover_tests(vsort_hmi_live_tests PROPERTIES RESOURCE_LOCK ipc_resources)
