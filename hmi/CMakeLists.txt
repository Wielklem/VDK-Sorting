find_package(Qt6 6.5 REQUIRED COMPONENTS Quick QuickControls2)
qt_standard_project_setup(REQUIRES 6.5)

# Put all QML files in the module root, next to the qmldir (needed for the Theme singleton).
set(VSORT_QML_NAMES Main Gallery Theme NavBar VsText VsButton VsNumberInput VsTable VsDialog OverlayLayer VsSideTab
    VsCameraPicker)
foreach(name IN LISTS VSORT_QML_NAMES)
  set_source_files_properties(qml/${name}.qml PROPERTIES QT_RESOURCE_ALIAS ${name}.qml)
endforeach()
set_source_files_properties(qml/Theme.qml PROPERTIES QT_QML_SINGLETON_TYPE TRUE)

# IPage header only: what page plugins link against.
add_library(vsort_hmi_ipage INTERFACE)
target_include_directories(vsort_hmi_ipage INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(vsort_hmi_ipage INTERFACE Qt6::Core)

# Registry + loader (header listed so AUTOMOC finds Q_OBJECT).
add_library(vsort_hmi_core STATIC
  src/page_registry.cpp
  src/plugin_loader.cpp
  include/vsort/hmi/page_registry.hpp)
target_link_libraries(vsort_hmi_core PUBLIC vsort_hmi_ipage Qt6::Core)
vsort_set_warnings(vsort_hmi_core)

add_subdirectory(src/video)
add_subdirectory(src/live)

qt_add_executable(vsort_hmi
  src/main.cpp)

qt_add_qml_module(vsort_hmi
  URI VsortHmi
  VERSION 1.0
  QML_FILES
    qml/Main.qml
    qml/Gallery.qml
    qml/Theme.qml
    qml/NavBar.qml
    qml/VsText.qml
    qml/VsButton.qml
    qml/VsNumberInput.qml
    qml/VsTable.qml
    qml/VsDialog.qml
    qml/OverlayLayer.qml
    qml/VsSideTab.qml
    qml/VsCameraPicker.qml)

target_sources(vsort_hmi PRIVATE ${VSORT_HMI_VIDEO_ITEM_SOURCES})
target_link_libraries(vsort_hmi PRIVATE Qt6::Quick Qt6::QuickControls2 vsort::common
                                        vsort_hmi_core vsort_hmi_video vsort_hmi_live)
vsort_set_warnings(vsort_hmi)

# Dev-only: shows VideoItem with a generated test pattern.
add_executable(vsort_video_demo
  demo/video_demo.cpp
  ${VSORT_HMI_VIDEO_ITEM_SOURCES})
target_link_libraries(vsort_video_demo PRIVATE Qt6::Quick vsort_hmi_video)
vsort_set_warnings(vsort_video_demo)

add_subdirectory(pages)

if(VSORT_BUILD_TESTS)
  add_subdirectory(tests)
endif()

# Headless QML smoke test (fails on any QML warning). Linux only until the
# Windows runner is verified to find the Qt platform plugins.
if(VSORT_BUILD_TESTS AND NOT WIN32)
  add_test(NAME hmi_gallery_selftest COMMAND vsort_hmi --selftest)
  set_tests_properties(hmi_gallery_selftest PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QUICK_BACKEND=software"
    TIMEOUT 60)
endif()

# GUI tests: render the real G30.10 page offscreen against a fake service. The test executable
# has its own QML module "VsortHmiTest" (same Theme/Vs* files) and renders copies of the page
# files with "import VsortHmi" rewritten, so it never shares a module URI with vsort_hmi.
# Linux only, like the gallery self-test above.
if(VSORT_BUILD_TESTS AND NOT WIN32)
  set(VSORT_GUI_PAGES_DIR ${CMAKE_CURRENT_BINARY_DIR}/gui_tests_pages)
  foreach(name LiveViewPage CameraTile RoiEditorPage CameraSettingsPage)
    set(src ${CMAKE_CURRENT_SOURCE_DIR}/pages/cameras/qml/${name}.qml)
    file(READ ${src} content)
    string(REPLACE "import VsortHmi\n" "import VsortHmiTest\n" content "${content}")
    file(WRITE ${VSORT_GUI_PAGES_DIR}/${name}.qml "${content}")
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${src})
  endforeach()
  # G60.20 HSV editor (P60.45)
  foreach(name HsvEditorView)
    set(src ${CMAKE_CURRENT_SOURCE_DIR}/pages/analytics/qml/${name}.qml)
    file(READ ${src} content)
    string(REPLACE "import VsortHmi\n" "import VsortHmiTest\n" content "${content}")
    file(WRITE ${VSORT_GUI_PAGES_DIR}/${name}.qml "${content}")
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${src})
  endforeach()
  # G140 Product monitor (P80.110)
  foreach(name ProductMonitorView)
    set(src ${CMAKE_CURRENT_SOURCE_DIR}/pages/product_monitor/qml/${name}.qml)
    file(READ ${src} content)
    string(REPLACE "import VsortHmi\n" "import VsortHmiTest\n" content "${content}")
    file(WRITE ${VSORT_GUI_PAGES_DIR}/${name}.qml "${content}")
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${src})
  endforeach()

  qt_add_executable(vsort_hmi_gui_tests
    tests/gui/main.cpp
    tests/gui/test_live_view_gui.cpp
    tests/gui/test_overlay_layer_gui.cpp
    tests/gui/test_setup_gui.cpp
    tests/gui/test_product_monitor_gui.cpp
    tests/gui/test_hsv_editor_gui.cpp)
  qt_add_qml_module(vsort_hmi_gui_tests
    URI VsortHmiTest
    VERSION 1.0
    QML_FILES
      qml/Theme.qml
      qml/VsText.qml
      qml/VsButton.qml
      qml/VsNumberInput.qml
      qml/OverlayLayer.qml
      qml/VsCameraPicker.qml)
  target_sources(vsort_hmi_gui_tests PRIVATE ${VSORT_HMI_VIDEO_ITEM_SOURCES})
  target_compile_definitions(vsort_hmi_gui_tests PRIVATE
    VSORT_CAMERAS_QML_DIR="${VSORT_GUI_PAGES_DIR}")
  target_link_libraries(vsort_hmi_gui_tests PRIVATE
    Qt6::Quick Qt6::QuickControls2 vsort_hmi_test_support vsort_hmi_live GTest::gtest)
  # Fixed TCP ports and shared-memory names: never run in parallel with the other IPC tests.
  gtest_discover_tests(vsort_hmi_gui_tests PROPERTIES
    RESOURCE_LOCK ipc_resources
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
endif()