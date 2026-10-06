find_package(Flatbuffers CONFIG REQUIRED)

# flatc comes from the vcpkg tools dir (target or host triplet).
find_program(VSORT_FLATC flatc
  HINTS "${VCPKG_INSTALLED_DIR}/${VCPKG_HOST_TRIPLET}/tools/flatbuffers"
        "${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/tools/flatbuffers")
if(NOT VSORT_FLATC AND TARGET flatbuffers::flatc)
  set(VSORT_FLATC flatbuffers::flatc)
endif()
if(NOT VSORT_FLATC)
  message(FATAL_ERROR "flatc not found (vcpkg port 'flatbuffers' or system package flatbuffers-compiler)")
endif()

set(VSORT_IPC_SCHEMA ${CMAKE_CURRENT_SOURCE_DIR}/schema/vdk_ipc.fbs)
set(VSORT_IPC_GEN_DIR ${CMAKE_CURRENT_BINARY_DIR}/generated)
set(VSORT_IPC_GEN_HEADER ${VSORT_IPC_GEN_DIR}/vsort/ipc/vdk_ipc_generated.h)
add_custom_command(
  OUTPUT ${VSORT_IPC_GEN_HEADER}
  COMMAND ${CMAKE_COMMAND} -E make_directory ${VSORT_IPC_GEN_DIR}/vsort/ipc
  COMMAND ${VSORT_FLATC} --cpp --scoped-enums -o ${VSORT_IPC_GEN_DIR}/vsort/ipc ${VSORT_IPC_SCHEMA}
  DEPENDS ${VSORT_IPC_SCHEMA}
  COMMENT "flatc: vdk_ipc.fbs"
  VERBATIM)

add_library(vsort_ipc STATIC
  src/envelope.cpp
  src/preview_ring.cpp
  ${VSORT_IPC_GEN_HEADER})
add_library(vsort::ipc ALIAS vsort_ipc)

target_include_directories(vsort_ipc PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
# SYSTEM: no warnings or clang-tidy findings from generated code.
target_include_directories(vsort_ipc SYSTEM PUBLIC ${VSORT_IPC_GEN_DIR})
target_link_libraries(vsort_ipc PUBLIC vsort::common vsort::platform flatbuffers::flatbuffers)
vsort_set_warnings(vsort_ipc)
set_target_properties(vsort_ipc PROPERTIES POSITION_INDEPENDENT_CODE ON)

if(VSORT_BUILD_TESTS)
  add_subdirectory(tests)
endif()
