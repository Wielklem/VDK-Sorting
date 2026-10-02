find_package(spdlog CONFIG REQUIRED)
find_package(nlohmann_json CONFIG REQUIRED)

add_library(vsort_common
  src/version.cpp)
add_library(vsort::common ALIAS vsort_common)

target_include_directories(vsort_common PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_compile_definitions(vsort_common PRIVATE VSORT_VERSION="${PROJECT_VERSION}")
target_link_libraries(vsort_common PUBLIC spdlog::spdlog nlohmann_json::nlohmann_json)
vsort_set_warnings(vsort_common)

if(VSORT_BUILD_TESTS)
  add_subdirectory(tests)
endif()
