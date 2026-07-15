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

FetchContent_Declare(
  gitache
  GIT_REPOSITORY "https://github.com/CarloWood/gitache.git"
  GIT_TAG        "c765a7199d8c789fe8d59af69a18a8eb08c5b945"
  UPDATE_DISCONNECTED TRUE
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

# Keep the long-standing AVA_* command-line variables as compatibility inputs.
# An explicitly set cw_option value wins; otherwise it follows the legacy cache
# variable so existing presets, CI jobs, and documented invocations still work.
option(AVA_BUILD_TESTS "Build AVA tests (compatibility alias for EnableAvaBuildTests)" ON)
option(AVA_ENABLE_SANITIZERS "Build AVA with sanitizers (compatibility alias for EnableAvaSanitizers)" OFF)
cw_option(EnableAvaBuildTests
    "Build AVA tests" "${AVA_BUILD_TESTS}"
    "" OFF)
cw_option(EnableAvaSanitizers
    "Build AVA with AddressSanitizer and UndefinedBehaviorSanitizer" "${AVA_ENABLE_SANITIZERS}"
    "" OFF)

# AVA_BUILD_DESKTOP_QML gates the experimental Qt Quick/QML desktop prototype
# under src/ava/desktop (added upstream). It is declared with plain option()
# because src/CMakeLists.txt references the AVA_BUILD_DESKTOP_QML variable name
# directly, unlike the cw_option-based toggles above.
option(AVA_BUILD_DESKTOP_QML "Build the experimental Qt Quick/QML desktop prototype" OFF)
option(AVA_ENABLE_TSAN "Build AVA with ThreadSanitizer (focused coordinator tests only)" OFF)
if(OptionEnableAvaSanitizers AND AVA_ENABLE_TSAN)
  message(FATAL_ERROR "EnableAvaSanitizers/AVA_ENABLE_SANITIZERS and AVA_ENABLE_TSAN are mutually exclusive")
endif()

# Default maximum string length for debug output truncation. When no maxlen()
# IO manipulator is active on the debug stream, strings longer than this are
# truncated in debug output. 0 means no limit. The value ends up in the
# generated ava/core/config.h as config::ava_debug_maxlen_c.
#
# This is a numeric config value, not a boolean, so it uses set(CACHE STRING)
# rather than cw_option (which is ON/OFF only: a value like 50 is neither "ON"
# nor "OFF" and would be silently replaced by the default).
set(AVA_DEBUG_MAXLEN 100 CACHE STRING "Default maximum string length for debug output truncation (0 = no limit).")
if(AVA_DEBUG_MAXLEN EQUAL 100)
  set(_ava_debug_maxlen_color "${OptionColorYay}")
  set(_ava_debug_maxlen_info " (default)")
else()
  set(_ava_debug_maxlen_color "${OptionColorAlert}")
  set(_ava_debug_maxlen_info "")
endif()
message(STATUS "${Option} ${OptionColor}AVA_DEBUG_MAXLEN${ColourReset} (Default maximum string length for debug output truncation) =\n\t${_ava_debug_maxlen_color}${AVA_DEBUG_MAXLEN}${ColourReset}${_ava_debug_maxlen_info}")
unset(_ava_debug_maxlen_color)
unset(_ava_debug_maxlen_info)

# ACP keeps JSON implementation details behind its codec. Prefer an installed
# package so configured offline environments do not need the network; the
# fallback is an immutable official release artifact.
find_package(nlohmann_json 3.11 CONFIG QUIET)
if(NOT TARGET nlohmann_json::nlohmann_json)
  set(JSON_BuildTests OFF CACHE INTERNAL "" FORCE)
  set(JSON_Install OFF CACHE INTERNAL "" FORCE)
  FetchContent_Declare(
    ava_nlohmann_json
    URL "https://github.com/nlohmann/json/releases/download/v3.11.3/json.tar.xz"
    URL_HASH "SHA256=d6c65aca6b1ed68e7a182f4757257b107ae403032760ed6ef121c9d55e81757d"
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE
  )
  FetchContent_MakeAvailable(ava_nlohmann_json)
endif()

function(ava_apply_debugging target)
  target_link_libraries(${target} PRIVATE ${AICXX_OBJECTS_LIST})
endfunction()

function(ava_apply_warnings target)
  if(MSVC)
    target_compile_options(${target} PRIVATE /W4 /WX)
  else()
    target_compile_options(${target} PRIVATE -Wall -Werror -Wextra -Wpedantic -Wundef)
    if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU" AND CMAKE_CXX_COMPILER_VERSION VERSION_LESS 17)
      # Keep GCC's optimizer-driven maybe-uninitialized diagnostics visible, but
      # do not promote them to errors for GCC 13.x through 16.x. Those releases can report false
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
  if(OptionEnableAvaSanitizers AND NOT MSVC)
    target_compile_options(${target} PRIVATE -fsanitize=address,undefined -fno-omit-frame-pointer)
    target_link_options(${target} PRIVATE -fsanitize=address,undefined)
  endif()
endfunction()

#==============================================================================
# CONFIG FILES

# Generate config.h from config.h.in.
configure_file(
  "${CMAKE_SOURCE_DIR}/config.h.in"
  "${CMAKE_BINARY_DIR}/config.h"
  @ONLY
)

# Tell cwds/sys.h that we have a config.h.
add_definitions(-DHAVE_CONFIG_H)
include_directories(${top_objdir})

# End of CONFIG FILES section.

#==============================================================================

# ThreadSanitizer must instrument every project-built object that can be linked
# into an instrumented executable. Applying it only to AVA's final targets can
# select uninstrumented weak libstdc++ template definitions from AICxx object
# libraries and hide real synchronization from TSan.
if(AVA_ENABLE_TSAN AND NOT MSVC)
  add_compile_options(-fsanitize=thread -fno-omit-frame-pointer)
  add_link_options(-fsanitize=thread)
endif()

# Declare aicxx submodules.
include(AICxxSubmodules)
set(AICXX_OBJECTS_LIST ${AICXX_OBJECTS_LIST} enchantum::enchantum CACHE INTERNAL "List of OBJECT libaries that this project uses.")

add_subdirectory(src)
add_subdirectory(enchantum)

if(OptionEnableAvaBuildTests)
  enable_testing()
  add_subdirectory(tests)
endif()
