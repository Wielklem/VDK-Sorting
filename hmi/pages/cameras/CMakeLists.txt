# Page plugin G30 Cameras. G30.10 Live view is implemented (P30.60); the other tabs follow.
# The QML uses the VsortHmi module of the executable (Theme, Vs* components, VideoItem).
add_library(vsort_page_cameras MODULE cameras_page.cpp)
qt_add_resources(vsort_page_cameras "cameras_qml"
  PREFIX "/VsortPages/Cameras"
  BASE qml
  FILES qml/CamerasPage.qml qml/LiveViewPage.qml qml/CameraTile.qml)
target_link_libraries(vsort_page_cameras PRIVATE vsort_hmi_ipage)
set_target_properties(vsort_page_cameras PROPERTIES
  LIBRARY_OUTPUT_DIRECTORY "$<TARGET_FILE_DIR:vsort_hmi>/pages"
  RUNTIME_OUTPUT_DIRECTORY "$<TARGET_FILE_DIR:vsort_hmi>/pages")
vsort_set_warnings(vsort_page_cameras)
