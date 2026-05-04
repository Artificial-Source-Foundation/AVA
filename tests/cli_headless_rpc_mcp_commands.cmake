if(NOT DEFINED AVA_EXE)
  message(FATAL_ERROR "AVA_EXE is required")
endif()

if(NOT DEFINED AVA_FAKE_MCP_SERVER_EXE)
  message(FATAL_ERROR "AVA_FAKE_MCP_SERVER_EXE is required")
endif()

if(NOT DEFINED AVA_CLI_TEST_ROOT)
  message(FATAL_ERROR "AVA_CLI_TEST_ROOT is required")
endif()

get_filename_component(TEST_ROOT "${AVA_CLI_TEST_ROOT}" ABSOLUTE)
get_filename_component(FAKE_MCP_SERVER "${AVA_FAKE_MCP_SERVER_EXE}" ABSOLUTE)
set(WORKSPACE "${TEST_ROOT}/workspace")
set(INPUT_FILE "${TEST_ROOT}/rpc-input.jsonl")
set(PROJECT_MCP_CONFIG "${WORKSPACE}/.ava/mcp.json")

string(REPLACE "\\" "\\\\" FAKE_MCP_SERVER_JSON "${FAKE_MCP_SERVER}")
string(REPLACE "\"" "\\\"" FAKE_MCP_SERVER_JSON "${FAKE_MCP_SERVER_JSON}")

file(REMOVE_RECURSE "${TEST_ROOT}")
file(MAKE_DIRECTORY "${WORKSPACE}/.ava" "${TEST_ROOT}/home" "${TEST_ROOT}/config" "${TEST_ROOT}/state"
                    "${TEST_ROOT}/data")
file(WRITE "${PROJECT_MCP_CONFIG}"
     "{\"servers\":[{\"id\":\"demo\",\"name\":\"Demo MCP\",\"command\":\"${FAKE_MCP_SERVER_JSON}\","
     "\"enabled\":true}]}\n")
file(WRITE "${INPUT_FILE}"
     "{\"id\":\"mcp-list\",\"type\":\"list_mcp_servers\",\"protocol_version\":1}\n"
     "{\"id\":\"mcp-inspect\",\"type\":\"inspect_mcp_server\",\"server_id\":\"demo\"}\n"
     "{\"id\":\"mcp-tools\",\"type\":\"list_mcp_tools\",\"server_id\":\"demo\"}\n"
     "{\"id\":\"mcp-restart\",\"type\":\"restart_mcp_server\",\"server_id\":\"demo\"}\n"
     "{\"id\":\"state\",\"type\":\"get_state\"}\n")

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
        "\"id\":\"mcp-list\""
        "\"id\":\"mcp-inspect\""
        "\"id\":\"mcp-tools\""
        "\"id\":\"mcp-restart\""
        "\"id\":\"state\""
        "\"success\":true"
        "MCP servers:"
        "demo  enabled  project  Demo MCP"
        "MCP server demo"
        "command: ${FAKE_MCP_SERVER}"
        "MCP server launch requires permission"
        "permission_denied"
        "resolution: deny"
        "MCP server demo uses per-request stdio processes"
        "\"workspace_dir\":\"${WORKSPACE}\"")
  string(FIND "${AVA_OUTPUT}" "${NEEDLE}" NEEDLE_INDEX)
  if(NEEDLE_INDEX EQUAL -1)
    message(FATAL_ERROR "ava --rpc output did not contain ${NEEDLE}\nstdout:\n${AVA_OUTPUT}\nstderr:\n${AVA_ERROR}")
  endif()
endforeach()
