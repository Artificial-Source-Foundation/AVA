if(NOT DEFINED AVA_EXE)
  message(FATAL_ERROR "AVA_EXE is required")
endif()

if(NOT DEFINED AVA_FAKE_MCP_SERVER_EXE)
  message(FATAL_ERROR "AVA_FAKE_MCP_SERVER_EXE is required")
endif()

if(NOT DEFINED AVA_CLI_TEST_ROOT)
  message(FATAL_ERROR "AVA_CLI_TEST_ROOT is required")
endif()

# Timeouts for this driver. Honors AVA_DEBUG_NO_TIMEOUT at runtime (this
# script runs via `cmake -P`, so $ENV{...} is live) so a hung driver is not
# killed -- and does not kill its `ava` subprocess via the driver's EXIT
# trap -- before a debugger can be attached. Each AVA_TIMEOUT_<n> /
# AVA_POLL_<n> defaults to its authored value; setting AVA_DEBUG_NO_TIMEOUT
# stretches all of them to one hour (or AVA_DEBUG_NO_TIMEOUT_SECONDS). Poll
# caps are scaled at 20 iterations/second to match the sleep 0.05 loops.
set(AVA_TIMEOUT_15 15)
if(DEFINED ENV{AVA_DEBUG_NO_TIMEOUT})
  set(AVA_DEBUG_SECONDS 3600)
  if(DEFINED ENV{AVA_DEBUG_NO_TIMEOUT_SECONDS} AND "$ENV{AVA_DEBUG_NO_TIMEOUT_SECONDS}" MATCHES "^[1-9][0-9]*$")
    set(AVA_DEBUG_SECONDS "$ENV{AVA_DEBUG_NO_TIMEOUT_SECONDS}")
  endif()
  set(AVA_TIMEOUT_15 "${AVA_DEBUG_SECONDS}")
endif()

get_filename_component(TEST_ROOT "${AVA_CLI_TEST_ROOT}" ABSOLUTE)
get_filename_component(FAKE_MCP_SERVER "${AVA_FAKE_MCP_SERVER_EXE}" ABSOLUTE)
set(WORKSPACE "${TEST_ROOT}/workspace")
set(INPUT_FILE "${TEST_ROOT}/rpc-input.jsonl")
set(PROJECT_MCP_CONFIG "${WORKSPACE}/.ava/mcp.json")

string(REPLACE "\\" "\\\\" FAKE_MCP_SERVER_JSON "${FAKE_MCP_SERVER}")
string(REPLACE "\"" "\\\"" FAKE_MCP_SERVER_JSON "${FAKE_MCP_SERVER_JSON}")

file(REMOVE_RECURSE "${TEST_ROOT}")
file(MAKE_DIRECTORY "${WORKSPACE}/.ava" "${TEST_ROOT}/home" "${TEST_ROOT}/config" "${TEST_ROOT}/state/ava"
                    "${TEST_ROOT}/data")
file(REAL_PATH "${WORKSPACE}" REAL_WORKSPACE)
file(WRITE "${TEST_ROOT}/state/ava/project-trust.json"
     "{\"schema_version\":1,\"decisions\":[{\"path\":\"${REAL_WORKSPACE}\",\"trusted\":true}]}\n")
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
  TIMEOUT ${AVA_TIMEOUT_15}
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
        "\"workspace_dir\":\"${REAL_WORKSPACE}\"")
  string(FIND "${AVA_OUTPUT}" "${NEEDLE}" NEEDLE_INDEX)
  if(NEEDLE_INDEX EQUAL -1)
    message(FATAL_ERROR "ava --rpc output did not contain ${NEEDLE}\nstdout:\n${AVA_OUTPUT}\nstderr:\n${AVA_ERROR}")
  endif()
endforeach()
