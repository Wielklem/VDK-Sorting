# Page plugin G35 Calibrate (P30.70): one camera live, with the overlay layer on top.
# The QML uses the VsortHmi module of the executable (Theme, Vs* components, VideoItem, OverlayLayer).
add_library(vsort_page_calibrate MODULE calibrate_page.cpp)
qt_add_resources(vsort_page_calibrate "calibrate_qml"
  PREFIX "/VsortPages/Calibrate"
  BASE qml
  FILES qml/CalibratePage.qml qml/CalibrateView.qml)
target_link_libraries(vsort_page_calibrate PRIVATE vsort_hmi_ipage)
set_target_properties(vsort_page_calibrate PROPERTIES
  LIBRARY_OUTPUT_DIRECTORY "$<TARGET_FILE_DIR:vsort_hmi>/pages"
  RUNTIME_OUTPUT_DIRECTORY "$<TARGET_FILE_DIR:vsort_hmi>/pages")
vsort_set_warnings(vsort_page_calibrate)
