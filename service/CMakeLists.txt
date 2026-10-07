find_package(OpenCV CONFIG REQUIRED)
find_package(cppzmq CONFIG REQUIRED)
find_package(unofficial-sqlite3 CONFIG REQUIRED)

# Everything except main(), so it can be unit-tested.
add_library(vsort_service_core STATIC
  src/cli.cpp
  src/service_app.cpp
  src/camera/camera_manager.cpp
  src/camera/replay_backend.cpp
  src/ipc/command_handler.cpp
  src/ipc/downscale.cpp
  src/ipc/ipc_server.cpp
  src/ipc/preview_hub.cpp
  src/tracking/frame_sequence_tracker.cpp
  src/tracking/lane_crop.cpp
  src/tracking/tracking_module.cpp)
target_include_directories(vsort_service_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(vsort_service_core
  PUBLIC vsort::common vsort::platform vsort::ipc vsort::camera vsort::replay
  PRIVATE opencv_core opencv_imgproc unofficial::sqlite3::sqlite3)
if(TARGET cppzmq-static)
  target_link_libraries(vsort_service_core PRIVATE cppzmq-static)
else()
  target_link_libraries(vsort_service_core PRIVATE cppzmq)
endif()
# Real cameras need the proprietary Galaxy SDK (opt-in, see camera/CMakeLists.txt).
if(VSORT_WITH_GALAXY)
  target_sources(vsort_service_core PRIVATE src/camera/daheng_backend.cpp)
  target_link_libraries(vsort_service_core PUBLIC vsort::camera_daheng)
  target_compile_definitions(vsort_service_core PUBLIC VSORT_WITH_GALAXY)
endif()
vsort_set_warnings(vsort_service_core)

add_executable(vsort_service src/main.cpp)
target_link_libraries(vsort_service PRIVATE vsort_service_core)
vsort_set_warnings(vsort_service)

if(VSORT_BUILD_TESTS)
  add_subdirectory(tests)
endif()
