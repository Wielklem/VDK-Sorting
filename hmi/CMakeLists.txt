find_package(Qt6 6.5 REQUIRED COMPONENTS Quick QuickControls2)
qt_standard_project_setup(REQUIRES 6.5)

# Put all QML files in the module root, next to the qmldir (needed for the Theme singleton).
set(VSORT_QML_NAMES Main Gallery Theme VsText VsButton VsNumberInput VsTable VsDialog)
foreach(name IN LISTS VSORT_QML_NAMES)
  set_source_files_properties(qml/${name}.qml PROPERTIES QT_RESOURCE_ALIAS ${name}.qml)
endforeach()
set_source_files_properties(qml/Theme.qml PROPERTIES QT_QML_SINGLETON_TYPE TRUE)

qt_add_executable(vsort_hmi
  src/main.cpp)

qt_add_qml_module(vsort_hmi
  URI VsortHmi
  VERSION 1.0
  QML_FILES
    qml/Main.qml
    qml/Gallery.qml
    qml/Theme.qml
    qml/VsText.qml
    qml/VsButton.qml
    qml/VsNumberInput.qml
    qml/VsTable.qml
    qml/VsDialog.qml)

target_link_libraries(vsort_hmi PRIVATE Qt6::Quick Qt6::QuickControls2 vsort::common)
vsort_set_warnings(vsort_hmi)

# Headless QML smoke test (fails on any QML warning). Linux only until the
# Windows runner is verified to find the Qt platform plugins.
if(VSORT_BUILD_TESTS AND NOT WIN32)
  add_test(NAME hmi_gallery_selftest COMMAND vsort_hmi --selftest)
  set_tests_properties(hmi_gallery_selftest PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QUICK_BACKEND=software"
    TIMEOUT 60)
endif()
