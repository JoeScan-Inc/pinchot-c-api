if (${PINCHOT_API_ROOT_DIR} STREQUAL "")
  message(FATAL_ERROR "PINCHOT_API_ROOT_DIR not defined!")
endif()

# Get the latest abbreviated commit hash of the working branch
execute_process(
  COMMAND git
  -C ${PINCHOT_API_ROOT_DIR}
  log -1 --format=%h
  WORKING_DIRECTORY ${CMAKE_CURRENT_LIST_DIR}
  OUTPUT_VARIABLE GIT_HASH
  OUTPUT_STRIP_TRAILING_WHITESPACE
)

execute_process(
  COMMAND git
  -C ${PINCHOT_API_ROOT_DIR}
  diff --quiet --exit-code
  RESULT_VARIABLE GIT_DIFF_STATUS
)

if(GIT_DIFF_STATUS EQUAL "1")
  add_definitions("-DAPI_DIRTY_FLAG=\"-dirty\"")
else()
  add_definitions("-DAPI_DIRTY_FLAG=\"\"")
endif()

add_definitions("-DAPI_GIT_HASH=\"+${GIT_HASH}\"")
