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

# Determine whether the available ctags supports JSON output.
#
# Probe `ctags --list-output-formats` and accept only a format literally named
# "json" whose AVAILABLE column (the third whitespace-separated field) is "yes".
#
set(CTAGS_HAS_JSON FALSE)
if (CTAGS_FOUND)
  execute_process(
    COMMAND "${CTAGS_EXECUTABLE}" --list-output-formats
    RESULT_VARIABLE _ctags_formats_result
    OUTPUT_VARIABLE _ctags_formats_output
    ERROR_QUIET
    OUTPUT_STRIP_TRAILING_WHITESPACE
  )
  if (_ctags_formats_result EQUAL 0)
    # Turn the multi-line output into a CMake list of lines, escaping any
    # literal semicolons first so they survive the newline-to-';' conversion.
    string(REPLACE ";" "\\;" _ctags_formats_output "${_ctags_formats_output}")
    string(REGEX REPLACE "\r?\n" ";" _ctags_format_lines "${_ctags_formats_output}")
    foreach(_line ${_ctags_format_lines})
      # Strip leading whitespace, then skip blank lines and the header line.
      string(REGEX REPLACE "^[ \t]+" "" _line "${_line}")
      if (_line STREQUAL "" OR _line MATCHES "^#")
        continue()
      endif ()
      # Collapse runs of whitespace into list separators and drop empties.
      string(REGEX REPLACE "[ \t]+" ";" _tokens "${_line}")
      list(FILTER _tokens EXCLUDE REGEX "^$")
      list(LENGTH _tokens _ntokens)
      if (_ntokens GREATER_EQUAL 3)
        list(GET _tokens 0 _fmt_name)
        list(GET _tokens 2 _fmt_available)
        if ("${_fmt_name}" STREQUAL "json" AND "${_fmt_available}" STREQUAL "yes")
          set(CTAGS_HAS_JSON TRUE)
          break()
        endif ()
      endif ()
    endforeach()
  endif ()
endif ()
unset(_ctags_formats_result)
unset(_ctags_formats_output)
unset(_ctags_format_lines)
unset(_line)
unset(_tokens)
unset(_ntokens)
unset(_fmt_name)
unset(_fmt_available)

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

#------------------------------------------------------------------------------
# Create the pseudo target `ctags-json` that generates tags.json
#
set(_ava_ctags_json_out "${CMAKE_BINARY_DIR}/generated/ctags/tags.json")

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
set(_ava_print_members_dir "${CMAKE_BINARY_DIR}/generated/print_members")

#------------------------------------------------------------------------------
# Create the `source_files` list, containing all required print_members.cpp files.
#
set(_ava_source_list_file "${_ava_print_members_dir}/source_files")

# Determine if we need to run the generator at configure time.
#
# This configure-time generation only exists so CMake can discover the set of
# generated print_members.cpp files (read from source_files) and add them to the
# ava_debug library below. It therefore runs only when source_files or any listed
# cpp is absent (e.g. a fresh checkout). Staleness against tags.json -- e.g.
# after a manual `ctags-json` refresh -- is handled at BUILD time by the
# add_custom_command(OUTPUT ...) declared further below, so it is intentionally
# NOT checked here.
set(NEED_GENERATION FALSE)

if (NOT EXISTS "${_ava_source_list_file}")
  message(STATUS "Need generation of print_members.cpp files because ${_ava_source_list_file} doesn't exist.")
  set(NEED_GENERATION TRUE)
else()
  # File exists, read it.
  file(STRINGS "${_ava_source_list_file}" REL_FILES)
  foreach(REL_FILE IN LISTS REL_FILES)
    if (NOT EXISTS "${_ava_print_members_dir}/${REL_FILE}")
      set(NEED_GENERATION TRUE)
      message(STATUS "Need (re)generation of print_members.cpp files because \"${_ava_print_members_dir}/${REL_FILE}\" doesn't exist.")
      break() # Stop checking as soon as we find a missing file.
    endif ()
  endforeach()
endif ()

# Run the generator if needed.
if (NOT NEED_GENERATION)
  message(STATUS "Not regenerating print_members.cpp files")
else ()
  # Manually run whatever ctags-json does here.
  message(STATUS "Generating ${_ava_ctags_json_out}")
  execute_process(
    COMMAND "${CMAKE_SOURCE_DIR}/cmake/scripts/generate_ctags_json.sh"
            "${_ava_ctags_json_out}"
            "${CTAGS_EXECUTABLE}"
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
  
  # Manually run the python script.
  message(STATUS "Generating print_members.cpp files in ${_ava_print_members_dir}")
  file(MAKE_DIRECTORY "${_ava_print_members_dir}")
  execute_process(
    COMMAND "${PYTHON3_EXECUTABLE}"
            "${CMAKE_SOURCE_DIR}/cmake/scripts/generate_print_members.py"
            "${_ava_ctags_json_out}"
            "${_ava_print_members_dir}"
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
set(_ava_generated_source_files "")
file(STRINGS "${_ava_source_list_file}" REL_FILES)
foreach(REL_FILE IN LISTS REL_FILES)
  list(APPEND _ava_generated_source_files "${_ava_print_members_dir}/${REL_FILE}")
endforeach()

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
  OUTPUT ${_ava_generated_source_files}
  COMMAND "${CMAKE_COMMAND}" -E make_directory "${_ava_print_members_dir}"
  COMMAND "${PYTHON3_EXECUTABLE}"
          "${CMAKE_SOURCE_DIR}/cmake/scripts/generate_print_members.py"
          "${_ava_ctags_json_out}"
          "${_ava_print_members_dir}"
  DEPENDS "${_ava_ctags_json_stamp}"
          "${CMAKE_SOURCE_DIR}/cmake/scripts/generate_print_members.py"
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
# The .sh runs to completion before the .py starts (COMMANDS of one edge run
# sequentially), so the .py always reads a fully-written tags.json.
add_custom_target(generate-print-members
  COMMAND "${CMAKE_COMMAND}" -E make_directory "${_ava_print_members_dir}"
  COMMAND "${CMAKE_SOURCE_DIR}/cmake/scripts/generate_ctags_json.sh"
          "${_ava_ctags_json_out}"
          "${CTAGS_EXECUTABLE}"
  COMMAND "${PYTHON3_EXECUTABLE}"
          "${CMAKE_SOURCE_DIR}/cmake/scripts/generate_print_members.py"
          "${_ava_ctags_json_out}"
          "${_ava_print_members_dir}"
  DEPENDS "${CMAKE_SOURCE_DIR}/cmake/scripts/generate_ctags_json.sh"
          "${CMAKE_SOURCE_DIR}/cmake/scripts/list_ava_sources.sh"
          "${CMAKE_SOURCE_DIR}/cmake/scripts/generate_print_members.py"
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
  ${_ava_generated_source_files}
)

target_include_directories(ava_debug PUBLIC "${CMAKE_SOURCE_DIR}/src")

ava_apply_debugging(ava_debug)
ava_apply_warnings(ava_debug)
ava_apply_sanitizers(ava_debug)
