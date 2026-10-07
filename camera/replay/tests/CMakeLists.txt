add_executable(vsort_replay_tests test_replay_camera.cpp test_replay_session.cpp)
target_link_libraries(vsort_replay_tests PRIVATE
  vsort::replay vsort::recorder nlohmann_json::nlohmann_json GTest::gtest_main)
vsort_set_warnings(vsort_replay_tests)
gtest_discover_tests(vsort_replay_tests)
