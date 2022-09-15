if (${PINCHOT_API_ROOT_DIR} STREQUAL "")
  message(FATAL_ERROR "PINCHOT_API_ROOT_DIR not defined!")
endif()

include(PinchotSources)
include(PinchotSchema)
include(PinchotVersionInfo)

if(WIN32)
  set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} /MT /EHsc")
  set(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} /MTd /EHsc")
endif(WIN32)

if(UNIX)
  set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -ggdb3 -O3")
  # The "-fvisibility=hidden" flag makes all functions in the shared library
  # not exported by default. To export a function, it needs to be done so
  # explicitly using toolchain specific function properties.
  set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=c++11 -fvisibility=hidden")
endif(UNIX)

target_sources(
  ${CMAKE_PROJECT_NAME}
  PRIVATE ${C_API_SOURCES} ${SCHEMA_BINARY_SOURCES}
)
target_link_libraries(${CMAKE_PROJECT_NAME} ${CMAKE_THREAD_LIBS_INIT})
