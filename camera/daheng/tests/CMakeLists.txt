find_package(Threads REQUIRED)

add_executable(vsort_daheng_tests test_daheng.cpp)
target_link_libraries(vsort_daheng_tests PRIVATE vsort::camera_daheng Threads::Threads GTest::gtest_main)
vsort_set_warnings(vsort_daheng_tests)
gtest_discover_tests(vsort_daheng_tests)
