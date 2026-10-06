find_package(Threads REQUIRED)
find_package(nlohmann_json CONFIG REQUIRED)

add_library(vsort_recorder STATIC src/recorder.cpp)
add_library(vsort::recorder ALIAS vsort_recorder)
target_include_directories(vsort_recorder PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(vsort_recorder
  PUBLIC vsort::camera vsort::common Threads::Threads
  PRIVATE nlohmann_json::nlohmann_json)
vsort_set_warnings(vsort_recorder)
set_target_properties(vsort_recorder PROPERTIES POSITION_INDEPENDENT_CODE ON)

if(VSORT_BUILD_TESTS)
  add_subdirectory(tests)
endif()
