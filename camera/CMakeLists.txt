# The interfaces are header-only. The Daheng adapter (P20.30) is opt-in; the replay camera comes in P20.70.
add_library(vsort_camera INTERFACE)
add_library(vsort::camera ALIAS vsort_camera)
target_include_directories(vsort_camera INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(vsort_camera INTERFACE vsort::common)

option(VSORT_WITH_GALAXY "Build the Daheng Galaxy adapter (needs the proprietary Galaxy SDK)" OFF)
set(GALAXY_SDK_ROOT "$ENV{GALAXY_SDK_ROOT}" CACHE PATH "Galaxy SDK folder (contains inc/ and lib/)")
if(VSORT_WITH_GALAXY)
  add_subdirectory(daheng)
endif()

if(VSORT_BUILD_TESTS)
  add_subdirectory(tests)
endif()