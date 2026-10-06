# Example page plugin (G30). Real page content comes in P30.60+.
add_library(vsort_page_cameras MODULE cameras_page.cpp)
qt_add_resources(vsort_page_cameras "cameras_qml"
  PREFIX "/VsortPages/Cameras"
  BASE qml
  FILES qml/CamerasPage.qml)
target_link_libraries(vsort_page_cameras PRIVATE vsort_hmi_ipage)
set_target_properties(vsort_page_cameras PROPERTIES
  LIBRARY_OUTPUT_DIRECTORY "$<TARGET_FILE_DIR:vsort_hmi>/pages"
  RUNTIME_OUTPUT_DIRECTORY "$<TARGET_FILE_DIR:vsort_hmi>/pages")
vsort_set_warnings(vsort_page_cameras)
