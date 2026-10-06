find_package(Threads REQUIRED)

add_library(vsort_platform STATIC
  src/paths.cpp)
add_library(vsort::platform ALIAS vsort_platform)

if(WIN32)
  target_sources(vsort_platform PRIVATE src/windows/platform_stubs.cpp)
elseif(CMAKE_SYSTEM_NAME STREQUAL "Linux")
  target_sources(vsort_platform PRIVATE
    src/linux/paths_linux.cpp
    src/linux/shared_memory_linux.cpp
    src/linux/thread_tuning_linux.cpp
    src/linux/service_host_linux.cpp)
else()
  message(FATAL_ERROR "vsort_platform: only Linux and Windows are supported")
endif()

target_include_directories(vsort_platform PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(vsort_platform PUBLIC vsort::common PRIVATE Threads::Threads)
vsort_set_warnings(vsort_platform)
set_target_properties(vsort_platform PROPERTIES POSITION_INDEPENDENT_CODE ON)

if(VSORT_BUILD_TESTS)
  add_subdirectory(tests)
endif()
