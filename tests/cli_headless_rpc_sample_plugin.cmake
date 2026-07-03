if(NOT DEFINED AVA_EXE)
  message(FATAL_ERROR "AVA_EXE is required")
endif()

if(NOT DEFINED AVA_CLI_TEST_ROOT)
  message(FATAL_ERROR "AVA_CLI_TEST_ROOT is required")
endif()

if(NOT DEFINED AVA_SAMPLE_TODO_PLUGIN_DIR)
  message(FATAL_ERROR "AVA_SAMPLE_TODO_PLUGIN_DIR is required")
endif()

get_filename_component(TEST_ROOT "${AVA_CLI_TEST_ROOT}" ABSOLUTE)
get_filename_component(SAMPLE_TODO_PLUGIN_DIR "${AVA_SAMPLE_TODO_PLUGIN_DIR}" ABSOLUTE)
set(WORKSPACE "${TEST_ROOT}/workspace")
set(INPUT_FILE "${TEST_ROOT}/rpc-input.jsonl")
set(PLUGIN_DIR "${WORKSPACE}/.ava/plugins/com.example.todo")
set(PLUGIN_SCRIPT "${PLUGIN_DIR}/plugin.sh")
set(PLUGIN_RAN_FILE "${PLUGIN_DIR}/entrypoint-ran")

if(NOT EXISTS "${SAMPLE_TODO_PLUGIN_DIR}/plugin.json")
  message(FATAL_ERROR "sample todo plugin manifest not found: ${SAMPLE_TODO_PLUGIN_DIR}/plugin.json")
endif()

if(NOT EXISTS "${SAMPLE_TODO_PLUGIN_DIR}/plugin.sh")
  message(FATAL_ERROR "sample todo plugin entrypoint not found: ${SAMPLE_TODO_PLUGIN_DIR}/plugin.sh")
endif()

file(REMOVE_RECURSE "${TEST_ROOT}")
file(MAKE_DIRECTORY "${WORKSPACE}/.ava/plugins" "${TEST_ROOT}/home" "${TEST_ROOT}/config" "${TEST_ROOT}/state/ava"
                    "${TEST_ROOT}/data")
file(COPY "${SAMPLE_TODO_PLUGIN_DIR}/" DESTINATION "${PLUGIN_DIR}")
file(READ "${PLUGIN_SCRIPT}" PLUGIN_SCRIPT_ORIGINAL)
file(WRITE "${PLUGIN_SCRIPT}" "printf '%s\\n' plugin-entrypoint-ran > '${PLUGIN_RAN_FILE}'\n${PLUGIN_SCRIPT_ORIGINAL}")
file(REAL_PATH "${WORKSPACE}" REAL_WORKSPACE)
file(WRITE "${TEST_ROOT}/state/ava/project-trust.json"
     "{\"schema_version\":1,\"decisions\":[{\"path\":\"${REAL_WORKSPACE}\",\"trusted\":true}]}\n")

file(WRITE "${INPUT_FILE}"
     "{\"id\":\"plugins\",\"type\":\"list_plugins\",\"protocol_version\":1}\n"
     "{\"id\":\"inspect\",\"type\":\"inspect_plugin\",\"plugin_id\":\"com.example.todo\"}\n"
     "{\"id\":\"validate\",\"type\":\"validate_plugin\",\"path\":\".ava/plugins/com.example.todo/plugin.json\"}\n"
     "{\"id\":\"prompts\",\"type\":\"list_plugin_prompts\",\"plugin_id\":\"com.example.todo\"}\n"
     "{\"id\":\"prompt\",\"type\":\"get_plugin_prompt\",\"plugin_id\":\"com.example.todo\",\"name\":\"todo-review\"}\n"
     "{\"id\":\"skills\",\"type\":\"list_plugin_skills\",\"plugin_id\":\"com.example.todo\"}\n"
     "{\"id\":\"skill\",\"type\":\"get_plugin_skill\",\"plugin_id\":\"com.example.todo\",\"name\":\"todo-triage\"}\n"
     "{\"id\":\"enable\",\"type\":\"enable_plugin\",\"plugin_id\":\"com.example.todo\"}\n"
     "{\"id\":\"run\",\"type\":\"run_plugin_command\",\"plugin_id\":\"com.example.todo\",\"name\":\"status\","
     "\"arguments\":\"{}\"}\n"
     "{\"id\":\"disable\",\"type\":\"disable_plugin\",\"plugin_id\":\"com.example.todo\"}\n"
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
        "com.example.todo"
        "Todo Sample Plugin"
        "0.1.0"
        "\"id\":\"inspect\""
        "entrypoint: /bin/sh plugin.sh (not executed)"
        "capabilities: tools, commands, prompts, skills, event_hooks"
        "todo_add - Accept one todo item for the current AVA call."
        "status - Report that the sample plugin is ready."
        "todo-review - Review todo-oriented follow-up work."
        "todo-triage - Triage a small implementation todo list."
        "event_hooks: 1"
        "no plugin process is started yet"
        "\"id\":\"validate\""
        "Valid plugin manifest"
        "api_version: ava.plugin.v1"
        "tools: 1"
        "commands: 1"
        "prompts: 1"
        "skills: 1"
        "note: validation parsed the manifest only; no entrypoint was executed."
        "\"id\":\"prompts\""
        "Plugin prompts for com.example.todo"
        "\"id\":\"prompt\""
        "Plugin prompt com.example.todo/todo-review"
        "# Todo Review Prompt"
        "Identify the next concrete action."
        "\"id\":\"skills\""
        "Plugin skills for com.example.todo"
        "\"id\":\"skill\""
        "Plugin skill com.example.todo/todo-triage"
        "# Todo Triage Skill"
        "Group related todos by outcome."
        "\"id\":\"enable\""
        "Enabled project plugin com.example.todo. No plugin process was started."
        "\"id\":\"run\""
        "permission_denied: plugin command requires permission"
        "plugin subprocess execution requires explicit approval"
        "command: com.example.todo"
        "resolution: deny"
        "\"id\":\"disable\""
        "Disabled project plugin com.example.todo. No plugin process was stopped."
        "\"id\":\"state\""
        "\"workspace_dir\":\"${REAL_WORKSPACE}\""
        "\"current_dir\":\"${REAL_WORKSPACE}\"")
  string(FIND "${AVA_OUTPUT}" "${NEEDLE}" NEEDLE_INDEX)
  if(NEEDLE_INDEX EQUAL -1)
    message(FATAL_ERROR "ava --rpc output did not contain ${NEEDLE}\nstdout:\n${AVA_OUTPUT}\nstderr:\n${AVA_ERROR}")
  endif()
endforeach()

if(EXISTS "${PLUGIN_RAN_FILE}")
  message(FATAL_ERROR "fail-closed run_plugin_command executed sample plugin entrypoint unexpectedly\nstdout:\n${AVA_OUTPUT}")
endif()
