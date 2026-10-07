find_package(Threads REQUIRED)
find_package(nlohmann_json CONFIG REQUIRED)

add_library(vsort_replay STATIC src/replay_camera.cpp src/replay_session.cpp)
add_library(vsort::replay ALIAS vsort_replay)
target_include_directories(vsort_replay PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(vsort_replay
  PUBLIC vsort::camera vsort::common
  PRIVATE vsort::recorder Threads::Threads nlohmann_json::nlohmann_json)
vsort_set_warnings(vsort_replay)
set_target_properties(vsort_replay PROPERTIES POSITION_INDEPENDENT_CODE ON)

if(VSORT_BUILD_TESTS)
  add_subdirectory(tests)
endif()
