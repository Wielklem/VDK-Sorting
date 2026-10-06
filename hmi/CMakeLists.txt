find_package(Qt6 6.5 REQUIRED COMPONENTS Quick QuickControls2)
qt_standard_project_setup(REQUIRES 6.5)

# Put all QML files in the module root, next to the qmldir (needed for the Theme singleton).
set(VSORT_QML_NAMES Main Gallery Theme NavBar VsText VsButton VsNumberInput VsTable VsDialog)
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
    qml/VsDialog.qml)

target_sources(vsort_hmi PRIVATE ${VSORT_HMI_VIDEO_ITEM_SOURCES})
target_link_libraries(vsort_hmi PRIVATE Qt6::Quick Qt6::QuickControls2 vsort::common
                                        vsort_hmi_core vsort_hmi_video)
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
