if(NOT DEFINED AVA_EXE)
  message(FATAL_ERROR "AVA_EXE is required")
endif()

if(NOT DEFINED AVA_FAKE_PROVIDER_EXE)
  message(FATAL_ERROR "AVA_FAKE_PROVIDER_EXE is required")
endif()

if(NOT DEFINED AVA_CLI_TEST_ROOT)
  message(FATAL_ERROR "AVA_CLI_TEST_ROOT is required")
endif()

# The fake provider is launched through the shared Python owner/broker so every
# harness has identical process-gate wiring and process-group cleanup. Direct
# (non-CTest) invocations fall back to the script directory and PATH python3.
if(NOT DEFINED AVA_FAKE_PROVIDER_PY OR AVA_FAKE_PROVIDER_PY STREQUAL "")
  get_filename_component(AVA_FAKE_PROVIDER_PY "${CMAKE_CURRENT_LIST_DIR}/fake_provider.py" ABSOLUTE)
endif()
if(NOT DEFINED AVA_FAKE_PROVIDER_SH OR AVA_FAKE_PROVIDER_SH STREQUAL "")
  get_filename_component(AVA_FAKE_PROVIDER_SH "${CMAKE_CURRENT_LIST_DIR}/fake_provider_shell.sh" ABSOLUTE)
endif()
if(NOT DEFINED AVA_PYTHON OR AVA_PYTHON STREQUAL "")
  find_program(AVA_PYTHON NAMES python3 REQUIRED)
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
set(PORT_FILE "${TEST_ROOT}/provider.port")
set(REQUEST_LOG "${TEST_ROOT}/provider-requests.log")
set(PROVIDER_OUT "${TEST_ROOT}/provider.out")
set(PROVIDER_ERR "${TEST_ROOT}/provider.err")
set(AVA_OUT "${TEST_ROOT}/ava.out")
set(AVA_ERR "${TEST_ROOT}/ava.err")
set(DRIVER_FILE "${TEST_ROOT}/driver.sh")

file(REMOVE_RECURSE "${TEST_ROOT}")
file(MAKE_DIRECTORY "${WORKSPACE}" "${HOME_DIR}" "${CONFIG_DIR}/ava" "${STATE_DIR}" "${DATA_DIR}")
file(WRITE "${WORKSPACE}/AGENTS.md" "workspace tool visibility context\n")
file(WRITE "${CONFIG_DIR}/ava/models.json"
     "{\"default_provider\":\"moonshot\",\"default_model\":\"ava-headless-fake\","
     "\"models\":[{\"provider\":\"moonshot\",\"id\":\"ava-headless-fake\",\"family\":\"fake\","
     "\"context_window_tokens\":8192,\"max_output_tokens\":1024,\"supports_tools\":true,"
     "\"supports_streaming\":false,\"supports_reasoning\":false,\"reports_usage\":true}]}\n")

file(WRITE "${DRIVER_FILE}"
"#!/bin/sh\n"
"set -u\n"
"AVA_PYTHON=\"${AVA_PYTHON}\"\n"
"AVA_FAKE_PROVIDER_PY=\"${AVA_FAKE_PROVIDER_PY}\"\n"
"AVA_FAKE_PROVIDER_SH=\"${AVA_FAKE_PROVIDER_SH}\"\n"
"AVA_FAKE_PROVIDER_EXE=\"${AVA_FAKE_PROVIDER_EXE}\"\n"
". \"${AVA_FAKE_PROVIDER_SH}\"\n"
"cleanup() {\n"
"  fake_provider_stop >/dev/null 2>&1 || true\n"
"}\n"
"trap cleanup EXIT INT TERM\n"
"rm -f \"${PORT_FILE}\" \"${REQUEST_LOG}\" \"${PROVIDER_OUT}\" \"${PROVIDER_ERR}\" \"${AVA_OUT}\" \"${AVA_ERR}\"\n"
"fake_provider_start \"${TEST_ROOT}\" provider 0 text unused || exit 1\n"
"port=$FAKE_PROVIDER_PORT\n"
"HOME=\"${HOME_DIR}\" XDG_CONFIG_HOME=\"${CONFIG_DIR}\" XDG_STATE_HOME=\"${STATE_DIR}\" XDG_DATA_HOME=\"${DATA_DIR}\" NO_COLOR=1 MOONSHOT_API_KEY=test-key MOONSHOT_BASE_URL=\"http://127.0.0.1:$port\" \"${AVA_EXE}\" --mode json -nt -t read,grep,find,ls -xt find \"hello tool visibility\" > \"${AVA_OUT}\" 2> \"${AVA_ERR}\"\n"
"ava_status=$?\n"
"if [ \"$ava_status\" -ne 0 ]; then\n"
"  echo \"ava tool visibility exited with $ava_status\" >&2\n"
"  cat \"${AVA_OUT}\" >&2 2>/dev/null || true\n"
"  cat \"${AVA_ERR}\" >&2 2>/dev/null || true\n"
"  exit \"$ava_status\"\n"
"fi\n"
"fake_provider_wait 0 ${AVA_TIMEOUT_30} || exit 1\n"
"fake_provider_finish ${AVA_TIMEOUT_30} || exit 1\n"
)

execute_process(
  COMMAND /bin/sh "${DRIVER_FILE}"
  WORKING_DIRECTORY "${WORKSPACE}"
  OUTPUT_VARIABLE DRIVER_OUTPUT
  ERROR_VARIABLE DRIVER_ERROR
  RESULT_VARIABLE DRIVER_RESULT
  TIMEOUT ${AVA_TIMEOUT_30}
)

if(NOT DRIVER_RESULT EQUAL 0)
  message(FATAL_ERROR "headless tool visibility driver exited with ${DRIVER_RESULT}\nstdout:\n${DRIVER_OUTPUT}\nstderr:\n${DRIVER_ERROR}")
endif()

file(READ "${AVA_OUT}" AVA_OUTPUT)
file(READ "${AVA_ERR}" AVA_ERROR)
file(READ "${REQUEST_LOG}" PROVIDER_REQUEST)

foreach(NEEDLE
        "\"schema_version\":1"
        "\"name\":\"done\""
        "\"payload\":{\"text\":\"headless active prompt complete\"}")
  string(FIND "${AVA_OUTPUT}" "${NEEDLE}" NEEDLE_INDEX)
  if(NEEDLE_INDEX EQUAL -1)
    message(FATAL_ERROR "ava tool visibility output did not contain ${NEEDLE}\nstdout:\n${AVA_OUTPUT}\nstderr:\n${AVA_ERROR}")
  endif()
endforeach()

string(FIND "${PROVIDER_REQUEST}" "\"name\":\"read_file\"" READ_FILE_INDEX)
if(READ_FILE_INDEX EQUAL -1)
  message(FATAL_ERROR "provider request did not advertise read_file after explicit tool include\nrequest:\n${PROVIDER_REQUEST}")
endif()

foreach(VISIBLE_TOOL "grep" "list_directory")
  string(FIND "${PROVIDER_REQUEST}" "\"name\":\"${VISIBLE_TOOL}\"" VISIBLE_INDEX)
  if(VISIBLE_INDEX EQUAL -1)
    message(FATAL_ERROR "provider request did not advertise ${VISIBLE_TOOL} through Pi-style tool alias input\nrequest:\n${PROVIDER_REQUEST}")
  endif()
endforeach()

foreach(HIDDEN_TOOL "glob" "bash" "write_file")
  string(FIND "${PROVIDER_REQUEST}" "\"name\":\"${HIDDEN_TOOL}\"" HIDDEN_INDEX)
  if(NOT HIDDEN_INDEX EQUAL -1)
    message(FATAL_ERROR "provider request advertised hidden tool ${HIDDEN_TOOL}\nrequest:\n${PROVIDER_REQUEST}")
  endif()
endforeach()
