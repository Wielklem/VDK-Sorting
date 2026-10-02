cmake_minimum_required(VERSION 3.25)

option(VSORT_BUILD_HMI   "Build the Qt HMI"  ON)
option(VSORT_BUILD_TESTS "Build unit tests"  ON)

# Map options to vcpkg manifest features (must be before project())
if(VSORT_BUILD_HMI)
  list(APPEND VCPKG_MANIFEST_FEATURES "hmi")
endif()
if(VSORT_BUILD_TESTS)
  list(APPEND VCPKG_MANIFEST_FEATURES "tests")
endif()

project(VDKSorting VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

list(APPEND CMAKE_MODULE_PATH ${CMAKE_SOURCE_DIR}/cmake)
include(CompilerWarnings)

if(VSORT_BUILD_TESTS)
  enable_testing()
  find_package(GTest CONFIG REQUIRED)
  include(GoogleTest)
endif()

add_subdirectory(common)
add_subdirectory(service)
if(VSORT_BUILD_HMI)
  add_subdirectory(hmi)
endif()
