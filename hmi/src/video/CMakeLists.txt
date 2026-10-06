add_library(vsort_hmi_video STATIC frame_convert.cpp)
target_include_directories(vsort_hmi_video PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/..)
target_link_libraries(vsort_hmi_video PUBLIC Qt6::Gui vsort::ipc)
vsort_set_warnings(vsort_hmi_video)
set_target_properties(vsort_hmi_video PROPERTIES POSITION_INDEPENDENT_CODE ON)

# VideoItem registers itself with QML at startup (see video_item.cpp), so the exe and the demo compile it directly.
set(VSORT_HMI_VIDEO_ITEM_SOURCES
  ${CMAKE_CURRENT_SOURCE_DIR}/video_item.hpp
  ${CMAKE_CURRENT_SOURCE_DIR}/video_item.cpp
  PARENT_SCOPE)

# Pure unit tests (QImage conversion, mailbox). No Qt platform plugin needed, so they run on Windows too.
if(VSORT_BUILD_TESTS)
  add_subdirectory(tests)
endif()