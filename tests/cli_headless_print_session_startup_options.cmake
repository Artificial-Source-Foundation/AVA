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
set(CUSTOM_SESSIONS "${TEST_ROOT}/custom-sessions")
set(DRIVER_FILE "${TEST_ROOT}/driver.sh")
set(SOURCE_SESSION_ID_FILE "${TEST_ROOT}/source-session-id")

file(REMOVE_RECURSE "${TEST_ROOT}")
file(MAKE_DIRECTORY "${WORKSPACE}" "${HOME_DIR}" "${CONFIG_DIR}/ava" "${STATE_DIR}" "${DATA_DIR}")
file(WRITE "${WORKSPACE}/AGENTS.md" "workspace startup option context should remain\n")
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
"run_ava() {\n"
"  label=\"$1\"\n"
"  shift\n"
"  port_file=\"${TEST_ROOT}/provider-$label.port\"\n"
"  request_log=\"${TEST_ROOT}/provider-$label-request.log\"\n"
"  provider_out=\"${TEST_ROOT}/provider-$label.out\"\n"
"  provider_err=\"${TEST_ROOT}/provider-$label.err\"\n"
"  ava_out=\"${TEST_ROOT}/ava-$label.out\"\n"
"  ava_err=\"${TEST_ROOT}/ava-$label.err\"\n"
"  rm -f \"$port_file\" \"$request_log\" \"$provider_out\" \"$provider_err\" \"$ava_out\" \"$ava_err\"\n"
"  \"${AVA_FAKE_PROVIDER_EXE}\" \"$port_file\" \"$request_log\" 0 > \"$provider_out\" 2> \"$provider_err\" &\n"
"  provider_pid=$!\n"
"  i=0\n"
"  while [ ! -s \"$port_file\" ]; do\n"
"    if ! kill -0 \"$provider_pid\" 2>/dev/null; then\n"
"      echo \"fake provider exited before writing a port\" >&2\n"
"      cat \"$provider_err\" >&2 2>/dev/null || true\n"
"      exit 1\n"
"    fi\n"
"    i=$((i + 1))\n"
"    if [ \"$i\" -gt 200 ]; then echo \"timed out waiting for fake provider port\" >&2; exit 1; fi\n"
"    sleep 0.05\n"
"  done\n"
"  port=$(cat \"$port_file\")\n"
"  HOME=\"${HOME_DIR}\" XDG_CONFIG_HOME=\"${CONFIG_DIR}\" XDG_STATE_HOME=\"${STATE_DIR}\" XDG_DATA_HOME=\"${DATA_DIR}\" NO_COLOR=1 MOONSHOT_API_KEY=test-key MOONSHOT_BASE_URL=\"http://127.0.0.1:$port\" \"${AVA_EXE}\" \"$@\" > \"$ava_out\" 2> \"$ava_err\"\n"
"  ava_status=$?\n"
"  if [ \"$ava_status\" -ne 0 ]; then\n"
"    kill \"$provider_pid\" 2>/dev/null || true\n"
"    wait \"$provider_pid\" 2>/dev/null || true\n"
"    provider_pid=\n"
"    echo \"ava startup option prompt exited with $ava_status\" >&2\n"
"    cat \"$ava_out\" >&2 2>/dev/null || true\n"
"    cat \"$ava_err\" >&2 2>/dev/null || true\n"
"    exit \"$ava_status\"\n"
"  fi\n"
"  wait \"$provider_pid\"\n"
"  provider_status=$?\n"
"  provider_pid=\n"
"  if [ \"$provider_status\" -ne 0 ]; then\n"
"    echo \"fake provider exited with $provider_status\" >&2\n"
"    cat \"$provider_err\" >&2 2>/dev/null || true\n"
"    exit \"$provider_status\"\n"
"  fi\n"
"}\n"
"run_ava named --session-dir \"${CUSTOM_SESSIONS}\" --name \"startup named\" --output text \"named\" \"prompt\"\n"
"session_file=$(find \"${CUSTOM_SESSIONS}\" -name '*.jsonl' | sort | head -n 1)\n"
"if [ -z \"$session_file\" ]; then echo \"startup run did not create a custom session\" >&2; exit 1; fi\n"
"session_id=$(basename \"$session_file\" .jsonl)\n"
"printf '%s\\n' \"$session_id\" > \"${SOURCE_SESSION_ID_FILE}\"\n"
"run_ava fork --session-dir \"${CUSTOM_SESSIONS}\" --fork \"$session_id\" --name \"fork startup\" --output text \"forked\" \"prompt\"\n")

execute_process(
  COMMAND /bin/sh "${DRIVER_FILE}"
  WORKING_DIRECTORY "${WORKSPACE}"
  OUTPUT_VARIABLE DRIVER_OUTPUT
  ERROR_VARIABLE DRIVER_ERROR
  RESULT_VARIABLE DRIVER_RESULT
  TIMEOUT 45
)

if(NOT DRIVER_RESULT EQUAL 0)
  message(FATAL_ERROR "headless startup options driver exited with ${DRIVER_RESULT}\nstdout:\n${DRIVER_OUTPUT}\nstderr:\n${DRIVER_ERROR}")
endif()

file(READ "${TEST_ROOT}/ava-named.out" NAMED_OUTPUT)
file(READ "${TEST_ROOT}/ava-named.err" NAMED_ERROR)
file(READ "${TEST_ROOT}/provider-named-request.log" NAMED_REQUEST)
file(READ "${TEST_ROOT}/ava-fork.out" FORK_OUTPUT)
file(READ "${TEST_ROOT}/ava-fork.err" FORK_ERROR)
file(READ "${TEST_ROOT}/provider-fork-request.log" FORK_REQUEST)
file(READ "${SOURCE_SESSION_ID_FILE}" SOURCE_SESSION_ID)
string(STRIP "${SOURCE_SESSION_ID}" SOURCE_SESSION_ID)

foreach(OUTPUT IN ITEMS "${NAMED_OUTPUT}" "${FORK_OUTPUT}")
  string(FIND "${OUTPUT}" "headless active prompt complete" NEEDLE_INDEX)
  if(NEEDLE_INDEX EQUAL -1)
    message(FATAL_ERROR "ava startup option output did not contain fake provider text\nnamed stdout:\n${NAMED_OUTPUT}\nnamed stderr:\n${NAMED_ERROR}\nfork stdout:\n${FORK_OUTPUT}\nfork stderr:\n${FORK_ERROR}")
  endif()
endforeach()

foreach(REQUEST IN ITEMS "${NAMED_REQUEST}" "${FORK_REQUEST}")
  foreach(NEEDLE "workspace startup option context should remain" "prompt")
    string(FIND "${REQUEST}" "${NEEDLE}" NEEDLE_INDEX)
    if(NEEDLE_INDEX EQUAL -1)
      message(FATAL_ERROR "fake provider request log did not contain ${NEEDLE}\nrequest:\n${REQUEST}")
    endif()
  endforeach()
endforeach()

file(GLOB_RECURSE DEFAULT_SESSION_FILES "${STATE_DIR}/ava/sessions/*.jsonl")
if(DEFAULT_SESSION_FILES)
  message(FATAL_ERROR "--session-dir wrote sessions into the default state directory:\n${DEFAULT_SESSION_FILES}")
endif()

file(GLOB_RECURSE SESSION_FILES "${CUSTOM_SESSIONS}/*.jsonl")
list(LENGTH SESSION_FILES SESSION_FILE_COUNT)
if(NOT SESSION_FILE_COUNT EQUAL 2)
  message(FATAL_ERROR "--session-dir/--fork expected two custom session files, found ${SESSION_FILE_COUNT}:\n${SESSION_FILES}")
endif()

set(SESSION_TEXT "")
foreach(SESSION_FILE IN LISTS SESSION_FILES)
  file(READ "${SESSION_FILE}" CONTENTS)
  string(APPEND SESSION_TEXT "${CONTENTS}\n")
endforeach()

foreach(NEEDLE
        "\"name\":\"startup named\""
        "\"name\":\"fork startup\""
        "\"branch_origin\":\"fork\""
        "\"actor\":\"cli\""
        "\"parent_session_id\":\"${SOURCE_SESSION_ID}\""
        "\"source_session_id\":\"${SOURCE_SESSION_ID}\"")
  string(FIND "${SESSION_TEXT}" "${NEEDLE}" NEEDLE_INDEX)
  if(NEEDLE_INDEX EQUAL -1)
    message(FATAL_ERROR "custom session files did not contain ${NEEDLE}\nsessions:\n${SESSION_TEXT}")
  endif()
endforeach()
