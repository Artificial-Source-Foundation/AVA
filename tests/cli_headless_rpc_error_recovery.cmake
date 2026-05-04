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
     "not json\n"
     "{\"id\":\"badver\",\"type\":\"get_state\",\"protocol_version\":999}\n"
     "{\"id\":\"badver-type\",\"type\":\"get_state\",\"protocol_version\":\"1\"}\n"
     "{\"id\":\"missing-type\"}\n"
     "{\"id\":\"unknown\",\"type\":\"does_not_exist\"}\n"
     "{\"id\":\"reply-missing\",\"type\":\"permission_reply\"}\n"
     "{\"id\":\"state\",\"type\":\"get_state\",\"protocol_version\":1}\n")

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
        "\"id\":\"\""
        "malformed RPC JSON object"
        "\"id\":\"badver\""
        "unsupported RPC protocol version"
        "\"id\":\"badver-type\""
        "RPC protocol_version must be an integer"
        "\"id\":\"missing-type\""
        "RPC request requires a non-empty string type"
        "\"id\":\"unknown\""
        "unknown RPC command type"
        "\"id\":\"reply-missing\""
        "permission_reply requires request_id"
        "\"id\":\"state\""
        "\"success\":true"
        "\"protocol_version\":1"
        "\"cancel_requested\":false")
  string(FIND "${AVA_OUTPUT}" "${NEEDLE}" NEEDLE_INDEX)
  if(NEEDLE_INDEX EQUAL -1)
    message(FATAL_ERROR "ava --rpc output did not contain ${NEEDLE}\nstdout:\n${AVA_OUTPUT}\nstderr:\n${AVA_ERROR}")
  endif()
endforeach()
