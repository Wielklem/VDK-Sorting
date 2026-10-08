# CLI record tool (P20.90). The core (argument parsing and the recording session) has no SDK
# dependency, so it is built and unit-tested on every platform. The executable needs the Daheng
# adapter, so it is only built with VSORT_WITH_GALAXY.
add_library(vsort_record_core STATIC
  src/record_cli.cpp
  src/record_session.cpp)
target_include_directories(vsort_record_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(vsort_record_core PUBLIC
  vsort::recorder vsort::camera vsort::common vsort::platform)
vsort_set_warnings(vsort_record_core)

if(VSORT_WITH_GALAXY)
  add_executable(vsort_record src/main.cpp)
  target_link_libraries(vsort_record PRIVATE vsort_record_core vsort::camera_daheng)
  vsort_set_warnings(vsort_record)
endif()

if(VSORT_BUILD_TESTS)
  add_subdirectory(tests)
endif()
