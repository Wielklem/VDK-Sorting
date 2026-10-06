find_package(Qt6 6.5 REQUIRED COMPONENTS Quick)
qt_standard_project_setup(REQUIRES 6.5)

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

qt_add_executable(vsort_hmi
  src/main.cpp)

qt_add_qml_module(vsort_hmi
  URI VsortHmi
  VERSION 1.0
  QML_FILES qml/Main.qml qml/NavBar.qml)

target_link_libraries(vsort_hmi PRIVATE Qt6::Quick vsort::common vsort_hmi_core)
vsort_set_warnings(vsort_hmi)

add_subdirectory(pages)

if(VSORT_BUILD_TESTS)
  add_subdirectory(tests)
endif()
