cmake_minimum_required(VERSION 3.25...4.3.4)

project(ava
  VERSION 1.0.0
  LANGUAGES CXX
  DESCRIPTION "OpenAI-first C++23 terminal coding agent with an ncursesw TUI"
)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

#==============================================================================
# Begin of gitache configuration.

set(GITACHE_PACKAGES libcwd)  # List of requested packages.

include(FetchContent)

# If a local gitache submodule is present then use that rather than downloading one.
if (EXISTS ${CMAKE_CURRENT_LIST_DIR}/gitache/.git)
  # This will disable the use of the GIT_REPOSITORY/GIT_TAG below, and disable the
  # FetchContent- download and update step. Instead, use the gitache submodule as-is.
  set(FETCHCONTENT_SOURCE_DIR_GITACHE "${CMAKE_CURRENT_LIST_DIR}/gitache" CACHE INTERNAL "" FORCE)
endif ()

FetchContent_Declare(
  gitache
  GIT_REPOSITORY "https://github.com/CarloWood/gitache.git"
  GIT_TAG        "stable"       # Normally use either "stable" or a fixed SHA1 to freeze the version here!
                                # Using "master" might be *unstable*.
)
FetchContent_MakeAvailable(gitache)
# End of gitache configuration.

#==============================================================================
# This project uses aicxx modules.
include(cmake/aicxx/Project NO_POLICY_SCOPE)

#==============================================================================
# OPTIONS

set(AVA_GENERATED_INCLUDE_DIR "${CMAKE_CURRENT_BINARY_DIR}/generated")
set(AVA_VERSION_DISPLAY "${PROJECT_VERSION_MAJOR}.${PROJECT_VERSION_MINOR}")
file(MAKE_DIRECTORY "${AVA_GENERATED_INCLUDE_DIR}/ava/core")
configure_file(
  "${CMAKE_CURRENT_SOURCE_DIR}/src/ava/core/version.h.in"
  "${AVA_GENERATED_INCLUDE_DIR}/ava/core/version.h"
  @ONLY
)

option(AVA_BUILD_TESTS "Build AVA tests" ON)
option(AVA_ENABLE_SANITIZERS "Build AVA with AddressSanitizer and UndefinedBehaviorSanitizer" OFF)

#==============================================================================
# OPTIONAL TOOLS

# Build-time debug printer generation (which feeds src/ava/debug) only runs
# when libcwd debug support is effectively enabled, i.e. when the aicxx-derived
# flag OptionEnableLibcwd is ON (note: the raw EnableLibcwd cache entry is a
# tri-state sentinel that cw_option deliberately clobbers, so always use
# OptionEnableLibcwd). That generation requires ctags with JSON output, a
# Universal Ctags feature that Exuberant Ctags and the BSD/emacs ctags lack.
# The whole detection is therefore gated on OptionEnableLibcwd, and a
# JSON-capable ctags is then a hard requirement. Configure with
# -DEnableLibcwd=OFF to skip this entirely.
if(OptionEnableLibcwd)
  # CTAGS_EXECUTABLE is the cache variable holding the path to the binary, or
  # CTAGS_EXECUTABLE-NOTFOUND when no ctags is on the PATH. CTAGS_FOUND is the
  # normalized boolean recomputed every configure from CTAGS_EXECUTABLE.
  find_program(CTAGS_EXECUTABLE ctags
    DOC "Path to the ctags executable; used by build-time code generation targets."
  )
  if(CTAGS_EXECUTABLE)
    set(CTAGS_FOUND TRUE)
  else()
    set(CTAGS_FOUND FALSE)
  endif()

  # Determine whether the available ctags supports JSON output. Probe
  # `ctags --list-output-formats` and accept only a format literally named
  # "json" whose AVAILABLE column (the third whitespace-separated field) is
  # "yes".
  set(CTAGS_HAS_JSON FALSE)
  if(CTAGS_FOUND)
    execute_process(
      COMMAND "${CTAGS_EXECUTABLE}" --list-output-formats
      RESULT_VARIABLE _ctags_formats_result
      OUTPUT_VARIABLE _ctags_formats_output
      ERROR_QUIET
      OUTPUT_STRIP_TRAILING_WHITESPACE
    )
    if(_ctags_formats_result EQUAL 0)
      # Turn the multi-line output into a CMake list of lines, escaping any
      # literal semicolons first so they survive the newline-to-';' conversion.
      string(REPLACE ";" "\\;" _ctags_formats_output "${_ctags_formats_output}")
      string(REGEX REPLACE "\r?\n" ";" _ctags_format_lines "${_ctags_formats_output}")
      foreach(_line ${_ctags_format_lines})
        # Strip leading whitespace, then skip blank lines and the header line.
        string(REGEX REPLACE "^[ \t]+" "" _line "${_line}")
        if(_line STREQUAL "" OR _line MATCHES "^#")
          continue()
        endif()
        # Collapse runs of whitespace into list separators and drop empties.
        string(REGEX REPLACE "[ \t]+" ";" _tokens "${_line}")
        list(FILTER _tokens EXCLUDE REGEX "^$")
        list(LENGTH _tokens _ntokens)
        if(_ntokens GREATER_EQUAL 3)
          list(GET _tokens 0 _fmt_name)
          list(GET _tokens 2 _fmt_available)
          if("${_fmt_name}" STREQUAL "json" AND "${_fmt_available}" STREQUAL "yes")
            set(CTAGS_HAS_JSON TRUE)
            break()
          endif()
        endif()
      endforeach()
    endif()
  endif()
  unset(_ctags_formats_result)
  unset(_ctags_formats_output)
  unset(_ctags_format_lines)
  unset(_line)
  unset(_tokens)
  unset(_ntokens)
  unset(_fmt_name)
  unset(_fmt_available)

  if(CTAGS_FOUND)
    message(STATUS "Found ctags: ${CTAGS_EXECUTABLE}")
    if(CTAGS_HAS_JSON)
      message(STATUS "ctags supports JSON output (Universal Ctags)")
    else()
      message(STATUS "ctags does not support JSON output")
    endif()
  else()
    message(STATUS "ctags not found.")
  endif()

  # Require a JSON-capable ctags (Universal Ctags) when libcwd is enabled.
  if(NOT CTAGS_HAS_JSON)
    message(FATAL_ERROR
      "EnableLibcwd is ON, which requires a ctags with JSON output support "
      "(Universal Ctags with the 'json' output format available) for "
      "build-time debug printer generation, but no such ctags was found. "
      "Install Universal Ctags with JSON support, or work around this by "
      "configuring with -DEnableLibcwd=OFF.")
  endif()

  #----------------------------------------------------------------------
  # ctags-json pseudo-target (goal 03): manually regenerate the JSON tags file
  # that feeds debug printer generation. It is a phony custom target, so it
  # only runs when built explicitly (cmake --build <build> --target ctags-json);
  # a normal build never triggers it. It deliberately depends only on the
  # scripts involved and on NO .h/.cpp/.cxx source file, so editing a header
  # does not regenerate it and a normal build pays no cost. Forcing regeneration
  # after a Debug<->Release configuration switch is handled by the
  # build-integration target (goal 06), not here.
  set(_ava_ctags_json_out "${CMAKE_BINARY_DIR}/generated/ctags/tags.json")
  add_custom_target(ctags-json
    COMMAND "${CMAKE_SOURCE_DIR}/cmake/scripts/generate_ctags_json.sh"
            "${_ava_ctags_json_out}"
            "${CTAGS_EXECUTABLE}"
    DEPENDS "${CMAKE_SOURCE_DIR}/cmake/scripts/generate_ctags_json.sh"
            "${CMAKE_SOURCE_DIR}/cmake/scripts/list_ava_sources.sh"
    COMMENT "Generating JSON tags for debug printer generation: ${_ava_ctags_json_out}"
    VERBATIM
  )
endif()

function(ava_apply_debugging target)
  target_link_libraries(${target} PRIVATE ${AICXX_OBJECTS_LIST})
endfunction()

function(ava_apply_warnings target)
  if(MSVC)
    target_compile_options(${target} PRIVATE /W4 /WX)
  else()
    target_compile_options(${target} PRIVATE -Wall -Werror -Wextra -Wpedantic -Wundef)
    if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU" AND CMAKE_CXX_COMPILER_VERSION VERSION_LESS 14)
      # Keep GCC's optimizer-driven maybe-uninitialized diagnostics visible, but
      # do not promote them to errors for GCC 13.x. That release can report false
      # positives through libstdc++ internals such as std::regex, std::optional,
      # and std::variant when Release optimization inlines those implementations.
      target_compile_options(${target} PRIVATE -Wno-error=maybe-uninitialized)
    endif()
    # Limit number of errors to two.
    target_compile_options(${target} PRIVATE
      $<$<CXX_COMPILER_ID:GNU>:-fmax-errors=2>
      $<$<CXX_COMPILER_ID:Clang>:-ferror-limit=2>
      $<$<CXX_COMPILER_ID:AppleClang>:-ferror-limit=2>
    )
  endif()
endfunction()

function(ava_apply_sanitizers target)
  if(AVA_ENABLE_SANITIZERS AND NOT MSVC)
    target_compile_options(${target} PRIVATE -fsanitize=address,undefined -fno-omit-frame-pointer)
    target_link_options(${target} PRIVATE -fsanitize=address,undefined)
  endif()
endfunction()

#==============================================================================
# CONFIG FILES

# Tell cwds/sys.h that we have a config.h.
#add_definitions(-DHAVE_CONFIG_H)
#include_directories(${top_objdir})
# End of CONFIG FILES section.

#==============================================================================

# Declare aicxx submodules.
include(AICxxSubmodules)

add_subdirectory(src)

if(AVA_BUILD_TESTS)
  enable_testing()
  add_subdirectory(tests)
endif()
