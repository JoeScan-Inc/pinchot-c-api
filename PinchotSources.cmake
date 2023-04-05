if (${PINCHOT_API_ROOT_DIR} STREQUAL "")
  message(FATAL_ERROR "PINCHOT_API_ROOT_DIR not defined!")
endif()

# source directory
set(SRC_DIR ${PINCHOT_API_ROOT_DIR}/src)
file(GLOB C_API_SOURCES ${SRC_DIR}/*.cpp)
include_directories(${SRC_DIR})

# third-party libraries
set(THIRD_PARTY_LIB_DIR ${PINCHOT_API_ROOT_DIR}/third-party)
set(CXXOPTS_DIR ${THIRD_PARTY_LIB_DIR}/cxxopts-b0f67a06de3446aa97a4943ad0ad6086460b2b61/include)
set(JSON_DIR ${THIRD_PARTY_LIB_DIR}/json-3.3.7)
set(RDWRQ_DIR ${THIRD_PARTY_LIB_DIR}/readerwriterqueue-6b5cca00b3a4d2ec539e0ac996482b3e8aae5a84)
include_directories(
  ${BETTER_ENUMS_DIR}
  ${CXXOPTS_DIR}
  ${JSON_DIR}
  ${RDWRQ_DIR}
)

set(BOOST_ROOT ${THIRD_PARTY_LIB_DIR}/boost-1.70.0)
find_package(Boost)
include_directories(${Boost_INCLUDE_DIRS})
link_directories(${Boost_LIBRARY_DIRS})

set(CMAKE_THREAD_PREFER_PTHREAD TRUE)
set(THREADS_PREFER_PTHREAD_FLAG TRUE)
find_package(Threads REQUIRED)
