if(NOT DEFINED AVA_EXE)
  message(FATAL_ERROR "AVA_EXE is required")
endif()

if(NOT DEFINED AVA_CLI_TEST_ROOT)
  message(FATAL_ERROR "AVA_CLI_TEST_ROOT is required")
endif()

get_filename_component(TEST_ROOT "${AVA_CLI_TEST_ROOT}" ABSOLUTE)
set(WORKSPACE "${TEST_ROOT}/workspace")
set(INPUT_FILE "${TEST_ROOT}/rpc-input.jsonl")
set(PLUGIN_DIR "${WORKSPACE}/.ava/plugins/com.example.rpc")
set(BAD_PLUGIN_DIR "${WORKSPACE}/.ava/plugins/com.example.bad")
set(PLUGIN_MANIFEST "${PLUGIN_DIR}/plugin.json")
set(PLUGIN_SCRIPT "${PLUGIN_DIR}/plugin.sh")
set(PLUGIN_RAN_FILE "${PLUGIN_DIR}/ran.txt")

file(REMOVE_RECURSE "${TEST_ROOT}")
file(MAKE_DIRECTORY "${PLUGIN_DIR}/prompts" "${PLUGIN_DIR}/skills" "${BAD_PLUGIN_DIR}" "${TEST_ROOT}/home"
                    "${TEST_ROOT}/config" "${TEST_ROOT}/state" "${TEST_ROOT}/data")
file(WRITE "${PLUGIN_MANIFEST}"
     [=[
{
  "schema_version": 1,
  "id": "com.example.rpc",
  "name": "RPC Plugin",
  "version": "0.1.0",
  "api_version": "ava.plugin.v1",
  "entrypoint": {"command": "/bin/sh", "args": ["plugin.sh"]},
  "capabilities": ["commands"],
  "contributes": {
    "commands": [{"name": "status", "description": "Show status"}],
    "prompts": [{"name": "review", "description": "Review prompt", "path": "prompts/review.md"}],
    "skills": [{"name": "triage", "description": "Triage skill", "path": "skills/triage.md"}]
  }
}
]=])
file(WRITE "${PLUGIN_SCRIPT}"
     "printf '%s\n' plugin-entrypoint-ran > ran.txt\n"
     "while read line; do :; done\n")
file(WRITE "${PLUGIN_DIR}/prompts/review.md" "Prompt body from plugin\n")
file(WRITE "${PLUGIN_DIR}/skills/triage.md" "Skill body from plugin\n")
file(WRITE "${BAD_PLUGIN_DIR}/plugin.json" "{not-json\n")
file(WRITE "${INPUT_FILE}"
     "{\"id\":\"plugins\",\"type\":\"list_plugins\",\"protocol_version\":1}\n"
     "{\"id\":\"failures\",\"type\":\"plugin_failures\"}\n"
     "{\"id\":\"inspect\",\"type\":\"inspect_plugin\",\"plugin_id\":\"com.example.rpc\"}\n"
     "{\"id\":\"validate\",\"type\":\"validate_plugin\",\"path\":\".ava/plugins/com.example.rpc/plugin.json\"}\n"
     "{\"id\":\"prompts\",\"type\":\"list_plugin_prompts\",\"plugin_id\":\"com.example.rpc\"}\n"
     "{\"id\":\"prompt\",\"type\":\"get_plugin_prompt\",\"plugin_id\":\"com.example.rpc\",\"name\":\"review\"}\n"
     "{\"id\":\"skills\",\"type\":\"list_plugin_skills\",\"plugin_id\":\"com.example.rpc\"}\n"
     "{\"id\":\"skill\",\"type\":\"get_plugin_skill\",\"plugin_id\":\"com.example.rpc\",\"name\":\"triage\"}\n"
     "{\"id\":\"enable\",\"type\":\"enable_plugin\",\"plugin_id\":\"com.example.rpc\"}\n"
     "{\"id\":\"run\",\"type\":\"run_plugin_command\",\"plugin_id\":\"com.example.rpc\",\"name\":\"status\","
     "\"arguments\":\"{}\"}\n"
     "{\"id\":\"disable\",\"type\":\"disable_plugin\",\"plugin_id\":\"com.example.rpc\"}\n"
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
        "\"id\":\"plugins\""
        "com.example.rpc"
        "RPC Plugin"
        "0.1.0"
        "Failures: 1"
        "\"id\":\"failures\""
        "plugin manifest must be a valid JSON object"
        "\"id\":\"inspect\""
        "entrypoint: /bin/sh plugin.sh (not executed)"
        "no plugin process is started yet"
        "\"id\":\"validate\""
        "Valid plugin manifest"
        "no entrypoint was executed"
        "\"id\":\"prompts\""
        "review - Review prompt"
        "\"id\":\"prompt\""
        "Prompt body from plugin"
        "\"id\":\"skills\""
        "triage - Triage skill"
        "\"id\":\"skill\""
        "Skill body from plugin"
        "\"id\":\"enable\""
        "Enabled project plugin com.example.rpc."
        "No plugin process was started."
        "\"id\":\"run\""
        "permission_denied: plugin command requires permission"
        "plugin subprocess execution requires explicit approval"
        "command: com.example.rpc"
        "resolution: deny"
        "\"id\":\"disable\""
        "Disabled project plugin com.example.rpc."
        "No plugin process was stopped."
        "\"id\":\"state\""
        "\"workspace_dir\":\"${WORKSPACE}\"")
  string(FIND "${AVA_OUTPUT}" "${NEEDLE}" NEEDLE_INDEX)
  if(NEEDLE_INDEX EQUAL -1)
    message(FATAL_ERROR "ava --rpc output did not contain ${NEEDLE}\nstdout:\n${AVA_OUTPUT}\nstderr:\n${AVA_ERROR}")
  endif()
endforeach()

if(EXISTS "${PLUGIN_RAN_FILE}")
  message(FATAL_ERROR "fail-closed run_plugin_command executed plugin entrypoint unexpectedly\nstdout:\n${AVA_OUTPUT}")
endif()
