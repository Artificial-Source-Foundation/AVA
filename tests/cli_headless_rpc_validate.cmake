if(NOT DEFINED AVA_EXE)
  message(FATAL_ERROR "AVA_EXE is required")
endif()

if(NOT DEFINED AVA_CLI_TEST_ROOT)
  message(FATAL_ERROR "AVA_CLI_TEST_ROOT is required")
endif()

get_filename_component(TEST_ROOT "${AVA_CLI_TEST_ROOT}" ABSOLUTE)
set(WORKSPACE "${TEST_ROOT}/workspace")
set(INPUT_FILE "${TEST_ROOT}/rpc-input.jsonl")

file(REMOVE_RECURSE "${TEST_ROOT}")
file(MAKE_DIRECTORY "${WORKSPACE}" "${TEST_ROOT}/home" "${TEST_ROOT}/config" "${TEST_ROOT}/state" "${TEST_ROOT}/data")
file(WRITE "${INPUT_FILE}"
     "{\"id\":\"proto\",\"type\":\"get_protocol\",\"protocol_version\":1}\n"
     "{\"id\":\"validate\",\"type\":\"validate_session\"}\n"
     "{\"id\":\"stats\",\"type\":\"get_session_stats\"}\n")

execute_process(
  COMMAND "${CMAKE_COMMAND}" -E env
          "HOME=${TEST_ROOT}/home"
          "XDG_CONFIG_HOME=${TEST_ROOT}/config"
          "XDG_STATE_HOME=${TEST_ROOT}/state"
          "XDG_DATA_HOME=${TEST_ROOT}/data"
          "NO_COLOR=1"
          "${AVA_EXE}" --rpc
  WORKING_DIRECTORY "${WORKSPACE}"
  INPUT_FILE "${INPUT_FILE}"
  OUTPUT_VARIABLE AVA_OUTPUT
  ERROR_VARIABLE AVA_ERROR
  RESULT_VARIABLE AVA_RESULT
  TIMEOUT 15
)

if(NOT AVA_RESULT EQUAL 0)
  message(FATAL_ERROR "ava --rpc exited with ${AVA_RESULT}\nstdout:\n${AVA_OUTPUT}\nstderr:\n${AVA_ERROR}")
endif()

foreach(NEEDLE
        "\"id\":\"proto\""
        "\"protocol_version\":1"
        "\"id\":\"validate\""
        "\"ok\":true"
        "\"error_count\":0"
        "\"issues\":[]"
        "\"id\":\"stats\""
        "\"entry_count\":1")
  string(FIND "${AVA_OUTPUT}" "${NEEDLE}" NEEDLE_INDEX)
  if(NEEDLE_INDEX EQUAL -1)
    message(FATAL_ERROR "ava --rpc output did not contain ${NEEDLE}\nstdout:\n${AVA_OUTPUT}\nstderr:\n${AVA_ERROR}")
  endif()
endforeach()
