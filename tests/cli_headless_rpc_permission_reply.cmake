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
set(TARGET_FILE "${TEST_ROOT}/created-by-tool.txt")
set(PORT_FILE "${TEST_ROOT}/provider-port")
set(REQUEST_LOG "${TEST_ROOT}/provider-request.log")
set(PROVIDER_OUT "${TEST_ROOT}/provider.out")
set(PROVIDER_ERR "${TEST_ROOT}/provider.err")
set(RPC_IN "${TEST_ROOT}/rpc-input.fifo")
set(RPC_OUT "${TEST_ROOT}/rpc-output.jsonl")
set(RPC_ERR "${TEST_ROOT}/rpc-error.log")
set(DRIVER_FILE "${TEST_ROOT}/driver.sh")

file(REMOVE_RECURSE "${TEST_ROOT}")
file(MAKE_DIRECTORY "${WORKSPACE}" "${HOME_DIR}" "${CONFIG_DIR}/ava" "${STATE_DIR}" "${DATA_DIR}")
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
"\"${AVA_FAKE_PROVIDER_EXE}\" \"${PORT_FILE}\" \"${REQUEST_LOG}\" 0 write-tool \"${TARGET_FILE}\" > \"${PROVIDER_OUT}\" 2> \"${PROVIDER_ERR}\" &\n"
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
"HOME=\"${HOME_DIR}\" XDG_CONFIG_HOME=\"${CONFIG_DIR}\" XDG_STATE_HOME=\"${STATE_DIR}\" XDG_DATA_HOME=\"${DATA_DIR}\" NO_COLOR=1 MOONSHOT_API_KEY=test-key MOONSHOT_BASE_URL=\"http://127.0.0.1:$port\" \"${AVA_EXE}\" --rpc < \"${RPC_IN}\" > \"${RPC_OUT}\" 2> \"${RPC_ERR}\" &\n"
"ava_pid=$!\n"
"exec 3>\"${RPC_IN}\"\n"
"printf '%s\\n' '{\"id\":\"prompt\",\"type\":\"prompt\",\"protocol_version\":1,\"message\":\"write outside with reply\"}' >&3\n"
"resolver_id=\n"
"i=0\n"
"while [ -z \"$resolver_id\" ]; do\n"
"  if [ -s \"${RPC_OUT}\" ]; then\n"
"    resolver_id=$(sed -n 's/.*\"resolver_request_id\":\"\\([^\"]*\\)\".*/\\1/p' \"${RPC_OUT}\" | head -n 1)\n"
"  fi\n"
"  if [ -n \"$resolver_id\" ]; then break; fi\n"
"  if ! kill -0 \"$ava_pid\" 2>/dev/null; then\n"
"    echo \"ava exited before permission request\" >&2\n"
"    cat \"${RPC_OUT}\" >&2 2>/dev/null || true\n"
"    cat \"${RPC_ERR}\" >&2 2>/dev/null || true\n"
"    exit 1\n"
"  fi\n"
"  i=$((i + 1))\n"
"  if [ \"$i\" -gt 200 ]; then\n"
"    echo \"timed out waiting for permission request\" >&2\n"
"    cat \"${RPC_OUT}\" >&2 2>/dev/null || true\n"
"    cat \"${RPC_ERR}\" >&2 2>/dev/null || true\n"
"    exit 1\n"
"  fi\n"
"  sleep 0.05\n"
"done\n"
"printf '%s\\n' \"{\\\"id\\\":\\\"reply\\\",\\\"type\\\":\\\"permission_reply\\\",\\\"request_id\\\":\\\"$resolver_id\\\",\\\"correlation_id\\\":\\\"prompt\\\",\\\"decision\\\":\\\"deny\\\",\\\"reason\\\":\\\"not approved by headless client\\\"}\" >&3\n"
"i=0\n"
"while ! grep -q 'after permission deny' \"${RPC_OUT}\" 2>/dev/null; do\n"
"  if ! kill -0 \"$ava_pid\" 2>/dev/null; then\n"
"    echo \"ava exited before permission-deny prompt completed\" >&2\n"
"    cat \"${RPC_OUT}\" >&2 2>/dev/null || true\n"
"    cat \"${RPC_ERR}\" >&2 2>/dev/null || true\n"
"    exit 1\n"
"  fi\n"
"  i=$((i + 1))\n"
"  if [ \"$i\" -gt 200 ]; then\n"
"    echo \"timed out waiting for permission-deny prompt completion\" >&2\n"
"    cat \"${RPC_OUT}\" >&2 2>/dev/null || true\n"
"    cat \"${RPC_ERR}\" >&2 2>/dev/null || true\n"
"    exit 1\n"
"  fi\n"
"  sleep 0.05\n"
"done\n"
"printf '%s\\n' '{\"id\":\"messages-after\",\"type\":\"get_messages\"}' >&3\n"
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
  message(FATAL_ERROR "headless permission-reply driver exited with ${DRIVER_RESULT}\nstdout:\n${DRIVER_OUTPUT}\nstderr:\n${DRIVER_ERROR}")
endif()

file(READ "${RPC_OUT}" AVA_OUTPUT)
file(READ "${RPC_ERR}" AVA_ERROR)
file(READ "${REQUEST_LOG}" PROVIDER_REQUEST)

if(EXISTS "${TARGET_FILE}")
  message(FATAL_ERROR "denied write_file RPC permission flow unexpectedly created ${TARGET_FILE}\nstdout:\n${AVA_OUTPUT}\nstderr:\n${AVA_ERROR}")
endif()

foreach(NEEDLE
        "--- request 1 ---"
        "--- request 2 ---"
        "\"model\":\"ava-headless-fake\""
        "\"stream\":false"
        "\"tool_call_id\":\"call_write\"")
  string(FIND "${PROVIDER_REQUEST}" "${NEEDLE}" NEEDLE_INDEX)
  if(NEEDLE_INDEX EQUAL -1)
    message(FATAL_ERROR "fake provider request log did not contain ${NEEDLE}\nrequest:\n${PROVIDER_REQUEST}\nstderr:\n${AVA_ERROR}")
  endif()
endforeach()

foreach(NEEDLE
        "\"name\":\"permission_requested\""
        "\"resolver_request_id\":\"permission_"
        "\"permission_request_id\":\"permreq_"
        "\"operation\":\"edit\""
        "\"target_path\":\"${TARGET_FILE}\""
        "\"tool_name\":\"write_file\""
        "\"risk\":\"high\""
        "\"diff_preview\""
        "+rpc new"
        "\"id\":\"reply\""
        "\"name\":\"permission_replied\""
        "\"decision\":\"deny\""
        "\"reason\":\"not approved by headless client\""
        "resolution_reason: not approved by headless client"
        "\"id\":\"prompt\""
        "after permission deny"
        "\"id\":\"messages-after\""
        "\"type\":\"tool_result\"")
  string(FIND "${AVA_OUTPUT}" "${NEEDLE}" NEEDLE_INDEX)
  if(NEEDLE_INDEX EQUAL -1)
    message(FATAL_ERROR "ava --rpc output did not contain ${NEEDLE}\nstdout:\n${AVA_OUTPUT}\nstderr:\n${AVA_ERROR}")
  endif()
endforeach()
