# Page plugin G60 Analytics (P60.45): G60.20 Parameters, starting with the HSV editor.
# The QML uses the VsortHmi module of the executable (Theme, Vs* components, HsvViewItem).
add_library(vsort_page_analytics MODULE analytics_page.cpp)
qt_add_resources(vsort_page_analytics "analytics_qml"
  PREFIX "/VsortPages/Analytics"
  BASE qml
  FILES qml/AnalyticsPage.qml qml/HsvEditorView.qml)
target_link_libraries(vsort_page_analytics PRIVATE vsort_hmi_ipage)
set_target_properties(vsort_page_analytics PROPERTIES
  LIBRARY_OUTPUT_DIRECTORY "$<TARGET_FILE_DIR:vsort_hmi>/pages"
  RUNTIME_OUTPUT_DIRECTORY "$<TARGET_FILE_DIR:vsort_hmi>/pages")
vsort_set_warnings(vsort_page_analytics)
