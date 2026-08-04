if(NOT DEFINED AVA_EXE)
  message(FATAL_ERROR "AVA_EXE is required")
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
set(WORKSPACE "${TEST_ROOT}/workspace")
set(INPUT_FILE "${TEST_ROOT}/rpc-input.jsonl")

file(REMOVE_RECURSE "${TEST_ROOT}")
file(MAKE_DIRECTORY "${WORKSPACE}" "${TEST_ROOT}/home" "${TEST_ROOT}/config" "${TEST_ROOT}/state" "${TEST_ROOT}/data")
file(REAL_PATH "${WORKSPACE}" REAL_WORKSPACE)
file(WRITE "${INPUT_FILE}"
     "{\"id\":\"proto\",\"type\":\"get_protocol\",\"protocol_version\":1}\n"
     "{\"id\":\"state\",\"type\":\"get_state\"}\n"
     "{\"id\":\"models\",\"type\":\"list_models\"}\n"
     "{\"id\":\"setr\",\"type\":\"set_reasoning\",\"reasoning_level\":\"low\"}\n"
     "{\"id\":\"clearr\",\"type\":\"clear_reasoning\"}\n"
     "{\"id\":\"set_model\",\"type\":\"set_model\",\"provider\":\"openai\",\"model\":\"gpt-4.1-mini\"}\n"
     "{\"id\":\"stats0\",\"type\":\"get_session_stats\"}\n"
     "{\"id\":\"validate0\",\"type\":\"validate_session\"}\n"
     "{\"id\":\"messages0\",\"type\":\"get_messages\"}\n"
     "{\"id\":\"sessions0\",\"type\":\"list_sessions\"}\n"
     "{\"id\":\"new\",\"type\":\"new_session\"}\n"
     "{\"id\":\"stats1\",\"type\":\"get_session_stats\"}\n"
     "{\"id\":\"validate1\",\"type\":\"validate_session\"}\n"
     "{\"id\":\"sessions1\",\"type\":\"list_sessions\"}\n")

execute_process(
  COMMAND "${CMAKE_COMMAND}" -E env
          "HOME=${TEST_ROOT}/home"
          "XDG_CONFIG_HOME=${TEST_ROOT}/config"
          "XDG_STATE_HOME=${TEST_ROOT}/state"
          "XDG_DATA_HOME=${TEST_ROOT}/data"
          "NO_COLOR=1"
          "${AVA_EXE}" --mode rpc
  WORKING_DIRECTORY "${WORKSPACE}"
  INPUT_FILE "${INPUT_FILE}"
  OUTPUT_VARIABLE AVA_OUTPUT
  ERROR_VARIABLE AVA_ERROR
  RESULT_VARIABLE AVA_RESULT
  TIMEOUT ${AVA_TIMEOUT_15}
)

if(NOT AVA_RESULT EQUAL 0)
  message(FATAL_ERROR "ava --mode rpc exited with ${AVA_RESULT}\nstdout:\n${AVA_OUTPUT}\nstderr:\n${AVA_ERROR}")
endif()

foreach(NEEDLE
        "\"id\":\"proto\""
        "\"protocol_version\":1"
        "\"id\":\"state\""
        "\"workspace_dir\":\"${REAL_WORKSPACE}\""
        "\"id\":\"models\""
        "\"models\":["
        "\"current_provider\":\"openai\""
        "\"provider\":\"kimi\""
        "\"model\":\"kimi-k2-thinking\""
        "\"provider\":\"moonshot\""
        "\"model\":\"kimi-k2.6\""
        "\"provider\":\"openrouter\""
        "\"model\":\"moonshotai/kimi-k2.6\""
        "\"api_family\":\"openai_chat_completions\""
        "\"reasoning_format\":\"reasoning_content\""
        "\"id\":\"setr\""
        "\"reasoning_enabled\":true"
        "\"reasoning_level\":\"low\""
        "\"id\":\"clearr\""
        "\"id\":\"set_model\""
        "\"model\":\"gpt-4.1-mini\""
        "\"id\":\"stats0\""
        "\"entry_count\":4"
        "\"reasoning_change\":2"
        "\"model_change\":1"
        "\"id\":\"validate0\""
        "\"ok\":true"
        "\"error_count\":0"
        "\"issues\":[]"
        "\"id\":\"messages0\""
        "\"messages\":[]"
        "\"message_count\":0"
        "\"id\":\"sessions0\""
        "\"sessions\":["
        "\"id\":\"new\""
        "\"id\":\"stats1\""
        "\"entry_count\":1"
        "\"id\":\"validate1\""
        "\"id\":\"sessions1\"")
  string(FIND "${AVA_OUTPUT}" "${NEEDLE}" NEEDLE_INDEX)
  if(NEEDLE_INDEX EQUAL -1)
    message(FATAL_ERROR "ava --rpc output did not contain ${NEEDLE}\nstdout:\n${AVA_OUTPUT}\nstderr:\n${AVA_ERROR}")
  endif()
endforeach()
