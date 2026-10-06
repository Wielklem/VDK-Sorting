# Header-only for now; adapters (Daheng, replay) are added in P20.30 / P20.70.
add_library(vsort_camera INTERFACE)
add_library(vsort::camera ALIAS vsort_camera)
target_include_directories(vsort_camera INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(vsort_camera INTERFACE vsort::common)

if(VSORT_BUILD_TESTS)
  add_subdirectory(tests)
endif()
