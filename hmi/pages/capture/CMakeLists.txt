
# Page plugin G150 Photo capture (P60.100): G150.10 saves photos of the lane ROIs to a folder.
# The QML uses the VsortHmi module of the executable (Theme, Vs* components).
add_library(vsort_page_capture MODULE capture_page.cpp)
qt_add_resources(vsort_page_capture "capture_qml"
  PREFIX "/VsortPages/Capture"
  BASE qml
  FILES qml/CapturePage.qml qml/CaptureView.qml qml/CaptureFolderPicker.qml)
target_link_libraries(vsort_page_capture PRIVATE vsort_hmi_ipage)
set_target_properties(vsort_page_capture PROPERTIES
  LIBRARY_OUTPUT_DIRECTORY "$<TARGET_FILE_DIR:vsort_hmi>/pages"
  RUNTIME_OUTPUT_DIRECTORY "$<TARGET_FILE_DIR:vsort_hmi>/pages")
vsort_set_warnings(vsort_page_capture)
