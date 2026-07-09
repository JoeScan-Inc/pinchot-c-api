if (${PINCHOT_API_ROOT_DIR} STREQUAL "")
  message(FATAL_ERROR "PINCHOT_API_ROOT_DIR not defined!")
endif()

include(PinchotSources)
include(PinchotSchema)
include(PinchotVersionInfo)

if(MSVC)
  set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} /MT /EHsc")
  set(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} /MTd /EHsc")
endif(MSVC)

if(UNIX)
  set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wunused-variable -Wunused-result")
  set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -ggdb3 -O3")
  # The "-fvisibility=hidden" flag makes all functions in the shared library
  # not exported by default. To export a function, it needs to be done so
  # explicitly using toolchain specific function properties.
  set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fvisibility=hidden")
endif(UNIX)

if(MINGW)
  set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wunused-variable -Wunused-result")
  # Same visibility semantics as UNIX; __declspec(dllexport) is still required
  # on exported symbols but this prevents accidental export of internal symbols.
  set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fvisibility=hidden")
endif(MINGW)

target_sources(
  ${CMAKE_PROJECT_NAME}
  PRIVATE ${C_API_SOURCES} ${SCHEMA_BINARY_SOURCES}
)
target_link_libraries(${CMAKE_PROJECT_NAME} ${CMAKE_THREAD_LIBS_INIT})

if(WIN32)
  target_link_libraries(${CMAKE_PROJECT_NAME} ws2_32 iphlpapi)
endif(WIN32)

add_custom_command(
  TARGET ${CMAKE_PROJECT_NAME} POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E copy
  ${PINCHOT_API_ROOT_DIR}/src/joescan_pinchot.h
  $<TARGET_FILE_DIR:${CMAKE_PROJECT_NAME}>/joescan_pinchot.h)
