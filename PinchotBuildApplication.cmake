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
  set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -pthread")
  set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wno-unused -Wall -Wshadow")
endif(UNIX)

list (APPEND SOURCES ${C_API_SOURCES})
list (APPEND SOURCES ${SCHEMA_BINARY_SOURCES})

target_sources(${CMAKE_PROJECT_NAME} PRIVATE ${SOURCES})
target_link_libraries(${CMAKE_PROJECT_NAME} ${CMAKE_THREAD_LIBS_INIT})

if(WIN32)
  target_link_libraries(${CMAKE_PROJECT_NAME} ws2_32 iphlpapi)
endif(WIN32)
