# Live view backend (P30.60): IPC client to the service, camera model, frame hand-over to VideoItems.
find_package(cppzmq CONFIG REQUIRED)

add_library(vsort_hmi_live STATIC
  camera_info.hpp
  service_client.cpp
  live_view_model.cpp
  roi_editor_model.cpp
  camera_settings_model.cpp
  product_monitor_model.cpp
  analysis_tuning_model.cpp
  detector_model.cpp)
target_include_directories(vsort_hmi_live PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/..)
target_link_libraries(vsort_hmi_live PUBLIC Qt6::Core vsort_hmi_video vsort::common vsort::ipc)
if(TARGET cppzmq-static)
  target_link_libraries(vsort_hmi_live PUBLIC cppzmq-static)
else()
  target_link_libraries(vsort_hmi_live PUBLIC cppzmq)
endif()
vsort_set_warnings(vsort_hmi_live)
set_target_properties(vsort_hmi_live PROPERTIES POSITION_INDEPENDENT_CODE ON)

if(VSORT_BUILD_TESTS)
  add_subdirectory(tests)
endif()
