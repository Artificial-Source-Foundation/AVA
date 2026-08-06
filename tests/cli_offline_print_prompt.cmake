if(NOT DEFINED AVA_EXE)
  message(FATAL_ERROR "AVA_EXE is required")
endif()

if(NOT DEFINED AVA_CLI_TEST_ROOT)
  message(FATAL_ERROR "AVA_CLI_TEST_ROOT is required")
endif()

include("${CMAKE_CURRENT_LIST_DIR}/test_timeout.cmake")
ava_test_timeout(AVA_PROCESS_TIMEOUT 10)

get_filename_component(TEST_ROOT "${AVA_CLI_TEST_ROOT}" ABSOLUTE)
set(WORKSPACE "${TEST_ROOT}/workspace")
set(HOME_DIR "${TEST_ROOT}/home")
set(CONFIG_DIR "${TEST_ROOT}/config")
set(STATE_DIR "${TEST_ROOT}/state")
set(DATA_DIR "${TEST_ROOT}/data")

file(REMOVE_RECURSE "${TEST_ROOT}")
file(MAKE_DIRECTORY "${WORKSPACE}" "${HOME_DIR}" "${CONFIG_DIR}/ava" "${STATE_DIR}" "${DATA_DIR}")

execute_process(
  COMMAND "${CMAKE_COMMAND}" -E env
          "HOME=${HOME_DIR}"
          "XDG_CONFIG_HOME=${CONFIG_DIR}"
          "XDG_STATE_HOME=${STATE_DIR}"
          "XDG_DATA_HOME=${DATA_DIR}"
          "NO_COLOR=1"
          "${AVA_EXE}" --offline --print "hello offline"
  WORKING_DIRECTORY "${WORKSPACE}"
  OUTPUT_VARIABLE AVA_OUTPUT
  ERROR_VARIABLE AVA_ERROR
  RESULT_VARIABLE AVA_RESULT
  TIMEOUT "${AVA_PROCESS_TIMEOUT}"
)

if(NOT AVA_RESULT EQUAL 1)
  message(FATAL_ERROR "offline print prompt exited with ${AVA_RESULT}; expected 1\nstdout:\n${AVA_OUTPUT}\nstderr:\n${AVA_ERROR}")
endif()

string(FIND "${AVA_ERROR}" "offline mode is enabled" OFFLINE_INDEX)
if(OFFLINE_INDEX EQUAL -1)
  message(FATAL_ERROR "offline print prompt did not report offline mode\nstdout:\n${AVA_OUTPUT}\nstderr:\n${AVA_ERROR}")
endif()

string(FIND "${AVA_ERROR}" "requires auth" AUTH_INDEX)
if(NOT AUTH_INDEX EQUAL -1)
  message(FATAL_ERROR "offline print prompt checked auth before failing offline\nstdout:\n${AVA_OUTPUT}\nstderr:\n${AVA_ERROR}")
endif()

if(NOT AVA_OUTPUT STREQUAL "")
  message(FATAL_ERROR "offline print prompt unexpectedly wrote stdout\nstdout:\n${AVA_OUTPUT}\nstderr:\n${AVA_ERROR}")
endif()
