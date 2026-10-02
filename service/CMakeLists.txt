find_package(OpenCV CONFIG REQUIRED)
find_package(cppzmq CONFIG REQUIRED)
find_package(unofficial-sqlite3 CONFIG REQUIRED)

add_executable(vsort_service
  src/main.cpp)

target_link_libraries(vsort_service PRIVATE
  vsort::common
  opencv_core opencv_imgproc
  unofficial::sqlite3::sqlite3)

if(TARGET cppzmq-static)
  target_link_libraries(vsort_service PRIVATE cppzmq-static)
else()
  target_link_libraries(vsort_service PRIVATE cppzmq)
endif()

vsort_set_warnings(vsort_service)
