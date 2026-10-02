find_package(Qt6 6.5 REQUIRED COMPONENTS Quick)
qt_standard_project_setup(REQUIRES 6.5)

qt_add_executable(vsort_hmi
  src/main.cpp)

qt_add_qml_module(vsort_hmi
  URI VsortHmi
  VERSION 1.0
  QML_FILES qml/Main.qml)

target_link_libraries(vsort_hmi PRIVATE Qt6::Quick vsort::common)
vsort_set_warnings(vsort_hmi)
