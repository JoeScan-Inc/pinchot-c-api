cmake_minimum_required(VERSION 3.8)
project(discover)

file(GLOB PROJECT_SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp)
add_executable(${CMAKE_PROJECT_NAME} ${PROJECT_SOURCES})

set(PINCHOT_API_ROOT_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../../")
list(APPEND CMAKE_MODULE_PATH ${PINCHOT_API_ROOT_DIR})
include(PinchotBuildApplication)
