find_package(Threads REQUIRED)
find_package(nlohmann_json CONFIG REQUIRED)

add_executable(vsort_record_tests
  test_record_cli.cpp
  test_record_session.cpp)
target_link_libraries(vsort_record_tests PRIVATE
  vsort_record_core nlohmann_json::nlohmann_json Threads::Threads GTest::gtest_main)
vsort_set_warnings(vsort_record_tests)
gtest_discover_tests(vsort_record_tests)
