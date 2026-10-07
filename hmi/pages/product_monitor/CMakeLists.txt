
# Page plugin G140 Product monitor (P80.110): G140.10 live cup table per lane.
# The QML uses the VsortHmi module of the executable (Theme, Vs* components).
add_library(vsort_page_product_monitor MODULE product_monitor_page.cpp)
qt_add_resources(vsort_page_product_monitor "product_monitor_qml"
  PREFIX "/VsortPages/ProductMonitor"
  BASE qml
  FILES qml/ProductMonitorPage.qml qml/ProductMonitorView.qml)
target_link_libraries(vsort_page_product_monitor PRIVATE vsort_hmi_ipage)
set_target_properties(vsort_page_product_monitor PROPERTIES
  LIBRARY_OUTPUT_DIRECTORY "$<TARGET_FILE_DIR:vsort_hmi>/pages"
  RUNTIME_OUTPUT_DIRECTORY "$<TARGET_FILE_DIR:vsort_hmi>/pages")
vsort_set_warnings(vsort_page_product_monitor)
