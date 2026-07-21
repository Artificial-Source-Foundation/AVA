if(NOT DEFINED AVA_EXE)
  message(FATAL_ERROR "AVA_EXE is required")
endif()

if(NOT DEFINED AVA_FAKE_PROVIDER_EXE)
  message(FATAL_ERROR "AVA_FAKE_PROVIDER_EXE is required")
endif()

if(NOT DEFINED AVA_CLI_TEST_ROOT)
  message(FATAL_ERROR "AVA_CLI_TEST_ROOT is required")
endif()

get_filename_component(TEST_ROOT "${AVA_CLI_TEST_ROOT}" ABSOLUTE)
set(WORKSPACE "${TEST_ROOT}/workspace")
set(HOME_DIR "${TEST_ROOT}/home")
set(CONFIG_DIR "${TEST_ROOT}/config")
set(STATE_DIR "${TEST_ROOT}/state")
set(DATA_DIR "${TEST_ROOT}/data")
set(PORT_FILE "${TEST_ROOT}/provider-port")
set(REQUEST_LOG "${TEST_ROOT}/provider-request.log")
set(PROVIDER_OUT "${TEST_ROOT}/provider.out")
set(PROVIDER_ERR "${TEST_ROOT}/provider.err")
set(RPC_IN "${TEST_ROOT}/rpc-input.fifo")
set(RPC_OUT "${TEST_ROOT}/rpc-output.jsonl")
set(RPC_ERR "${TEST_ROOT}/rpc-error.log")
set(REPLAY_INPUT "${TEST_ROOT}/replay-input.jsonl")
set(DRIVER_FILE "${TEST_ROOT}/driver.sh")

file(REMOVE_RECURSE "${TEST_ROOT}")
file(MAKE_DIRECTORY "${WORKSPACE}/src" "${WORKSPACE}/docs" "${HOME_DIR}" "${CONFIG_DIR}/ava" "${STATE_DIR}" "${DATA_DIR}")
file(WRITE "${WORKSPACE}/AGENTS.md" "headless performance smoke project instructions\n")
file(WRITE "${WORKSPACE}/src/a.txt" "needle alpha\nquiet line\n")
file(WRITE "${WORKSPACE}/src/b.txt" "quiet line\nneedle beta\n")
file(WRITE "${WORKSPACE}/docs/readme.md" "needle outside include should not be searched\n")
foreach(INDEX RANGE 1 30)
  file(WRITE "${WORKSPACE}/src/noise-${INDEX}.txt" "noise ${INDEX}\n")
endforeach()

file(WRITE "${CONFIG_DIR}/ava/models.json"
     "{\"default_provider\":\"moonshot\",\"default_model\":\"ava-headless-fake\","
     "\"models\":[{\"provider\":\"moonshot\",\"id\":\"ava-headless-fake\",\"family\":\"fake\","
     "\"context_window_tokens\":8192,\"max_output_tokens\":1024,\"supports_tools\":true,"
     "\"supports_streaming\":false,\"supports_reasoning\":false,\"reports_usage\":true}]}\n")

file(WRITE "${DRIVER_FILE}"
"#!/bin/sh\n"
"set -u\n"
"ava_pid=\n"
"provider_pid=\n"
"cleanup() {\n"
"  if [ -n \"$ava_pid\" ]; then kill \"$ava_pid\" 2>/dev/null || true; fi\n"
"  if [ -n \"$provider_pid\" ]; then kill \"$provider_pid\" 2>/dev/null || true; fi\n"
"}\n"
"trap cleanup EXIT INT TERM\n"
"rm -f \"${RPC_IN}\" \"${RPC_OUT}\" \"${RPC_ERR}\" \"${PORT_FILE}\" \"${REQUEST_LOG}\" \"${PROVIDER_OUT}\" \"${PROVIDER_ERR}\"\n"
"\"${AVA_FAKE_PROVIDER_EXE}\" \"${PORT_FILE}\" \"${REQUEST_LOG}\" 0 grep-tool \"src/**/*.txt\" > \"${PROVIDER_OUT}\" 2> \"${PROVIDER_ERR}\" &\n"
"provider_pid=$!\n"
"i=0\n"
"while [ ! -s \"${PORT_FILE}\" ]; do\n"
"  if ! kill -0 \"$provider_pid\" 2>/dev/null; then\n"
"    echo \"fake provider exited before writing a port\" >&2\n"
"    cat \"${PROVIDER_ERR}\" >&2 2>/dev/null || true\n"
"    exit 1\n"
"  fi\n"
"  i=$((i + 1))\n"
"  if [ \"$i\" -gt 200 ]; then\n"
"    echo \"timed out waiting for fake provider port\" >&2\n"
"    exit 1\n"
"  fi\n"
"  sleep 0.05\n"
"done\n"
"port=$(cat \"${PORT_FILE}\")\n"
"mkfifo \"${RPC_IN}\"\n"
"HOME=\"${HOME_DIR}\" XDG_CONFIG_HOME=\"${CONFIG_DIR}\" XDG_STATE_HOME=\"${STATE_DIR}\" XDG_DATA_HOME=\"${DATA_DIR}\" NO_COLOR=1 MOONSHOT_API_KEY=test-key MOONSHOT_BASE_URL=\"http://127.0.0.1:$port\" \"${AVA_EXE}\" --rpc --allow-tool grep < \"${RPC_IN}\" > \"${RPC_OUT}\" 2> \"${RPC_ERR}\" &\n"
"ava_pid=$!\n"
"exec 3>\"${RPC_IN}\"\n"
"printf '%s\\n' '{\"id\":\"prompt\",\"type\":\"prompt\",\"protocol_version\":1,\"message\":\"search for needle through grep\"}' >&3\n"
"i=0\n"
"while ! grep -q 'after grep tool' \"${RPC_OUT}\" 2>/dev/null; do\n"
"  if ! kill -0 \"$ava_pid\" 2>/dev/null; then\n"
"    echo \"ava exited before grep prompt completed\" >&2\n"
"    cat \"${RPC_OUT}\" >&2 2>/dev/null || true\n"
"    cat \"${RPC_ERR}\" >&2 2>/dev/null || true\n"
"    exit 1\n"
"  fi\n"
"  i=$((i + 1))\n"
"  if [ \"$i\" -gt 200 ]; then\n"
"    echo \"timed out waiting for grep prompt completion\" >&2\n"
"    cat \"${RPC_OUT}\" >&2 2>/dev/null || true\n"
"    cat \"${RPC_ERR}\" >&2 2>/dev/null || true\n"
"    exit 1\n"
"  fi\n"
"  sleep 0.05\n"
"done\n"
"printf '%s\\n' '{\"id\":\"stats-after\",\"type\":\"get_session_stats\"}' >&3\n"
"printf '%s\\n' '{\"id\":\"validate-after\",\"type\":\"validate_session\"}' >&3\n"
"printf '%s\\n' '{\"id\":\"messages-after\",\"type\":\"get_messages\"}' >&3\n"
"printf '%s\\n' '{\"id\":\"sessions-after\",\"type\":\"list_sessions\"}' >&3\n"
"exec 3>&-\n"
"wait \"$ava_pid\"\n"
"ava_status=$?\n"
"ava_pid=\n"
"wait \"$provider_pid\"\n"
"provider_status=$?\n"
"provider_pid=\n"
"if [ \"$ava_status\" -ne 0 ]; then\n"
"  echo \"ava --rpc exited with $ava_status\" >&2\n"
"  cat \"${RPC_OUT}\" >&2 2>/dev/null || true\n"
"  cat \"${RPC_ERR}\" >&2 2>/dev/null || true\n"
"  exit \"$ava_status\"\n"
"fi\n"
"if [ \"$provider_status\" -ne 0 ]; then\n"
"  echo \"fake provider exited with $provider_status\" >&2\n"
"  cat \"${PROVIDER_ERR}\" >&2 2>/dev/null || true\n"
"  exit \"$provider_status\"\n"
"fi\n")

execute_process(
  COMMAND /bin/sh "${DRIVER_FILE}"
  WORKING_DIRECTORY "${WORKSPACE}"
  OUTPUT_VARIABLE DRIVER_OUTPUT
  ERROR_VARIABLE DRIVER_ERROR
  RESULT_VARIABLE DRIVER_RESULT
  TIMEOUT 30
)

if(NOT DRIVER_RESULT EQUAL 0)
  message(FATAL_ERROR "headless startup/search smoke driver exited with ${DRIVER_RESULT}\nstdout:\n${DRIVER_OUTPUT}\nstderr:\n${DRIVER_ERROR}")
endif()

file(READ "${RPC_OUT}" AVA_OUTPUT)
file(READ "${RPC_ERR}" AVA_ERROR)
file(READ "${REQUEST_LOG}" PROVIDER_REQUEST)

foreach(NEEDLE
        "--- request 1 ---"
        "--- request 2 ---"
        "\"model\":\"ava-headless-fake\""
        "\"name\":\"grep\""
        "\\\"pattern\\\":\\\"needle\\\""
        "\\\"include\\\":\\\"src/**/*.txt\\\""
        "\"tool_call_id\":\"call_grep\""
        "\\\"tool\\\":\\\"grep\\\""
        "\\\"ok\\\":true"
        "needle alpha"
        "needle beta")
  string(FIND "${PROVIDER_REQUEST}" "${NEEDLE}" NEEDLE_INDEX)
  if(NEEDLE_INDEX EQUAL -1)
    message(FATAL_ERROR "fake provider request log did not contain ${NEEDLE}\nrequest:\n${PROVIDER_REQUEST}\nstderr:\n${AVA_ERROR}")
  endif()
endforeach()

foreach(NEEDLE
        "\"name\":\"tool_start\""
        "\"call_id\":\"call_grep\""
        "\"tool\":\"grep\""
        "\"name\":\"tool_result\""
        "\"status\":\"success\""
        "\"structured_result\":{\"schema_version\":1"
        "\"ok\":true"
        "\"content_type\":\"application/json\""
        "needle alpha"
        "needle beta"
        "\"id\":\"prompt\""
        "\"success\":true"
        "\"final_text\":\"after grep tool\""
        "\"id\":\"stats-after\""
        "\"tool_call\":1"
        "\"tool_result\":1"
        "\"id\":\"validate-after\""
        "\"ok\":true"
        "\"id\":\"messages-after\""
        "\"name\":\"tool_start\""
        "\"type\":\"tool_result\""
        "\"id\":\"sessions-after\""
        "\"sessions\":[")
  string(FIND "${AVA_OUTPUT}" "${NEEDLE}" NEEDLE_INDEX)
  if(NEEDLE_INDEX EQUAL -1)
    message(FATAL_ERROR "ava --rpc output did not contain ${NEEDLE}\nstdout:\n${AVA_OUTPUT}\nstderr:\n${AVA_ERROR}")
  endif()
endforeach()

file(WRITE "${REPLAY_INPUT}"
     "{\"id\":\"replay-state\",\"type\":\"get_state\"}\n"
     "{\"id\":\"replay-stats\",\"type\":\"get_session_stats\"}\n"
     "{\"id\":\"replay-validate\",\"type\":\"validate_session\"}\n"
     "{\"id\":\"replay-messages\",\"type\":\"get_messages\"}\n"
     "{\"id\":\"replay-sessions\",\"type\":\"list_sessions\"}\n")

execute_process(
  COMMAND "${CMAKE_COMMAND}" -E env
          "HOME=${HOME_DIR}"
          "XDG_CONFIG_HOME=${CONFIG_DIR}"
          "XDG_STATE_HOME=${STATE_DIR}"
          "XDG_DATA_HOME=${DATA_DIR}"
          "NO_COLOR=1"
          "${AVA_EXE}" --rpc --continue
  WORKING_DIRECTORY "${WORKSPACE}"
  INPUT_FILE "${REPLAY_INPUT}"
  OUTPUT_VARIABLE REPLAY_OUTPUT
  ERROR_VARIABLE REPLAY_ERROR
  RESULT_VARIABLE REPLAY_RESULT
  TIMEOUT 15
)

if(NOT REPLAY_RESULT EQUAL 0)
  message(FATAL_ERROR "ava --rpc --continue exited with ${REPLAY_RESULT}\nstdout:\n${REPLAY_OUTPUT}\nstderr:\n${REPLAY_ERROR}")
endif()

foreach(NEEDLE
        "\"id\":\"replay-state\""
        "\"id\":\"replay-stats\""
        "\"tool_call\":1"
        "\"tool_result\":1"
        "\"id\":\"replay-validate\""
        "\"ok\":true"
        "\"id\":\"replay-messages\""
        "search for needle through grep"
        "needle alpha"
        "needle beta"
        "\"type\":\"tool_result\""
        "\"id\":\"replay-sessions\""
        "\"sessions\":[")
  string(FIND "${REPLAY_OUTPUT}" "${NEEDLE}" NEEDLE_INDEX)
  if(NEEDLE_INDEX EQUAL -1)
    message(FATAL_ERROR "ava --rpc --continue output did not contain ${NEEDLE}\nstdout:\n${REPLAY_OUTPUT}\nstderr:\n${REPLAY_ERROR}")
  endif()
endforeach()
