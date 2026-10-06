add_executable(vsort_recorder_tests test_recorder.cpp)
target_link_libraries(vsort_recorder_tests PRIVATE
  vsort::recorder nlohmann_json::nlohmann_json GTest::gtest_main)
vsort_set_warnings(vsort_recorder_tests)
gtest_discover_tests(vsort_recorder_tests)
