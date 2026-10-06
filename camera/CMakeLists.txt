# Interfaces plus the resilient wrapper (P20.50). The Daheng adapter (P20.30) is opt-in;
# the replay camera comes in P20.70.
find_package(Threads REQUIRED)
add_library(vsort_camera STATIC src/resilient_camera.cpp)
add_library(vsort::camera ALIAS vsort_camera)
target_include_directories(vsort_camera PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(vsort_camera PUBLIC vsort::common PRIVATE Threads::Threads)
vsort_set_warnings(vsort_camera)
set_target_properties(vsort_camera PROPERTIES POSITION_INDEPENDENT_CODE ON)

option(VSORT_WITH_GALAXY "Build the Daheng Galaxy adapter (needs the proprietary Galaxy SDK)" OFF)
set(GALAXY_SDK_ROOT "$ENV{GALAXY_SDK_ROOT}" CACHE PATH "Galaxy SDK folder (contains inc/ and lib/)")
if(VSORT_WITH_GALAXY)
  add_subdirectory(daheng)
endif()

add_subdirectory(recorder)

if(VSORT_BUILD_TESTS)
  add_subdirectory(tests)
endif()