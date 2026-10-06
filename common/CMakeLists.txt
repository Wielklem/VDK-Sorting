find_package(spdlog CONFIG REQUIRED)
find_package(nlohmann_json CONFIG REQUIRED)

add_library(vsort_common STATIC
  src/version.cpp
  src/error.cpp
  src/timestamp.cpp
  src/logging.cpp
  src/module_registry.cpp)
add_library(vsort::common ALIAS vsort_common)

target_include_directories(vsort_common PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_compile_definitions(vsort_common PRIVATE VSORT_VERSION="${PROJECT_VERSION}")
target_link_libraries(vsort_common PUBLIC spdlog::spdlog nlohmann_json::nlohmann_json)
vsort_set_warnings(vsort_common)
# Static avoids DLL export macros on Windows; PIC so it can link into Qt plugins later.
set_target_properties(vsort_common PROPERTIES POSITION_INDEPENDENT_CODE ON)

if(VSORT_BUILD_TESTS)
  add_subdirectory(tests)
endif()

add_subdirectory(platform)
