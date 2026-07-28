# Build-time debug printer generation only runs when libcwd debug support is
# effectively enabled, i.e. when OptionEnableLibcwd is ON.
#
# That generation requires ctags with JSON output, a Universal Ctags feature.
#
# The whole detection is therefore gated on OptionEnableLibcwd, and a JSON-
# capable ctags is then a hard requirement. Configure with -DEnableLibcwd=OFF
# to skip this entirely.
#

# CTAGS_EXECUTABLE is the cache variable holding the path to the binary, or
# CTAGS_EXECUTABLE-NOTFOUND when no ctags is on the PATH. CTAGS_FOUND is the
# normalized boolean recomputed every configure from CTAGS_EXECUTABLE.
find_program(CTAGS_EXECUTABLE ctags
  DOC "Path to the ctags executable; used by build-time code generation targets."
)
if (CTAGS_EXECUTABLE)
  set(CTAGS_FOUND TRUE)
else()
  set(CTAGS_FOUND FALSE)
endif ()

set(CTAGS_HAS_JSON FALSE)
if (CTAGS_FOUND)
  include("${CMAKE_SOURCE_DIR}/cmake/DetectCtagsJson.cmake")
  ava_detect_ctags_json("${CTAGS_EXECUTABLE}" CTAGS_HAS_JSON)
endif ()

# Print find result(s).
if (CTAGS_FOUND)
  message(STATUS "Found ctags: ${CTAGS_EXECUTABLE}")
  if (CTAGS_HAS_JSON)
    message(STATUS "ctags supports JSON output (Universal Ctags)")
  else()
    message(STATUS "ctags does not support JSON output")
  endif ()
else()
  message(STATUS "ctags not found.")
endif ()

# Require a JSON-capable ctags (Universal Ctags) when libcwd is enabled.
if (NOT CTAGS_HAS_JSON)
  message(FATAL_ERROR
    "EnableLibcwd is ON, which requires a ctags with JSON output support "
    "(Universal Ctags with the 'json' output format available) for "
    "build-time debug printer generation, but no such ctags was found. "
    "Install Universal Ctags with JSON support, or work around this by "
    "configuring with -DEnableLibcwd=OFF.")
endif ()

find_program(PYTHON3_EXECUTABLE python3
  DOC "Path to the python3 executable; used to generate print_members definitions."
  REQUIRED
)

find_program(FLOCK_EXECUTABLE flock
  DOC "Path to flock; serializes debug tags and print_members generation."
)
if (NOT FLOCK_EXECUTABLE)
  message(FATAL_ERROR
    "EnableLibcwd is ON, which requires the flock executable to serialize "
    "debug tags and print_members generation across concurrent build processes. "
    "Install util-linux (providing flock), or configure with -DEnableLibcwd=OFF.")
endif ()
message(STATUS "Found flock: ${FLOCK_EXECUTABLE}")

#------------------------------------------------------------------------------
# Create the pseudo target `ctags-json` that generates tags.json
#
set(_ava_ctags_json_out "${CMAKE_BINARY_DIR}/generated/ctags/tags.json")
set(_ava_print_members_dir "${CMAKE_BINARY_DIR}/generated/print_members")
set(_ava_generation_lock "${_ava_print_members_dir}/generation.lock")

# Ninja decides whether to rerun the print_members generator by comparing the
# build-log-RECORDED mtime of the generated .cpp outputs against the CURRENT
# mtime of the edge's inputs (it does not re-stat the outputs for this check).
# If the .cpp edge depended on tags.json directly, then every configure that
# refreshes tags.json -- which configure always does when it has to regenerate
# the printers -- would leave tags.json newer than the .cpp edge's recorded
# output mtime, so the very next build would needlessly rerun the generator even
# though configure already regenerated the .cpp.
#
# The stamp breaks that coupling: the .cpp edge depends on the stamp, and ONLY
# `ctags-json` advances the stamp. A manual `ctags-json` refresh is therefore
# picked up by the next build (the intended behavior), while a configure-time or
# `generate-print-members`-time regeneration -- which already refreshed the .cpp
# themselves -- deliberately leaves the stamp untouched, so no follow-up build
# reruns the generator.
set(_ava_ctags_json_stamp "${CMAKE_BINARY_DIR}/generated/ctags/tags.json.stamp")

# Ensure the stamp exists before the build-time edge can reference it. It is
# created only when absent; its mtime is never bumped here (only `ctags-json`
# does that), see the rationale above.
if (NOT EXISTS "${_ava_ctags_json_stamp}")
  file(WRITE "${_ava_ctags_json_stamp}" "")
endif ()

add_custom_target(ctags-json
  COMMAND "${CMAKE_SOURCE_DIR}/cmake/scripts/generate_ctags_json.sh"
          "${_ava_ctags_json_out}"
          "${CTAGS_EXECUTABLE}"
          "${CMAKE_COMMAND}"
          "${FLOCK_EXECUTABLE}"
          "${_ava_generation_lock}"
  COMMAND "${CMAKE_COMMAND}" -E touch "${_ava_ctags_json_stamp}"
  DEPENDS "${CMAKE_SOURCE_DIR}/cmake/scripts/generate_ctags_json.sh"
          "${CMAKE_SOURCE_DIR}/cmake/scripts/list_ava_sources.sh"
  COMMENT "Generating JSON tags for debug printer generation: ${_ava_ctags_json_out}"
  VERBATIM
)

# The print_members.cpp files live under here. The build-time rule that
# (re)generates them and the `generate-print-members` pseudo-target are declared
# further down, once the set of output files is known.
#
# Exact source signature whose successful printer generation this marker proves.
set(_ava_print_members_completion
    "${_ava_print_members_dir}/generation-complete.sources.sha256")
set(_ava_run_generate_print_members
    "${CMAKE_SOURCE_DIR}/cmake/scripts/run_generate_print_members.sh")

#------------------------------------------------------------------------------
# Create the `source_files` list, containing all required print_members.cpp files.
#
set(_ava_source_list_file "${_ava_print_members_dir}/source_files")
# Exact sorted src/ava input list and matching content signatures recorded by
# generate_ctags_json.sh beside tags.json.
set(_ava_ctags_json_sources "${_ava_ctags_json_out}.sources")
set(_ava_ctags_json_sources_sha256 "${_ava_ctags_json_sources}.sha256")

# Determine if we need to run the generator at configure time.
#
# Configure-time generation exists so CMake can discover the set of generated
# print_members.cpp files (read from source_files) and add them to the ava_debug
# library below. It must also refresh stale printers when the src/ava tree has
# changed since the last tags/printer run -- otherwise an existing build tree
# keeps obsolete namespace buckets after type moves, renames, or content edits.
#
# NEED_GENERATION is set when any of the following hold:
#  - tags.json, either input manifest, the printer completion marker,
#    source_files, or a listed generated .cpp is absent
#  - the current authoritative source enumeration differs from tags.json.sources
#  - either recorded signature differs from the deterministic source-content
#    signature
#
# Build-time staleness after a manual `ctags-json` (stamp advanced) remains
# handled by the add_custom_command(OUTPUT ...) edge below. Configure-time and
# generate-print-members refreshes deliberately leave the stamp untouched so a
# follow-up plain build does not rerun the generator endlessly.
set(NEED_GENERATION FALSE)

# Enumerate the exact sorted source list ctags will see. list_ava_sources.sh is
# authoritative; the recursive glob below exists only to make plain builds ask
# CMake to reconfigure after matching files are added or deleted.
execute_process(
  COMMAND "${CMAKE_SOURCE_DIR}/cmake/scripts/list_ava_sources.sh"
  WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
  RESULT_VARIABLE _ava_list_sources_result
  OUTPUT_VARIABLE _ava_current_sources_manifest
  ERROR_VARIABLE _ava_list_sources_error
)
if (NOT _ava_list_sources_result EQUAL 0)
  if (_ava_list_sources_error)
    message(STATUS "${_ava_list_sources_error}")
  endif ()
  message(FATAL_ERROR
    "Failed to enumerate src/ava sources via cmake/scripts/list_ava_sources.sh "
    "(exit ${_ava_list_sources_result}).")
endif ()

# Convert the newline-terminated manifest into a CMake list.
if (_ava_current_sources_manifest STREQUAL "")
  set(_ava_current_sources "")
else()
  string(REGEX REPLACE "\n$" "" _ava_current_sources_text
         "${_ava_current_sources_manifest}")
  string(REPLACE "\n" ";" _ava_current_sources
         "${_ava_current_sources_text}")
endif ()

file(GLOB_RECURSE _ava_watched_source_files
  CONFIGURE_DEPENDS
  LIST_DIRECTORIES FALSE
  "${CMAKE_SOURCE_DIR}/src/ava/*.h"
  "${CMAKE_SOURCE_DIR}/src/ava/*.cpp"
  "${CMAKE_SOURCE_DIR}/src/ava/*.cxx"
)
list(FILTER _ava_watched_source_files EXCLUDE REGEX "/\\.git/")

set(_ava_watched_sources "")
foreach(_ava_src_abs IN LISTS _ava_watched_source_files)
  file(RELATIVE_PATH _ava_src_rel "${CMAKE_SOURCE_DIR}" "${_ava_src_abs}")
  list(APPEND _ava_watched_sources "${_ava_src_rel}")
endforeach()
if (NOT "${_ava_watched_sources}" STREQUAL "${_ava_current_sources}")
  message(FATAL_ERROR
    "The CONFIGURE_DEPENDS src/ava glob does not match the authoritative "
    "cmake/scripts/list_ava_sources.sh output.")
endif ()

# Existing source edits also need to trigger reconfiguration on a plain build.
# Compute the exact signature text while constructing those configure inputs.
set(_ava_current_source_files "")
set(_ava_current_sources_sha256 "")
foreach(_ava_src_rel IN LISTS _ava_current_sources)
  set(_ava_src_abs "${CMAKE_SOURCE_DIR}/${_ava_src_rel}")
  list(APPEND _ava_current_source_files "${_ava_src_abs}")
  file(SHA256 "${_ava_src_abs}" _ava_src_sha256)
  string(APPEND _ava_current_sources_sha256
         "${_ava_src_sha256}  ${_ava_src_rel}\n")
endforeach()
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
             ${_ava_current_source_files})

if (NOT EXISTS "${_ava_ctags_json_out}")
  message(STATUS "Need generation of print_members.cpp files because ${_ava_ctags_json_out} doesn't exist.")
  set(NEED_GENERATION TRUE)
endif ()

if (NOT NEED_GENERATION AND NOT EXISTS "${_ava_ctags_json_sources}")
  message(STATUS "Need generation of print_members.cpp files because ${_ava_ctags_json_sources} doesn't exist.")
  set(NEED_GENERATION TRUE)
endif ()

if (NOT NEED_GENERATION AND NOT EXISTS "${_ava_ctags_json_sources_sha256}")
  message(STATUS "Need generation of print_members.cpp files because ${_ava_ctags_json_sources_sha256} doesn't exist.")
  set(NEED_GENERATION TRUE)
endif ()

if (NOT NEED_GENERATION AND NOT EXISTS "${_ava_print_members_completion}")
  message(STATUS "Need generation of print_members.cpp files because ${_ava_print_members_completion} doesn't exist.")
  set(NEED_GENERATION TRUE)
endif ()

if (NOT NEED_GENERATION AND NOT EXISTS "${_ava_source_list_file}")
  message(STATUS "Need generation of print_members.cpp files because ${_ava_source_list_file} doesn't exist.")
  set(NEED_GENERATION TRUE)
endif ()

if (NOT NEED_GENERATION)
  file(STRINGS "${_ava_source_list_file}" REL_FILES)
  foreach(REL_FILE IN LISTS REL_FILES)
    if (NOT EXISTS "${_ava_print_members_dir}/${REL_FILE}")
      set(NEED_GENERATION TRUE)
      message(STATUS "Need (re)generation of print_members.cpp files because \"${_ava_print_members_dir}/${REL_FILE}\" doesn't exist.")
      break() # Stop checking as soon as we find a missing file.
    endif ()
  endforeach()
endif ()

# Added/deleted sources: compare the exact live enumerator output to the path
# manifest generate_ctags_json.sh recorded on its last successful run.
if (NOT NEED_GENERATION)
  file(READ "${_ava_ctags_json_sources}" _ava_recorded_sources_manifest)
  if (NOT "${_ava_current_sources_manifest}" STREQUAL
          "${_ava_recorded_sources_manifest}")
    set(NEED_GENERATION TRUE)
    message(STATUS
      "Need (re)generation of print_members.cpp files because the src/ava source "
      "list differs from ${_ava_ctags_json_sources}.")
  endif ()
endif ()

# Edited sources (including type namespace moves): compare content signatures,
# never mtimes, so equal timestamp resolution cannot cause configure loops.
if (NOT NEED_GENERATION)
  file(READ "${_ava_ctags_json_sources_sha256}"
       _ava_recorded_sources_sha256)
  if (NOT "${_ava_current_sources_sha256}" STREQUAL
          "${_ava_recorded_sources_sha256}")
    set(NEED_GENERATION TRUE)
    message(STATUS
      "Need (re)generation of print_members.cpp files because the src/ava source "
      "content signature differs from ${_ava_ctags_json_sources_sha256}.")
  endif ()
endif ()

# Only the completion marker proves that printers were generated successfully
# from the current inputs. In particular, ctags may have advanced both of its
# manifests before a failed Python run.
if (NOT NEED_GENERATION)
  file(READ "${_ava_print_members_completion}"
       _ava_completed_sources_sha256)
  if (NOT "${_ava_current_sources_sha256}" STREQUAL
          "${_ava_completed_sources_sha256}")
    set(NEED_GENERATION TRUE)
    message(STATUS
      "Need (re)generation of print_members.cpp files because the completed "
      "printer source signature differs from ${_ava_print_members_completion}.")
  endif ()
endif ()

# Run the generator if needed. Does NOT advance tags.json.stamp (only the
# manual ctags-json target does), so a subsequent plain build will not loop
# through the build-time printer edge.
if (NOT NEED_GENERATION)
  message(STATUS "Not regenerating print_members.cpp files")
else ()
  # Manually run whatever ctags-json does here (without touching the stamp).
  message(STATUS "Generating ${_ava_ctags_json_out}")
  execute_process(
    COMMAND "${CMAKE_SOURCE_DIR}/cmake/scripts/generate_ctags_json.sh"
            "${_ava_ctags_json_out}"
            "${CTAGS_EXECUTABLE}"
            "${CMAKE_COMMAND}"
            "${FLOCK_EXECUTABLE}"
            "${_ava_generation_lock}"
    RESULT_VARIABLE GEN_RESULT
    OUTPUT_VARIABLE GEN_OUTPUT
    ERROR_VARIABLE GEN_ERROR
  )

  if (GEN_OUTPUT)
    message(STATUS "${GEN_OUTPUT}")
  endif ()
  if (GEN_ERROR)
    message(STATUS "${GEN_ERROR}")
  endif ()

  if (NOT GEN_RESULT EQUAL 0)
    message(FATAL_ERROR "Failed to successfully generate the \"${_ava_ctags_json_out}\" file.")
  endif ()

  # Run Python transactionally: the helper invalidates completion before launch
  # and atomically republishes the exact source signature only after success.
  message(STATUS "Generating print_members.cpp files in ${_ava_print_members_dir}")
  execute_process(
    COMMAND "${_ava_run_generate_print_members}"
            "${PYTHON3_EXECUTABLE}"
            "${CMAKE_SOURCE_DIR}/cmake/scripts/generate_print_members.py"
            "${_ava_ctags_json_out}"
            "${_ava_print_members_dir}"
            "${_ava_ctags_json_sources_sha256}"
            "${_ava_print_members_completion}"
            "${FLOCK_EXECUTABLE}"
            "${_ava_generation_lock}"
    RESULT_VARIABLE GEN_RESULT
    OUTPUT_VARIABLE GEN_OUTPUT
    ERROR_VARIABLE GEN_ERROR
  )

  if (GEN_OUTPUT)
    message(STATUS "${GEN_OUTPUT}")
  endif ()
  if (GEN_ERROR)
    message(STATUS "${GEN_ERROR}")
  endif ()

  if (NOT GEN_RESULT EQUAL 0)
    message(FATAL_ERROR "Failed to successfully generate print_members.cpp files.")
  endif ()
endif ()

#------------------------------------------------------------------------------
# Construct a list with all generated source files.
#
# Watch source_files for content changes so that when a build-time regeneration
# adds or removes a print_members.cpp (because a class toggled opt-in/opt-out),
# the next build re-runs cmake configure and _ava_generated_source_files is
# adjusted accordingly. Without this, the configure-time list would go stale
# whenever the set of generated files changes during a build.
set(_ava_generated_source_files "")
file(STRINGS "${_ava_source_list_file}" REL_FILES)
foreach(REL_FILE IN LISTS REL_FILES)
  list(APPEND _ava_generated_source_files "${_ava_print_members_dir}/${REL_FILE}")
endforeach()
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${_ava_source_list_file}")

#------------------------------------------------------------------------------
# Build-time generation rule for the print_members.cpp files.
#
# The edge depends on the stamp (NOT on tags.json directly) for the reason
# documented where the stamp is declared: Ninja reruns this command whenever the
# stamp's current mtime is newer than the RECORDED mtime of these outputs, and
# only `ctags-json` advances the stamp. So a manual `ctags-json` refresh is
# picked up by the next normal build, while a configure-time or
# `generate-print-members`-time regeneration (which already wrote the .cpp) does
# not cause a follow-up build to rerun the generator. The .py script itself is
# also a dependency so editing it forces a regeneration. The generator reads
# tags.json at run time; tags.json is intentionally NOT listed here so that a
# configure-time refresh of tags.json cannot trigger this edge.
#
# The guard handles the degenerate case where no type opted into print_members:
# add_custom_command requires at least one OUTPUT, and there is nothing to drive.
if (_ava_generated_source_files)
add_custom_command(
  OUTPUT ${_ava_generated_source_files} "${_ava_print_members_completion}"
  COMMAND "${_ava_run_generate_print_members}"
          "${PYTHON3_EXECUTABLE}"
          "${CMAKE_SOURCE_DIR}/cmake/scripts/generate_print_members.py"
          "${_ava_ctags_json_out}"
          "${_ava_print_members_dir}"
          "${_ava_ctags_json_sources_sha256}"
          "${_ava_print_members_completion}"
          "${FLOCK_EXECUTABLE}"
          "${_ava_generation_lock}"
  DEPENDS "${_ava_ctags_json_stamp}"
          "${CMAKE_SOURCE_DIR}/cmake/scripts/generate_print_members.py"
          "${_ava_run_generate_print_members}"
  COMMENT "Generating print_members definitions into ${_ava_print_members_dir}"
  VERBATIM
)

# Manual pseudo-target that forces a full, unconditional refresh of both
# tags.json and the print_members.cpp files.
#
# It runs the two scripts directly as its own COMMANDS rather than depending on
# `ctags-json` or on the .cpp edge. Two reasons:
#
#  - It must ALWAYS run generate_print_members.py, but the .cpp edge is planned
#    from the mtimes at build start, so it would only fire when already stale --
#    a clean-state `generate-print-members` would otherwise be a no-op for the
#    printers. Running the .py as a COMMAND of this always-out-of-date target
#    guarantees it.
#
#  - It must NOT advance the stamp. Depending on `ctags-json` would touch the
#    stamp, and since this target refreshes the .cpp as a side effect (not via
#    the Ninja-tracked .cpp edge), Ninja's recorded output mtime for the .cpp
#    would stay behind the stamp, making the very next build rerun the generator
#    (the "python runs again" symptom). Running the scripts directly leaves the
#    stamp -- and thus the .cpp edge -- untouched, so the next plain build only
#    recompiles the freshly-written .cpp without regenerating them.
#
# The ctags command runs to completion before the transactional printer helper
# starts (COMMANDS of one edge run sequentially), so Python and its eventual
# completion marker always use fully-published tags manifests.
add_custom_target(generate-print-members
  COMMAND "${CMAKE_SOURCE_DIR}/cmake/scripts/generate_ctags_json.sh"
          "${_ava_ctags_json_out}"
          "${CTAGS_EXECUTABLE}"
          "${CMAKE_COMMAND}"
          "${FLOCK_EXECUTABLE}"
          "${_ava_generation_lock}"
  COMMAND "${_ava_run_generate_print_members}"
          "${PYTHON3_EXECUTABLE}"
          "${CMAKE_SOURCE_DIR}/cmake/scripts/generate_print_members.py"
          "${_ava_ctags_json_out}"
          "${_ava_print_members_dir}"
          "${_ava_ctags_json_sources_sha256}"
          "${_ava_print_members_completion}"
          "${FLOCK_EXECUTABLE}"
          "${_ava_generation_lock}"
  DEPENDS "${CMAKE_SOURCE_DIR}/cmake/scripts/generate_ctags_json.sh"
          "${CMAKE_SOURCE_DIR}/cmake/scripts/list_ava_sources.sh"
          "${CMAKE_SOURCE_DIR}/cmake/scripts/generate_print_members.py"
          "${_ava_run_generate_print_members}"
  COMMENT "Forcing regeneration of tags.json and print_members.cpp files"
  VERBATIM
)
endif ()

# Tell CMake that these files are generated so IDEs treat them nicely.
set_source_files_properties(${_ava_generated_source_files} PROPERTIES GENERATED TRUE)

#------------------------------------------------------------------------------
# Combine everything into a library.
#
add_library(ava_debug
  "print_members_on.cpp"
  "maxlen.cpp"
  "debug_ostream_operators.cpp"
  ${_ava_generated_source_files}
)

target_include_directories(ava_debug
  PUBLIC
  "${CMAKE_BINARY_DIR}/src"   # Needed for ava/
    "${CMAKE_SOURCE_DIR}/src"
)

# The generated print_members.cpp files #include headers from ava_terminal
# (e.g. ComplexChar.h, which pulls in GraphemeCluster.h). GraphemeCluster and
# Border use AVA_PRINT_ON_MEMBERS — a hand-written print_members — so the
# generated .o files reference but do not define their print_members.
# Other dependencies might exist if, for example, a generated print_members
# uses a `to_string`. These should be added when encountered.
target_link_libraries(ava_debug
  PUBLIC
    AVA::terminal       # ava::tui::terminal::GraphemeCluster::print_members
    ava_agent           # ava::agent::to_string
)

ava_apply_debugging(ava_debug)
ava_apply_warnings(ava_debug)
ava_apply_sanitizers(ava_debug)
