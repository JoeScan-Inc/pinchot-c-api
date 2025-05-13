if (${PINCHOT_API_ROOT_DIR} STREQUAL "")
  message(FATAL_ERROR "PINCHOT_API_ROOT_DIR not defined!")
endif()

set(PRECOMPILED_DIR ${PINCHOT_API_ROOT_DIR}/schema)

if(EXISTS ${PRECOMPILED_DIR})
  include_directories(${PRECOMPILED_DIR})
  file(GLOB SCHEMA_BINARY_SOURCES ${PRECOMPILED_DIR}/*.c)
else()
  # source directory
  set(SCHEMA_DIR ${PINCHOT_API_ROOT_DIR}/../../schema)
  set(FLATBUFFERS_SRC_DIR ${SCHEMA_DIR}/flatbuffers-2.0.0)
  set(FLATC_ARGS "--gen-object-api")
  include_directories(${FLATBUFFERS_SRC_DIR}/include)
  include_directories(${CMAKE_LIBRARY_OUTPUT_DIRECTORY})
  add_subdirectory(${FLATBUFFERS_SRC_DIR}
                   ${CMAKE_CURRENT_BINARY_DIR}/flatbuffers-build
                   EXCLUDE_FROM_ALL)

  set(SCHEMA_SOURCES
    ${SCHEMA_DIR}/MessageClientEnums.fbs
    ${SCHEMA_DIR}/MessageClient.fbs
    ${SCHEMA_DIR}/MessageServerEnums.fbs
    ${SCHEMA_DIR}/MessageServer.fbs
    ${SCHEMA_DIR}/MessageDiscoveryClient.fbs
    ${SCHEMA_DIR}/MessageDiscoveryServer.fbs
    ${SCHEMA_DIR}/MessageUpdateClient.fbs
    ${SCHEMA_DIR}/MessageUpdateServer.fbs
    ${SCHEMA_DIR}/ScanHeadType.fbs
    ${SCHEMA_DIR}/ScanHeadSpecification.fbs
    ${SCHEMA_DIR}/StoreInfoData.fbs)

  build_flatbuffers(
    "${SCHEMA_SOURCES}"
    ""
    schema
    ""
    "${CMAKE_BINARY_DIR}"
    ""
    "")

  add_dependencies(${CMAKE_PROJECT_NAME} schema)

  # schema binaries
  file(GLOB SCHEMA_BINARY_SOURCES ${SCHEMA_DIR}/bin/*.c)
  include_directories(${SCHEMA_DIR}/bin)
endif()
