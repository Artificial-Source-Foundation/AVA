if(NOT DEFINED AVA_EXE)
  message(FATAL_ERROR "AVA_EXE is required")
endif()

if(NOT DEFINED AVA_FAKE_PROVIDER_EXE)
  message(FATAL_ERROR "AVA_FAKE_PROVIDER_EXE is required")
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
set(AVA_TIMEOUT_30 30)
set(AVA_POLL_200 200)
if(DEFINED ENV{AVA_DEBUG_NO_TIMEOUT})
  set(AVA_DEBUG_SECONDS 3600)
  if(DEFINED ENV{AVA_DEBUG_NO_TIMEOUT_SECONDS} AND "$ENV{AVA_DEBUG_NO_TIMEOUT_SECONDS}" MATCHES "^[1-9][0-9]*$")
    set(AVA_DEBUG_SECONDS "$ENV{AVA_DEBUG_NO_TIMEOUT_SECONDS}")
  endif()
  set(AVA_TIMEOUT_30 "${AVA_DEBUG_SECONDS}")
  math(EXPR AVA_POLL_200 "${AVA_DEBUG_SECONDS} * 20")
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
set(AVA_OUT "${TEST_ROOT}/ava.out")
set(AVA_ERR "${TEST_ROOT}/ava.err")
set(DRIVER_FILE "${TEST_ROOT}/driver.sh")

file(REMOVE_RECURSE "${TEST_ROOT}")
file(MAKE_DIRECTORY "${WORKSPACE}" "${HOME_DIR}" "${CONFIG_DIR}/ava" "${STATE_DIR}" "${DATA_DIR}")
file(WRITE "${WORKSPACE}/AGENTS.md" "workspace cli context should remain\n")
file(WRITE "${CONFIG_DIR}/ava/SYSTEM.md" "global system prompt should not appear\n")
file(WRITE "${CONFIG_DIR}/ava/APPEND_SYSTEM.md" "global append prompt should not appear\n")
file(WRITE "${CONFIG_DIR}/ava/models.json"
     "{\"default_provider\":\"moonshot\",\"default_model\":\"ava-headless-fake\","
     "\"models\":[{\"provider\":\"moonshot\",\"id\":\"ava-headless-fake\",\"family\":\"fake\","
     "\"context_window_tokens\":8192,\"max_output_tokens\":1024,\"supports_tools\":true,"
     "\"supports_streaming\":false,\"supports_reasoning\":false,\"reports_usage\":true}]}\n")

file(WRITE "${DRIVER_FILE}"
"#!/bin/sh\n"
"set -u\n"
"provider_pid=\n"
"cleanup() {\n"
"  if [ -n \"$provider_pid\" ]; then kill \"$provider_pid\" 2>/dev/null || true; fi\n"
"}\n"
"trap cleanup EXIT INT TERM\n"
"rm -f \"${PORT_FILE}\" \"${REQUEST_LOG}\" \"${PROVIDER_OUT}\" \"${PROVIDER_ERR}\" \"${AVA_OUT}\" \"${AVA_ERR}\"\n"
"\"${AVA_FAKE_PROVIDER_EXE}\" \"${PORT_FILE}\" \"${REQUEST_LOG}\" 0 > \"${PROVIDER_OUT}\" 2> \"${PROVIDER_ERR}\" &\n"
"provider_pid=$!\n"
"i=0\n"
"while [ ! -s \"${PORT_FILE}\" ]; do\n"
"  if ! kill -0 \"$provider_pid\" 2>/dev/null; then\n"
"    echo \"fake provider exited before writing a port\" >&2\n"
"    cat \"${PROVIDER_ERR}\" >&2 2>/dev/null || true\n"
"    exit 1\n"
"  fi\n"
"  i=$((i + 1))\n"
"  if [ \"$i\" -gt ${AVA_POLL_200} ]; then echo \"timed out waiting for fake provider port\" >&2; exit 1; fi\n"
"  sleep 0.05\n"
"done\n"
"port=$(cat \"${PORT_FILE}\")\n"
"HOME=\"${HOME_DIR}\" XDG_CONFIG_HOME=\"${CONFIG_DIR}\" XDG_STATE_HOME=\"${STATE_DIR}\" XDG_DATA_HOME=\"${DATA_DIR}\" NO_COLOR=1 MOONSHOT_API_KEY=test-key MOONSHOT_BASE_URL=\"http://127.0.0.1:$port\" \"${AVA_EXE}\" --system-prompt \"cli smoke system\" --append-system-prompt \"cli smoke append one\" --append-system-prompt \"cli smoke append two\" --print \"hello cli prompt\" --output text > \"${AVA_OUT}\" 2> \"${AVA_ERR}\"\n"
"ava_status=$?\n"
"if [ \"$ava_status\" -ne 0 ]; then\n"
"  kill \"$provider_pid\" 2>/dev/null || true\n"
"  wait \"$provider_pid\" 2>/dev/null || true\n"
"  provider_pid=\n"
"  echo \"ava --print exited with $ava_status\" >&2\n"
"  cat \"${AVA_OUT}\" >&2 2>/dev/null || true\n"
"  cat \"${AVA_ERR}\" >&2 2>/dev/null || true\n"
"  exit \"$ava_status\"\n"
"fi\n"
"wait \"$provider_pid\"\n"
"provider_status=$?\n"
"provider_pid=\n"
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
  TIMEOUT ${AVA_TIMEOUT_30}
)

if(NOT DRIVER_RESULT EQUAL 0)
  message(FATAL_ERROR "headless print prompt-override driver exited with ${DRIVER_RESULT}\nstdout:\n${DRIVER_OUTPUT}\nstderr:\n${DRIVER_ERROR}")
endif()

file(READ "${AVA_OUT}" AVA_OUTPUT)
file(READ "${AVA_ERR}" AVA_ERROR)
file(READ "${REQUEST_LOG}" PROVIDER_REQUEST)

foreach(NEEDLE
        "headless active prompt complete")
  string(FIND "${AVA_OUTPUT}" "${NEEDLE}" NEEDLE_INDEX)
  if(NEEDLE_INDEX EQUAL -1)
    message(FATAL_ERROR "ava --print output did not contain ${NEEDLE}\nstdout:\n${AVA_OUTPUT}\nstderr:\n${AVA_ERROR}")
  endif()
endforeach()

foreach(NEEDLE
        "cli smoke system"
        "cli smoke append one"
        "cli smoke append two"
        "workspace cli context should remain"
        "hello cli prompt")
  string(FIND "${PROVIDER_REQUEST}" "${NEEDLE}" NEEDLE_INDEX)
  if(NEEDLE_INDEX EQUAL -1)
    message(FATAL_ERROR "fake provider request log did not contain ${NEEDLE}\nrequest:\n${PROVIDER_REQUEST}\nstderr:\n${AVA_ERROR}")
  endif()
endforeach()

foreach(NEEDLE
        "global system prompt should not appear"
        "global append prompt should not appear")
  string(FIND "${PROVIDER_REQUEST}" "${NEEDLE}" NEEDLE_INDEX)
  if(NOT NEEDLE_INDEX EQUAL -1)
    message(FATAL_ERROR "fake provider request log unexpectedly contained ${NEEDLE}\nrequest:\n${PROVIDER_REQUEST}\nstderr:\n${AVA_ERROR}")
  endif()
endforeach()
