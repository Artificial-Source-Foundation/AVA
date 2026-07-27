# Verify transactional print-members completion-marker publication without
# Universal Ctags, libcwd, or source-tree mutation.

if (NOT DEFINED AVA_SOURCE_DIR OR AVA_SOURCE_DIR STREQUAL "")
  message(FATAL_ERROR "AVA_SOURCE_DIR must be set")
endif ()
if (NOT DEFINED AVA_TEST_ROOT OR AVA_TEST_ROOT STREQUAL "")
  message(FATAL_ERROR "AVA_TEST_ROOT must be set")
endif ()
if (NOT DEFINED PYTHON3_EXECUTABLE OR PYTHON3_EXECUTABLE STREQUAL "")
  message(FATAL_ERROR "PYTHON3_EXECUTABLE must be set")
endif ()

# This focused non-libcwd test still exercises the production locking contract.
find_program(FLOCK_EXECUTABLE flock)
if (NOT FLOCK_EXECUTABLE)
  message(FATAL_ERROR "generate_print_members_completion test requires flock")
endif ()

set(_generator "${AVA_SOURCE_DIR}/cmake/scripts/generate_print_members.py")
set(_runner "${AVA_SOURCE_DIR}/cmake/scripts/run_generate_print_members.sh")
set(_tags "${AVA_TEST_ROOT}/tags.json")
set(_signature "${_tags}.sources.sha256")
set(_output_dir "${AVA_TEST_ROOT}/generated/print_members")
set(_completion "${_output_dir}/generation-complete.sources.sha256")
set(_generation_lock "${_output_dir}/generation.lock")

file(REMOVE_RECURSE "${AVA_TEST_ROOT}")
file(MAKE_DIRECTORY "${_output_dir}")

string(CONCAT _valid_tags
  "{\"name\":\"Widget\",\"kind\":\"struct\",\"scope\":\"ava::fixture\",\"path\":\"src/ava/fixture/widget.h\"}\n"
  "{\"name\":\"value_\",\"kind\":\"member\",\"scope\":\"ava::fixture::Widget\",\"line\":2}\n"
  "{\"name\":\"print_members_opt_in\",\"kind\":\"member\",\"scope\":\"ava::fixture::Widget\",\"line\":3}\n")
set(_first_signature
  "1111111111111111111111111111111111111111111111111111111111111111  src/ava/fixture/widget.h\n")
file(WRITE "${_tags}" "${_valid_tags}")
file(WRITE "${_signature}" "${_first_signature}")

execute_process(
  COMMAND "${_runner}"
          "${PYTHON3_EXECUTABLE}" "${_generator}"
          "${_tags}" "${_output_dir}" "${_signature}" "${_completion}"
          "${FLOCK_EXECUTABLE}" "${_generation_lock}"
  RESULT_VARIABLE _success_rc
  OUTPUT_VARIABLE _success_out
  ERROR_VARIABLE _success_err)
if (NOT _success_rc EQUAL 0)
  message(FATAL_ERROR
    "initial print-members generation failed (exit ${_success_rc})\n"
    "stdout: ${_success_out}\nstderr: ${_success_err}")
endif ()
if (NOT EXISTS "${_completion}")
  message(FATAL_ERROR "successful generation did not publish ${_completion}")
endif ()
file(READ "${_completion}" _published_signature)
if (NOT _published_signature STREQUAL _first_signature)
  message(FATAL_ERROR "completion marker does not contain the exact source signature")
endif ()

# Reproduce the review failure: ctags has successfully advanced the signature,
# but its output is malformed and Python fails. A prior matching-looking marker
# must be invalidated and no completion temp may be accepted or left behind.
set(_second_signature
  "2222222222222222222222222222222222222222222222222222222222222222  src/ava/fixture/widget.h\n")
file(WRITE "${_signature}" "${_second_signature}")
file(WRITE "${_completion}" "${_second_signature}")
file(WRITE "${_tags}" "{ malformed ctags json\n")
execute_process(
  COMMAND "${_runner}"
          "${PYTHON3_EXECUTABLE}" "${_generator}"
          "${_tags}" "${_output_dir}" "${_signature}" "${_completion}"
          "${FLOCK_EXECUTABLE}" "${_generation_lock}"
  RESULT_VARIABLE _failure_rc
  OUTPUT_VARIABLE _failure_out
  ERROR_VARIABLE _failure_err)
if (_failure_rc EQUAL 0)
  message(FATAL_ERROR "generator unexpectedly accepted malformed ctags JSON")
endif ()
if (EXISTS "${_completion}")
  message(FATAL_ERROR "failed generation must not leave a completion marker")
endif ()
file(GLOB _completion_temps "${_completion}.tmp.*")
if (_completion_temps)
  message(FATAL_ERROR "completion temp file(s) left behind: ${_completion_temps}")
endif ()

# A later successful retry atomically publishes exactly the new signature.
file(WRITE "${_tags}" "${_valid_tags}")
execute_process(
  COMMAND "${_runner}"
          "${PYTHON3_EXECUTABLE}" "${_generator}"
          "${_tags}" "${_output_dir}" "${_signature}" "${_completion}"
          "${FLOCK_EXECUTABLE}" "${_generation_lock}"
  RESULT_VARIABLE _retry_rc
  OUTPUT_VARIABLE _retry_out
  ERROR_VARIABLE _retry_err)
if (NOT _retry_rc EQUAL 0)
  message(FATAL_ERROR
    "retry generation failed (exit ${_retry_rc})\n"
    "stdout: ${_retry_out}\nstderr: ${_retry_err}")
endif ()
file(READ "${_completion}" _retry_signature)
if (NOT _retry_signature STREQUAL _second_signature)
  message(FATAL_ERROR "retry completion marker does not contain the exact new signature")
endif ()
file(GLOB _retry_temps "${_completion}.tmp.*")
if (_retry_temps)
  message(FATAL_ERROR "completion temp file(s) left behind after retry: ${_retry_temps}")
endif ()

# Reproduce the concurrent review failure deterministically. The successful
# generator enters while holding the shared lock and waits. A failing runner is
# then launched second; its flock wrapper reports that it reached lock
# acquisition before the success is released. Serialization makes failure run
# last, so its modified printer can never coexist with a valid completion.
find_program(BASH_EXECUTABLE bash)
if (NOT BASH_EXECUTABLE)
  message(FATAL_ERROR "concurrent completion test requires bash")
endif ()
set(_concurrent_dir "${AVA_TEST_ROOT}/concurrent")
set(_concurrent_output "${_concurrent_dir}/generated/print_members")
set(_concurrent_completion
    "${_concurrent_output}/generation-complete.sources.sha256")
set(_concurrent_lock "${_concurrent_output}/generation.lock")
set(_success_tags "${_concurrent_dir}/success-tags.json")
set(_failure_tags "${_concurrent_dir}/failure-tags.json")
set(_success_signature "${_success_tags}.sources.sha256")
set(_failure_signature "${_failure_tags}.sources.sha256")
set(_success_generator "${_concurrent_dir}/success-generator")
set(_failure_generator "${_concurrent_dir}/failure-generator")
set(_flock_wrapper "${_concurrent_dir}/flock-wrapper")
set(_orchestrator "${_concurrent_dir}/run-concurrent-generators")
file(MAKE_DIRECTORY "${_concurrent_dir}")
file(WRITE "${_success_tags}" "success fixture\n")
file(WRITE "${_failure_tags}" "failure fixture\n")
file(WRITE "${_success_signature}"
  "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa  success.h\n")
file(WRITE "${_failure_signature}"
  "bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb  failure.h\n")
file(WRITE "${_success_generator}" [=[#!/bin/bash
set -euo pipefail
output_dir="$2"
: >"$SUCCESS_HOLDS_LOCK"
for ((i = 0; i < 1000; ++i)); do
  [[ -e "$RELEASE_SUCCESS" ]] && break
  sleep 0.01
done
[[ -e "$RELEASE_SUCCESS" ]] || exit 90
mkdir -p "$output_dir/fixture"
printf '%s\n' 'successful printer content' >"$output_dir/fixture/print_members.cpp"
printf '%s\n' 'fixture/print_members.cpp' >"$output_dir/source_files"
]=])
file(WRITE "${_failure_generator}" [=[#!/bin/bash
set -euo pipefail
output_dir="$2"
mkdir -p "$output_dir/fixture"
printf '%s\n' 'failed invocation printer content' >"$output_dir/fixture/print_members.cpp"
printf '%s\n' 'fixture/print_members.cpp' >"$output_dir/source_files"
exit 23
]=])
file(WRITE "${_flock_wrapper}" [=[#!/bin/bash
set -euo pipefail
if [[ "${AVA_LOCK_TEST_ROLE:-}" == "failure" ]]; then
  : >"$FAILURE_AT_FLOCK"
fi
exec "$REAL_FLOCK" "$@"
]=])
file(WRITE "${_orchestrator}" [=[#!/bin/bash
set -euo pipefail
wait_for_file() {
  for ((i = 0; i < 1000; ++i)); do
    [[ -e "$1" ]] && return 0
    sleep 0.01
  done
  echo "timed out waiting for $1" >&2
  return 1
}
AVA_LOCK_TEST_ROLE=success "$RUNNER" "$BASH_EXE" "$SUCCESS_GENERATOR" \
  "$SUCCESS_TAGS" "$OUTPUT_DIR" "$SUCCESS_SIGNATURE" "$COMPLETION" \
  "$FLOCK_WRAPPER" "$GEN_LOCK" &
success_pid=$!
wait_for_file "$SUCCESS_HOLDS_LOCK"
AVA_LOCK_TEST_ROLE=failure "$RUNNER" "$BASH_EXE" "$FAILURE_GENERATOR" \
  "$FAILURE_TAGS" "$OUTPUT_DIR" "$FAILURE_SIGNATURE" "$COMPLETION" \
  "$FLOCK_WRAPPER" "$GEN_LOCK" &
failure_pid=$!
wait_for_file "$FAILURE_AT_FLOCK"
: >"$RELEASE_SUCCESS"
set +e
wait "$success_pid"
success_rc=$?
wait "$failure_pid"
failure_rc=$?
set -e
if [[ $success_rc -ne 0 || $failure_rc -eq 0 ]]; then
  echo "unexpected generator results: success=$success_rc failure=$failure_rc" >&2
  exit 91
fi
]=])
foreach(_helper IN ITEMS
    "${_success_generator}" "${_failure_generator}"
    "${_flock_wrapper}" "${_orchestrator}")
  file(CHMOD "${_helper}"
       PERMISSIONS OWNER_READ OWNER_WRITE OWNER_EXECUTE GROUP_READ GROUP_EXECUTE WORLD_READ WORLD_EXECUTE)
endforeach()
execute_process(
  COMMAND "${CMAKE_COMMAND}" -E env
          "RUNNER=${_runner}"
          "BASH_EXE=${BASH_EXECUTABLE}"
          "SUCCESS_GENERATOR=${_success_generator}"
          "FAILURE_GENERATOR=${_failure_generator}"
          "SUCCESS_TAGS=${_success_tags}"
          "FAILURE_TAGS=${_failure_tags}"
          "OUTPUT_DIR=${_concurrent_output}"
          "SUCCESS_SIGNATURE=${_success_signature}"
          "FAILURE_SIGNATURE=${_failure_signature}"
          "COMPLETION=${_concurrent_completion}"
          "FLOCK_WRAPPER=${_flock_wrapper}"
          "REAL_FLOCK=${FLOCK_EXECUTABLE}"
          "GEN_LOCK=${_concurrent_lock}"
          "SUCCESS_HOLDS_LOCK=${_concurrent_dir}/success-holds-lock"
          "FAILURE_AT_FLOCK=${_concurrent_dir}/failure-at-flock"
          "RELEASE_SUCCESS=${_concurrent_dir}/release-success"
          "${_orchestrator}"
  RESULT_VARIABLE _concurrent_rc
  OUTPUT_VARIABLE _concurrent_stdout
  ERROR_VARIABLE _concurrent_stderr)
if (NOT _concurrent_rc EQUAL 0)
  message(FATAL_ERROR
    "concurrent completion check failed (exit ${_concurrent_rc})\n"
    "stdout: ${_concurrent_stdout}\nstderr: ${_concurrent_stderr}")
endif ()
if (EXISTS "${_concurrent_completion}")
  message(FATAL_ERROR
    "failure serialized after success must leave completion missing")
endif ()
set(_concurrent_printer
    "${_concurrent_output}/fixture/print_members.cpp")
file(READ "${_concurrent_printer}" _concurrent_printer_content)
if (NOT _concurrent_printer_content STREQUAL
    "failed invocation printer content\n")
  message(FATAL_ERROR
    "failed invocation did not execute last under the generation lock")
endif ()
file(GLOB _concurrent_completion_temps
  "${_concurrent_completion}.tmp.*")
if (_concurrent_completion_temps)
  message(FATAL_ERROR
    "concurrent completion temp file(s) left behind: ${_concurrent_completion_temps}")
endif ()
file(REMOVE_RECURSE "${_concurrent_dir}")
if (EXISTS "${_concurrent_dir}")
  message(FATAL_ERROR "failed to clean concurrent test fixtures")
endif ()

message(STATUS "print-members completion-marker and lock checks passed")
