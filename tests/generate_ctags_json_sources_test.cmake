# generate_ctags_json_sources_test.cmake -- verify tags input bookkeeping.
#
# Exercises cmake/scripts/generate_ctags_json.sh with a fake ctags so the test
# stays credential-free and does not require Universal Ctags or libcwd. It
# asserts exact sorted path and deterministic content-signature manifests,
# transactional failure behavior for both manifests, and relative output paths.
#
# Required -D variables:
#   AVA_SOURCE_DIR  repository root
#   AVA_TEST_ROOT   private scratch directory for this test

if (NOT DEFINED AVA_SOURCE_DIR OR AVA_SOURCE_DIR STREQUAL "")
  message(FATAL_ERROR "AVA_SOURCE_DIR must be set")
endif ()
if (NOT DEFINED AVA_TEST_ROOT OR AVA_TEST_ROOT STREQUAL "")
  message(FATAL_ERROR "AVA_TEST_ROOT must be set")
endif ()

# This focused non-libcwd test still exercises the production locking contract.
find_program(FLOCK_EXECUTABLE flock)
if (NOT FLOCK_EXECUTABLE)
  message(FATAL_ERROR "generate_ctags_json_sources test requires flock")
endif ()

set(_script "${AVA_SOURCE_DIR}/cmake/scripts/generate_ctags_json.sh")
set(_list_script "${AVA_SOURCE_DIR}/cmake/scripts/list_ava_sources.sh")
if (NOT EXISTS "${_script}")
  message(FATAL_ERROR "missing ${_script}")
endif ()
if (NOT EXISTS "${_list_script}")
  message(FATAL_ERROR "missing ${_list_script}")
endif ()

# This focused test runs even when libcwd is disabled, so directly guard the
# wiring that makes ordinary builds reconfigure for existing edits and glob-set
# additions/deletions.
set(_debug_rules "${AVA_SOURCE_DIR}/src/ava/debug/CMakeLists.txt")
file(READ "${_debug_rules}" _debug_rules_text)
set(_required_rules_text
  "file(GLOB_RECURSE _ava_watched_source_files"
  "  CONFIGURE_DEPENDS"
  "\"\${CMAKE_SOURCE_DIR}/src/ava/*.h\""
  "\"\${CMAKE_SOURCE_DIR}/src/ava/*.cpp\""
  "\"\${CMAKE_SOURCE_DIR}/src/ava/*.cxx\""
  "list(FILTER _ava_watched_source_files EXCLUDE REGEX"
  "CMAKE_CONFIGURE_DEPENDS"
  "\${_ava_current_source_files}"
  "\${_ava_watched_sources}"
  "\${_ava_current_sources}")
foreach(_required_text IN LISTS _required_rules_text)
  string(FIND "${_debug_rules_text}" "${_required_text}" _required_text_at)
  if (_required_text_at EQUAL -1)
    message(FATAL_ERROR
      "${_debug_rules} is missing automatic reconfigure wiring: ${_required_text}")
  endif ()
endforeach()
string(FIND "${_debug_rules_text}" "IS_NEWER_THAN" _mtime_logic_at)
if (NOT _mtime_logic_at EQUAL -1)
  message(FATAL_ERROR "${_debug_rules} must not use IS_NEWER_THAN freshness logic")
endif ()

file(REMOVE_RECURSE "${AVA_TEST_ROOT}")
file(MAKE_DIRECTORY "${AVA_TEST_ROOT}")

set(_out_dir "${AVA_TEST_ROOT}/generated/ctags")
set(_out "${_out_dir}/tags.json")
set(_sources_out "${_out}.sources")
set(_sources_sha256_out "${_sources_out}.sha256")
set(_generation_lock "${AVA_TEST_ROOT}/generated/print_members/generation.lock")
file(MAKE_DIRECTORY "${_out_dir}")

# Expected path manifest is whatever list_ava_sources.sh currently emits.
execute_process(
  COMMAND "${_list_script}"
  WORKING_DIRECTORY "${AVA_SOURCE_DIR}"
  RESULT_VARIABLE _list_rc
  OUTPUT_VARIABLE _expected_sources
  ERROR_VARIABLE _list_err
)
if (NOT _list_rc EQUAL 0)
  message(FATAL_ERROR "list_ava_sources.sh failed (exit ${_list_rc}): ${_list_err}")
endif ()

# Independently compute the exact CMake sha256sum-compatible signature text.
string(REGEX REPLACE "\n$" "" _expected_sources_text "${_expected_sources}")
if (_expected_sources_text STREQUAL "")
  set(_expected_source_list "")
else()
  string(REPLACE "\n" ";" _expected_source_list "${_expected_sources_text}")
endif ()
set(_expected_sources_sha256 "")
foreach(_source_rel IN LISTS _expected_source_list)
  file(SHA256 "${AVA_SOURCE_DIR}/${_source_rel}" _source_sha256)
  string(APPEND _expected_sources_sha256 "${_source_sha256}  ${_source_rel}\n")
endforeach()

# Repeating the calculation over unchanged read-only inputs must be stable.
set(_recomputed_sources_sha256 "")
foreach(_source_rel IN LISTS _expected_source_list)
  file(SHA256 "${AVA_SOURCE_DIR}/${_source_rel}" _source_sha256)
  string(APPEND _recomputed_sources_sha256 "${_source_sha256}  ${_source_rel}\n")
endforeach()
if (NOT _recomputed_sources_sha256 STREQUAL _expected_sources_sha256)
  message(FATAL_ERROR "unchanged source signature calculation is not deterministic")
endif ()

# Content changes are checked in an isolated fixture, never in src/ava.
set(_signature_fixture "${AVA_TEST_ROOT}/signature-fixture.cpp")
file(WRITE "${_signature_fixture}" "int fixture_value = 1;\n")
file(SHA256 "${_signature_fixture}" _fixture_sha256_before)
file(WRITE "${_signature_fixture}" "int fixture_value = 2;\n")
file(SHA256 "${_signature_fixture}" _fixture_sha256_after)
if (_fixture_sha256_before STREQUAL _fixture_sha256_after)
  message(FATAL_ERROR "isolated fixture content change did not change its signature")
endif ()

# Fake ctags that accepts the same flag shape as Universal Ctags and writes a
# minimal JSON line when -f is present. It ignores the input list contents.
set(_fake_ok "${AVA_TEST_ROOT}/fake-ctags-ok")
file(WRITE "${_fake_ok}"
"#!/bin/sh
out=\"\"
while [ \"$#\" -gt 0 ]; do
  case \"$1\" in
    -f)
      shift
      out=\"$1\"
      ;;
  esac
  shift
done
if [ -z \"$out\" ]; then
  echo \"fake-ctags-ok: missing -f\" >&2
  exit 2
fi
printf '%s\\n' '{\"name\":\"Fake\",\"kind\":\"struct\",\"path\":\"src/ava/Fake.h\"}' >\"$out\"
exit 0
")
file(CHMOD "${_fake_ok}"
     PERMISSIONS OWNER_READ OWNER_WRITE OWNER_EXECUTE GROUP_READ GROUP_EXECUTE WORLD_READ WORLD_EXECUTE)

execute_process(
  COMMAND "${_script}" "${_out}" "${_fake_ok}" "${CMAKE_COMMAND}"
          "${FLOCK_EXECUTABLE}" "${_generation_lock}"
  WORKING_DIRECTORY "${AVA_SOURCE_DIR}"
  RESULT_VARIABLE _gen_rc
  OUTPUT_VARIABLE _gen_out
  ERROR_VARIABLE _gen_err
)
if (NOT _gen_rc EQUAL 0)
  message(FATAL_ERROR
    "generate_ctags_json.sh failed with fake-ctags-ok (exit ${_gen_rc})\n"
    "stdout: ${_gen_out}\nstderr: ${_gen_err}")
endif ()
if (NOT EXISTS "${_out}")
  message(FATAL_ERROR "expected tags output missing: ${_out}")
endif ()
if (NOT EXISTS "${_sources_out}")
  message(FATAL_ERROR "expected sources manifest missing: ${_sources_out}")
endif ()
if (NOT EXISTS "${_sources_sha256_out}")
  message(FATAL_ERROR "expected signature manifest missing: ${_sources_sha256_out}")
endif ()

file(READ "${_sources_out}" _actual_sources)
if (NOT _actual_sources STREQUAL _expected_sources)
  message(FATAL_ERROR
    "tags.json.sources does not match list_ava_sources.sh output.\n"
    "--- expected ---\n${_expected_sources}\n"
    "--- actual ---\n${_actual_sources}")
endif ()
file(READ "${_sources_sha256_out}" _actual_sources_sha256)
if (NOT _actual_sources_sha256 STREQUAL _expected_sources_sha256)
  message(FATAL_ERROR
    "tags.json.sources.sha256 does not match the deterministic source signature.\n"
    "--- expected ---\n${_expected_sources_sha256}\n"
    "--- actual ---\n${_actual_sources_sha256}")
endif ()

# Preserve known prior manifests, then force ctags failure and confirm neither
# is replaced and neither staging temp remains.
set(_prior "src/ava/PriorOnly.h\n")
set(_prior_sha256 "0000000000000000000000000000000000000000000000000000000000000000  src/ava/PriorOnly.h\n")
file(WRITE "${_sources_out}" "${_prior}")
file(WRITE "${_sources_sha256_out}" "${_prior_sha256}")
file(WRITE "${_out}" "stale-json\n")

set(_fake_fail "${AVA_TEST_ROOT}/fake-ctags-fail")
file(WRITE "${_fake_fail}"
"#!/bin/sh
echo \"fake-ctags-fail: deliberate failure\" >&2
exit 1
")
file(CHMOD "${_fake_fail}"
     PERMISSIONS OWNER_READ OWNER_WRITE OWNER_EXECUTE GROUP_READ GROUP_EXECUTE WORLD_READ WORLD_EXECUTE)

execute_process(
  COMMAND "${_script}" "${_out}" "${_fake_fail}" "${CMAKE_COMMAND}"
          "${FLOCK_EXECUTABLE}" "${_generation_lock}"
  WORKING_DIRECTORY "${AVA_SOURCE_DIR}"
  RESULT_VARIABLE _fail_rc
  OUTPUT_VARIABLE _fail_out
  ERROR_VARIABLE _fail_err
)
if (_fail_rc EQUAL 0)
  message(FATAL_ERROR "generate_ctags_json.sh unexpectedly succeeded with failing ctags")
endif ()

file(READ "${_sources_out}" _after_fail_sources)
if (NOT _after_fail_sources STREQUAL _prior)
  message(FATAL_ERROR
    "failing ctags run must not publish a new sources manifest.\n"
    "--- expected prior ---\n${_prior}\n"
    "--- actual ---\n${_after_fail_sources}")
endif ()
file(READ "${_sources_sha256_out}" _after_fail_sources_sha256)
if (NOT _after_fail_sources_sha256 STREQUAL _prior_sha256)
  message(FATAL_ERROR
    "failing ctags run must not publish a new signature manifest.\n"
    "--- expected prior ---\n${_prior_sha256}\n"
    "--- actual ---\n${_after_fail_sources_sha256}")
endif ()

file(GLOB _tmp_leftovers
  "${_sources_out}.tmp.*"
  "${_sources_sha256_out}.tmp.*")
if (_tmp_leftovers)
  message(FATAL_ERROR "manifest staging temp file(s) left behind: ${_tmp_leftovers}")
endif ()

# Relative output paths must still land beside the tags file after the script
# cds to the repository root for ctags path recording.
set(_rel_root "${AVA_TEST_ROOT}/relative-out")
file(MAKE_DIRECTORY "${_rel_root}/generated/ctags")
set(_rel_out_abs "${_rel_root}/generated/ctags/tags.json")
set(_rel_sources_abs "${_rel_out_abs}.sources")
set(_rel_sources_sha256_abs "${_rel_sources_abs}.sha256")
execute_process(
  COMMAND "${_script}" "generated/ctags/tags.json" "${_fake_ok}" "${CMAKE_COMMAND}"
          "${FLOCK_EXECUTABLE}" "${_generation_lock}"
  WORKING_DIRECTORY "${_rel_root}"
  RESULT_VARIABLE _rel_rc
  OUTPUT_VARIABLE _rel_out
  ERROR_VARIABLE _rel_err
)
if (NOT _rel_rc EQUAL 0)
  message(FATAL_ERROR
    "generate_ctags_json.sh failed with relative out path (exit ${_rel_rc})\n"
    "stdout: ${_rel_out}\nstderr: ${_rel_err}")
endif ()
if (NOT EXISTS "${_rel_out_abs}")
  message(FATAL_ERROR "relative out path did not produce ${_rel_out_abs}")
endif ()
if (NOT EXISTS "${_rel_sources_abs}")
  message(FATAL_ERROR "relative out path did not produce ${_rel_sources_abs}")
endif ()
if (NOT EXISTS "${_rel_sources_sha256_abs}")
  message(FATAL_ERROR "relative out path did not produce ${_rel_sources_sha256_abs}")
endif ()
file(READ "${_rel_sources_abs}" _rel_sources)
if (NOT _rel_sources STREQUAL _expected_sources)
  message(FATAL_ERROR
    "relative-out tags.json.sources mismatch.\n"
    "--- expected ---\n${_expected_sources}\n"
    "--- actual ---\n${_rel_sources}")
endif ()
file(READ "${_rel_sources_sha256_abs}" _rel_sources_sha256)
if (NOT _rel_sources_sha256 STREQUAL _expected_sources_sha256)
  message(FATAL_ERROR
    "relative-out tags.json.sources.sha256 mismatch.\n"
    "--- expected ---\n${_expected_sources_sha256}\n"
    "--- actual ---\n${_rel_sources_sha256}")
endif ()

# Deterministically prove that two ctags writers using the shared lock cannot
# overlap. The first fake ctags holds the lock; the wrapper reports when the
# second invocation has reached flock, then the first is released. Serialized
# execution must leave the second writer's tags content last.
set(_concurrent_dir "${AVA_TEST_ROOT}/ctags-lock")
set(_concurrent_out "${_concurrent_dir}/tags.json")
set(_concurrent_lock "${_generation_lock}")
set(_first_ctags "${_concurrent_dir}/fake-ctags-first")
set(_second_ctags "${_concurrent_dir}/fake-ctags-second")
set(_flock_wrapper "${_concurrent_dir}/flock-wrapper")
set(_orchestrator "${_concurrent_dir}/run-concurrent-ctags")
file(MAKE_DIRECTORY "${_concurrent_dir}")
file(WRITE "${_first_ctags}" [=[#!/bin/bash
set -euo pipefail
out=""
while [[ $# -gt 0 ]]; do
  if [[ "$1" == "-f" ]]; then shift; out="$1"; fi
  shift
done
: >"$CTAGS_FIRST_STARTED"
for ((i = 0; i < 1000; ++i)); do
  [[ -e "$CTAGS_RELEASE" ]] && break
  sleep 0.01
done
[[ -e "$CTAGS_RELEASE" ]] || exit 90
printf '%s\n' '{"writer":"first"}' >"$out"
]=])
file(WRITE "${_second_ctags}" [=[#!/bin/bash
set -euo pipefail
out=""
while [[ $# -gt 0 ]]; do
  if [[ "$1" == "-f" ]]; then shift; out="$1"; fi
  shift
done
printf '%s\n' '{"writer":"second"}' >"$out"
]=])
file(WRITE "${_flock_wrapper}" [=[#!/bin/bash
set -euo pipefail
if [[ "${AVA_LOCK_TEST_ROLE:-}" == "second" ]]; then
  : >"$CTAGS_SECOND_AT_FLOCK"
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
AVA_LOCK_TEST_ROLE=first "$GEN_SCRIPT" "$CTAGS_OUT" "$FIRST_CTAGS" \
  "$CMAKE_EXE" "$FLOCK_WRAPPER" "$GEN_LOCK" &
first_pid=$!
wait_for_file "$CTAGS_FIRST_STARTED"
AVA_LOCK_TEST_ROLE=second "$GEN_SCRIPT" "$CTAGS_OUT" "$SECOND_CTAGS" \
  "$CMAKE_EXE" "$FLOCK_WRAPPER" "$GEN_LOCK" &
second_pid=$!
wait_for_file "$CTAGS_SECOND_AT_FLOCK"
: >"$CTAGS_RELEASE"
wait "$first_pid"
wait "$second_pid"
]=])
foreach(_helper IN ITEMS
    "${_first_ctags}" "${_second_ctags}" "${_flock_wrapper}" "${_orchestrator}")
  file(CHMOD "${_helper}"
       PERMISSIONS OWNER_READ OWNER_WRITE OWNER_EXECUTE GROUP_READ GROUP_EXECUTE WORLD_READ WORLD_EXECUTE)
endforeach()
execute_process(
  COMMAND "${CMAKE_COMMAND}" -E env
          "GEN_SCRIPT=${_script}"
          "CTAGS_OUT=${_concurrent_out}"
          "FIRST_CTAGS=${_first_ctags}"
          "SECOND_CTAGS=${_second_ctags}"
          "CMAKE_EXE=${CMAKE_COMMAND}"
          "FLOCK_WRAPPER=${_flock_wrapper}"
          "REAL_FLOCK=${FLOCK_EXECUTABLE}"
          "GEN_LOCK=${_concurrent_lock}"
          "CTAGS_FIRST_STARTED=${_concurrent_dir}/first-started"
          "CTAGS_SECOND_AT_FLOCK=${_concurrent_dir}/second-at-flock"
          "CTAGS_RELEASE=${_concurrent_dir}/release-first"
          "${_orchestrator}"
  WORKING_DIRECTORY "${AVA_SOURCE_DIR}"
  RESULT_VARIABLE _concurrent_rc
  OUTPUT_VARIABLE _concurrent_stdout
  ERROR_VARIABLE _concurrent_stderr)
if (NOT _concurrent_rc EQUAL 0)
  message(FATAL_ERROR
    "concurrent ctags lock check failed (exit ${_concurrent_rc})\n"
    "stdout: ${_concurrent_stdout}\nstderr: ${_concurrent_stderr}")
endif ()
file(READ "${_concurrent_out}" _concurrent_tags)
if (NOT _concurrent_tags STREQUAL "{\"writer\":\"second\"}\n")
  message(FATAL_ERROR
    "ctags writers overlapped or executed out of order: ${_concurrent_tags}")
endif ()
file(GLOB _concurrent_temps
  "${_concurrent_out}.sources.tmp.*"
  "${_concurrent_out}.sources.sha256.tmp.*")
if (_concurrent_temps)
  message(FATAL_ERROR "concurrent ctags temp file(s) left behind: ${_concurrent_temps}")
endif ()
file(REMOVE_RECURSE "${_concurrent_dir}")
if (EXISTS "${_concurrent_dir}")
  message(FATAL_ERROR "failed to clean concurrent ctags test fixtures")
endif ()

message(STATUS "generate_ctags_json.sh source-manifest and lock checks passed")
