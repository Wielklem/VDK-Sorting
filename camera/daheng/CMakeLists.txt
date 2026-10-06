# Daheng Galaxy adapter (M30.10). Needs the proprietary Galaxy SDK, so it is opt-in
# (VSORT_WITH_GALAXY). CI builds without it; build it locally with the linux-galaxy-debug preset.

if(NOT GALAXY_SDK_ROOT OR NOT EXISTS "${GALAXY_SDK_ROOT}/inc/GxIAPI.h")
  message(FATAL_ERROR
    "VSORT_WITH_GALAXY=ON, but GALAXY_SDK_ROOT is not a Galaxy SDK folder "
    "(inc/GxIAPI.h not found in '${GALAXY_SDK_ROOT}'). "
    "Set the GALAXY_SDK_ROOT environment variable or pass -DGALAXY_SDK_ROOT=<folder>.")
endif()

find_library(GALAXY_GXIAPI_LIBRARY NAMES gxiapi
  PATHS "${GALAXY_SDK_ROOT}/lib/${CMAKE_SYSTEM_PROCESSOR}"
  NO_DEFAULT_PATH REQUIRED)
find_package(Threads REQUIRED)

add_library(vsort_camera_daheng STATIC
  src/galaxy_lib.cpp
  src/daheng_discovery.cpp
  src/daheng_gige_config.cpp
  src/daheng_camera.cpp)
add_library(vsort::camera_daheng ALIAS vsort_camera_daheng)

target_include_directories(vsort_camera_daheng
  PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
# SYSTEM: the vendor headers are not ours to fix.
target_include_directories(vsort_camera_daheng SYSTEM PRIVATE "${GALAXY_SDK_ROOT}/inc")
target_link_libraries(vsort_camera_daheng
  PUBLIC vsort::camera vsort::common
  PRIVATE "${GALAXY_GXIAPI_LIBRARY}" Threads::Threads)
vsort_set_warnings(vsort_camera_daheng)
set_target_properties(vsort_camera_daheng PROPERTIES POSITION_INDEPENDENT_CODE ON)

add_executable(vsort_daheng_probe tools/daheng_probe.cpp)
target_link_libraries(vsort_daheng_probe PRIVATE vsort::camera_daheng)
vsort_set_warnings(vsort_daheng_probe)

if(VSORT_BUILD_TESTS)
  add_subdirectory(tests)
endif()
