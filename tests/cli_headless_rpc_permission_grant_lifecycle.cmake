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
set(TARGET_FILE "${TEST_ROOT}/outside.txt")
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
file(CHMOD "${CONFIG_DIR}/ava" PERMISSIONS OWNER_READ OWNER_WRITE OWNER_EXECUTE)
file(WRITE "${TARGET_FILE}" "outside controlled grant target\n")
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
"\"${AVA_FAKE_PROVIDER_EXE}\" \"${PORT_FILE}\" \"${REQUEST_LOG}\" 0 read-tool-thrice \"${TARGET_FILE}\" > \"${PROVIDER_OUT}\" 2> \"${PROVIDER_ERR}\" &\n"
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
"printf '%s\\n' '{\"id\":\"prompt1\",\"type\":\"prompt\",\"protocol_version\":1,\"message\":\"read outside first\"}' >&3\n"
"resolver_id=\n"
"i=0\n"
"while [ -z \"$resolver_id\" ]; do\n"
"  if [ -s \"${RPC_OUT}\" ]; then\n"
"    resolver_id=$(sed -n 's/.*\"resolver_request_id\":\"\\([^\"]*\\)\".*/\\1/p' \"${RPC_OUT}\" | head -n 1)\n"
"  fi\n"
"  if [ -n \"$resolver_id\" ]; then break; fi\n"
"  if ! kill -0 \"$ava_pid\" 2>/dev/null; then\n"
"    echo \"ava exited before first permission request\" >&2\n"
"    cat \"${RPC_OUT}\" >&2 2>/dev/null || true\n"
"    cat \"${RPC_ERR}\" >&2 2>/dev/null || true\n"
"    exit 1\n"
"  fi\n"
"  i=$((i + 1))\n"
"  if [ \"$i\" -gt 200 ]; then\n"
"    echo \"timed out waiting for first permission request\" >&2\n"
"    cat \"${RPC_OUT}\" >&2 2>/dev/null || true\n"
"    cat \"${RPC_ERR}\" >&2 2>/dev/null || true\n"
"    exit 1\n"
"  fi\n"
"  sleep 0.05\n"
"done\n"
"printf '%s\\n' \"{\\\"id\\\":\\\"reply1\\\",\\\"type\\\":\\\"permission_reply\\\",\\\"request_id\\\":\\\"$resolver_id\\\",\\\"correlation_id\\\":\\\"prompt1\\\",\\\"decision\\\":\\\"allow_session\\\"}\" >&3\n"
"i=0\n"
"while ! grep -q '\"id\":\"prompt1\",\"type\":\"response\",\"success\":true' \"${RPC_OUT}\" 2>/dev/null; do\n"
"  if ! kill -0 \"$ava_pid\" 2>/dev/null; then\n"
"    echo \"ava exited before first prompt response\" >&2\n"
"    cat \"${RPC_OUT}\" >&2 2>/dev/null || true\n"
"    cat \"${RPC_ERR}\" >&2 2>/dev/null || true\n"
"    exit 1\n"
"  fi\n"
"  i=$((i + 1))\n"
"  if [ \"$i\" -gt 200 ]; then\n"
"    echo \"timed out waiting for first prompt response\" >&2\n"
"    cat \"${RPC_OUT}\" >&2 2>/dev/null || true\n"
"    cat \"${RPC_ERR}\" >&2 2>/dev/null || true\n"
"    exit 1\n"
"  fi\n"
"  sleep 0.05\n"
"done\n"
"printf '%s\\n' '{\"id\":\"grants1\",\"type\":\"permission_grants\"}' >&3\n"
"grant_id=\n"
"i=0\n"
"while [ -z \"$grant_id\" ]; do\n"
"  if [ -s \"${RPC_OUT}\" ]; then\n"
"    grant_id=$(sed -n 's/.*\"grant_id\":\"\\([^\"]*\\)\".*/\\1/p' \"${RPC_OUT}\" | head -n 1)\n"
"  fi\n"
"  if [ -n \"$grant_id\" ]; then break; fi\n"
"  i=$((i + 1))\n"
"  if [ \"$i\" -gt 200 ]; then\n"
"    echo \"timed out waiting for first grant listing\" >&2\n"
"    cat \"${RPC_OUT}\" >&2 2>/dev/null || true\n"
"    cat \"${RPC_ERR}\" >&2 2>/dev/null || true\n"
"    exit 1\n"
"  fi\n"
"  sleep 0.05\n"
"done\n"
"printf '%s\\n' \"{\\\"id\\\":\\\"revoke\\\",\\\"type\\\":\\\"permission_grant_revoke\\\",\\\"grant_id\\\":\\\"$grant_id\\\"}\" >&3\n"
"i=0\n"
"while ! grep -q '\"id\":\"revoke\".*\"revoked\":true' \"${RPC_OUT}\" 2>/dev/null; do\n"
"  i=$((i + 1))\n"
"  if [ \"$i\" -gt 200 ]; then\n"
"    echo \"timed out waiting for grant revoke response\" >&2\n"
"    cat \"${RPC_OUT}\" >&2 2>/dev/null || true\n"
"    cat \"${RPC_ERR}\" >&2 2>/dev/null || true\n"
"    exit 1\n"
"  fi\n"
"  sleep 0.05\n"
"done\n"
"printf '%s\\n' '{\"id\":\"prompt2\",\"type\":\"prompt\",\"message\":\"read outside second\"}' >&3\n"
"resolver_id=\n"
"i=0\n"
"while [ -z \"$resolver_id\" ]; do\n"
"  permission_count=$(grep -c '\"name\":\"permission_requested\"' \"${RPC_OUT}\" 2>/dev/null || true)\n"
"  if [ \"$permission_count\" -ge 2 ]; then\n"
"    resolver_id=$(sed -n 's/.*\"resolver_request_id\":\"\\([^\"]*\\)\".*/\\1/p' \"${RPC_OUT}\" | tail -n 1)\n"
"  fi\n"
"  if [ -n \"$resolver_id\" ]; then break; fi\n"
"  if ! kill -0 \"$ava_pid\" 2>/dev/null; then\n"
"    echo \"ava exited before second permission request\" >&2\n"
"    cat \"${RPC_OUT}\" >&2 2>/dev/null || true\n"
"    cat \"${RPC_ERR}\" >&2 2>/dev/null || true\n"
"    exit 1\n"
"  fi\n"
"  i=$((i + 1))\n"
"  if [ \"$i\" -gt 200 ]; then\n"
"    echo \"timed out waiting for second permission request after revoke\" >&2\n"
"    cat \"${RPC_OUT}\" >&2 2>/dev/null || true\n"
"    cat \"${RPC_ERR}\" >&2 2>/dev/null || true\n"
"    exit 1\n"
"  fi\n"
"  sleep 0.05\n"
"done\n"
"printf '%s\\n' \"{\\\"id\\\":\\\"reply2\\\",\\\"type\\\":\\\"permission_reply\\\",\\\"request_id\\\":\\\"$resolver_id\\\",\\\"correlation_id\\\":\\\"prompt2\\\",\\\"decision\\\":\\\"allow_session\\\"}\" >&3\n"
"i=0\n"
"while ! grep -q '\"id\":\"prompt2\",\"type\":\"response\",\"success\":true' \"${RPC_OUT}\" 2>/dev/null; do\n"
"  if ! kill -0 \"$ava_pid\" 2>/dev/null; then\n"
"    echo \"ava exited before second prompt response\" >&2\n"
"    cat \"${RPC_OUT}\" >&2 2>/dev/null || true\n"
"    cat \"${RPC_ERR}\" >&2 2>/dev/null || true\n"
"    exit 1\n"
"  fi\n"
"  i=$((i + 1))\n"
"  if [ \"$i\" -gt 200 ]; then\n"
"    echo \"timed out waiting for second prompt response\" >&2\n"
"    cat \"${RPC_OUT}\" >&2 2>/dev/null || true\n"
"    cat \"${RPC_ERR}\" >&2 2>/dev/null || true\n"
"    exit 1\n"
"  fi\n"
"  sleep 0.05\n"
"done\n"
"printf '%s\\n' '{\"id\":\"clear\",\"type\":\"permission_grants_clear\"}' >&3\n"
"i=0\n"
"while ! grep -q '\"id\":\"clear\".*\"cleared\":1' \"${RPC_OUT}\" 2>/dev/null; do\n"
"  i=$((i + 1))\n"
"  if [ \"$i\" -gt 200 ]; then\n"
"    echo \"timed out waiting for grant clear response\" >&2\n"
"    cat \"${RPC_OUT}\" >&2 2>/dev/null || true\n"
"    cat \"${RPC_ERR}\" >&2 2>/dev/null || true\n"
"    exit 1\n"
"  fi\n"
"  sleep 0.05\n"
"done\n"
"printf '%s\\n' '{\"id\":\"prompt3\",\"type\":\"prompt\",\"message\":\"read outside third\"}' >&3\n"
"resolver_id=\n"
"i=0\n"
"while [ -z \"$resolver_id\" ]; do\n"
"  permission_count=$(grep -c '\"name\":\"permission_requested\"' \"${RPC_OUT}\" 2>/dev/null || true)\n"
"  if [ \"$permission_count\" -ge 3 ]; then\n"
"    resolver_id=$(sed -n 's/.*\"resolver_request_id\":\"\\([^\"]*\\)\".*/\\1/p' \"${RPC_OUT}\" | tail -n 1)\n"
"  fi\n"
"  if [ -n \"$resolver_id\" ]; then break; fi\n"
"  if ! kill -0 \"$ava_pid\" 2>/dev/null; then\n"
"    echo \"ava exited before third permission request\" >&2\n"
"    cat \"${RPC_OUT}\" >&2 2>/dev/null || true\n"
"    cat \"${RPC_ERR}\" >&2 2>/dev/null || true\n"
"    exit 1\n"
"  fi\n"
"  i=$((i + 1))\n"
"  if [ \"$i\" -gt 200 ]; then\n"
"    echo \"timed out waiting for third permission request after clear\" >&2\n"
"    cat \"${RPC_OUT}\" >&2 2>/dev/null || true\n"
"    cat \"${RPC_ERR}\" >&2 2>/dev/null || true\n"
"    exit 1\n"
"  fi\n"
"  sleep 0.05\n"
"done\n"
"printf '%s\\n' \"{\\\"id\\\":\\\"reply3\\\",\\\"type\\\":\\\"permission_reply\\\",\\\"request_id\\\":\\\"$resolver_id\\\",\\\"correlation_id\\\":\\\"prompt3\\\",\\\"decision\\\":\\\"allow\\\"}\" >&3\n"
"i=0\n"
"while ! grep -q '\"id\":\"prompt3\",\"type\":\"response\",\"success\":true' \"${RPC_OUT}\" 2>/dev/null; do\n"
"  if ! kill -0 \"$ava_pid\" 2>/dev/null; then\n"
"    echo \"ava exited before third prompt response\" >&2\n"
"    cat \"${RPC_OUT}\" >&2 2>/dev/null || true\n"
"    cat \"${RPC_ERR}\" >&2 2>/dev/null || true\n"
"    exit 1\n"
"  fi\n"
"  i=$((i + 1))\n"
"  if [ \"$i\" -gt 200 ]; then\n"
"    echo \"timed out waiting for third prompt response\" >&2\n"
"    cat \"${RPC_OUT}\" >&2 2>/dev/null || true\n"
"    cat \"${RPC_ERR}\" >&2 2>/dev/null || true\n"
"    exit 1\n"
"  fi\n"
"  sleep 0.05\n"
"done\n"
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
  TIMEOUT 60
)

if(NOT DRIVER_RESULT EQUAL 0)
  message(FATAL_ERROR "headless permission-grant lifecycle driver exited with ${DRIVER_RESULT}\nstdout:\n${DRIVER_OUTPUT}\nstderr:\n${DRIVER_ERROR}")
endif()

file(READ "${RPC_OUT}" AVA_OUTPUT)
file(READ "${RPC_ERR}" AVA_ERROR)

string(REGEX MATCHALL "\"name\":\"permission_requested\"" PERMISSION_REQUEST_EVENTS "${AVA_OUTPUT}")
list(LENGTH PERMISSION_REQUEST_EVENTS PERMISSION_REQUEST_EVENT_COUNT)
if(NOT PERMISSION_REQUEST_EVENT_COUNT EQUAL 3)
  message(FATAL_ERROR "expected three permission requests across revoke and clear, got ${PERMISSION_REQUEST_EVENT_COUNT}\nstdout:\n${AVA_OUTPUT}\nstderr:\n${AVA_ERROR}")
endif()

foreach(NEEDLE
        "\"id\":\"revoke\""
        "\"revoked\":true"
        "\"name\":\"permission_grant_revoked\""
        "\"id\":\"clear\""
        "\"cleared\":1"
        "\"name\":\"permission_grants_cleared\""
        "first controlled grant"
        "second controlled grant"
        "third controlled grant")
  string(FIND "${AVA_OUTPUT}" "${NEEDLE}" NEEDLE_INDEX)
  if(NEEDLE_INDEX EQUAL -1)
    message(FATAL_ERROR "ava --rpc output did not contain ${NEEDLE}\nstdout:\n${AVA_OUTPUT}\nstderr:\n${AVA_ERROR}")
  endif()
endforeach()
