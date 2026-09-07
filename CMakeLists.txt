cmake_minimum_required(VERSION 3.27...4.3.4)

project(ava
  VERSION 1.0.0
  LANGUAGES CXX
  DESCRIPTION "OpenAI-first C++23 terminal coding agent with an ncursesw TUI"
)

include(GNUInstallDirs)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# On macOS, Homebrew installs Boost/CMake packages under its prefix and keeps
# ncurses keg-only under <prefix>/opt/ncurses. Extend the default prefix path
# so FindCurses (ncursesw) and Boost work without manual -DCMAKE_PREFIX_PATH.
# This appends only well-known Homebrew locations; explicit user-provided
# CMAKE_PREFIX_PATH entries are preserved.
if(CMAKE_HOST_SYSTEM_NAME STREQUAL "Darwin")
  foreach(_ava_brew_prefix IN ITEMS /opt/homebrew /usr/local)
    if(EXISTS "${_ava_brew_prefix}/opt/ncurses" AND NOT "${_ava_brew_prefix}/opt/ncurses" IN_LIST CMAKE_PREFIX_PATH)
      list(APPEND CMAKE_PREFIX_PATH "${_ava_brew_prefix}/opt/ncurses")
    endif()
  endforeach()
  unset(_ava_brew_prefix)
  if(DEFINED ENV{HOMEBREW_PREFIX} AND NOT "$ENV{HOMEBREW_PREFIX}" STREQUAL "")
    if(NOT "$ENV{HOMEBREW_PREFIX}" IN_LIST CMAKE_PREFIX_PATH)
      list(APPEND CMAKE_PREFIX_PATH "$ENV{HOMEBREW_PREFIX}")
    endif()
  else()
    foreach(_ava_brew_prefix IN ITEMS /opt/homebrew /usr/local)
      if(EXISTS "${_ava_brew_prefix}" AND NOT "${_ava_brew_prefix}" IN_LIST CMAKE_PREFIX_PATH)
        list(APPEND CMAKE_PREFIX_PATH "${_ava_brew_prefix}")
      endif()
    endforeach()
    unset(_ava_brew_prefix)
  endif()
  # The pinned aicxx submodules (ai-utils etc.) use header-only Boost via plain
  # #include <boost/...> without linking an imported Boost target. Apple Clang
  # does not search Homebrew prefixes by default, so add them as SYSTEM includes
  # (warning-free under -Werror) plus library search paths on macOS only.
  foreach(_ava_brew_prefix IN ITEMS /opt/homebrew /usr/local)
    if(EXISTS "${_ava_brew_prefix}/include/boost/version.hpp")
      include_directories(SYSTEM "${_ava_brew_prefix}/include")
    elseif(EXISTS "${_ava_brew_prefix}/include")
      include_directories(SYSTEM "${_ava_brew_prefix}/include")
    endif()
    if(EXISTS "${_ava_brew_prefix}/lib")
      link_directories("${_ava_brew_prefix}/lib")
    endif()
  endforeach()
  unset(_ava_brew_prefix)
endif()

# The CommandLineTools libc++ still ships <stop_token>/std::jthread as an
# experimental feature; enable the vendor-sanctioned experimental library on
# Apple builds so standard C++20 threading primitives are available.
if(CMAKE_HOST_SYSTEM_NAME STREQUAL "Darwin" OR CMAKE_SYSTEM_NAME STREQUAL "Darwin")
  add_compile_options(-fexperimental-library)
  add_link_options(-fexperimental-library)
endif()

# Force-include the macOS compatibility shim on Apple builds. It only defines
# symbols missing from macOS/BSD libc (SIGRTMIN/SIGRTMAX, SIGSTKFLT, SIGPOLL,
# SIGPWR, sigorset) needed by the pinned Linux-centric submodules, and is a
# no-op everywhere else. An absolute path keeps it working regardless of the
# including file's directory.
if(CMAKE_HOST_SYSTEM_NAME STREQUAL "Darwin" OR CMAKE_SYSTEM_NAME STREQUAL "Darwin")
  add_compile_options(-include "${CMAKE_CURRENT_SOURCE_DIR}/cmake/macos-compat.h")
endif()

#==============================================================================
# Begin of gitache configuration.

include(FetchContent)
option(AVA_ENABLE_GITACHE "Fetch and enable gitache-managed developer dependencies" ON)
if(AVA_ENABLE_GITACHE)
  # libcwd requires libdw/libelf (elfutils) for location lookup and does not
  # build on macOS/Mach-O targets. Skip the gitache-managed libcwd there; the
  # aicxx modules then default EnableLibcwd to OFF while EnableDebug stays usable.
  if(CMAKE_HOST_SYSTEM_NAME STREQUAL "Darwin" OR CMAKE_SYSTEM_NAME STREQUAL "Darwin")
    set(GITACHE_PACKAGES "")  # List of requested packages.
    message(STATUS "gitache libcwd skipped on macOS (libdw unavailable); EnableLibcwd defaults to OFF")
  else()
    set(GITACHE_PACKAGES libcwd)  # List of requested packages.
  endif()

  if(GITACHE_PACKAGES STREQUAL "")
    message(STATUS "gitache disabled: no packages requested for this platform")
  else()
    FetchContent_Declare(
      gitache
      GIT_REPOSITORY "https://github.com/CarloWood/gitache.git"
      GIT_TAG        "c765a7199d8c789fe8d59af69a18a8eb08c5b945"
      UPDATE_DISCONNECTED TRUE
    )
    FetchContent_MakeAvailable(gitache)
  endif()
else()
  message(STATUS "gitache disabled by AVA_ENABLE_GITACHE=OFF")
endif()
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
  # The initialized, pinned gitlink is the release source dependency. Keep the
  # immutable upstream archive only as the existing non-qualified fallback for
  # incomplete developer checkouts; provenance rejects a missing gitlink.
  if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/src/json/CMakeLists.txt")
    add_subdirectory(src/json EXCLUDE_FROM_ALL)
  else()
    FetchContent_Declare(
      ava_nlohmann_json
      URL "https://github.com/nlohmann/json/releases/download/v3.11.3/json.tar.xz"
      URL_HASH "SHA256=d6c65aca6b1ed68e7a182f4757257b107ae403032760ed6ef121c9d55e81757d"
      DOWNLOAD_EXTRACT_TIMESTAMP TRUE
    )
    FetchContent_MakeAvailable(ava_nlohmann_json)
  endif()
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
    if (NOT "${CMAKE_CXX_FLAGS}" MATCHES "-f(error-limit|max-errors)=")
      # Limit number of errors to five (if this option isn't already set in CXXFLAGS).
      target_compile_options(${target} PRIVATE
        $<$<CXX_COMPILER_ID:GNU>:-fmax-errors=5>
        $<$<CXX_COMPILER_ID:Clang>:-ferror-limit=5>
        $<$<CXX_COMPILER_ID:AppleClang>:-ferror-limit=5>
      )
    endif()
  endif()
endfunction()

function(ava_apply_sanitizers target)
  if(OptionEnableAvaSanitizers AND NOT MSVC)
    target_compile_options(${target} PRIVATE -fsanitize=address,undefined -fno-sanitize-recover=undefined -fno-omit-frame-pointer)
    target_link_options(${target} PUBLIC -fsanitize=address,undefined -fno-sanitize-recover=undefined)
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

# Tell cwds/sys.h that we have a config.h. Reject the two guarded non-MIT
# ai-utils headers if a future AVA source accidentally includes either one.
add_definitions(-DHAVE_CONFIG_H)
add_compile_definitions(MIT_LICENSE_ONLY)
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

# ai-utils probes x86 BMI2 by compiling and running GCC x86 builtins. Those
# builtins do not exist on non-x86 targets, where a failed probe must mean
# "unsupported" rather than aborting configuration.
string(TOLOWER "${CMAKE_SYSTEM_PROCESSOR}" _ava_system_processor)
if(NOT _ava_system_processor MATCHES "^(x86_64|amd64|i[3-6]86)$")
  set(CW_BMI2_SUPPORT 0 CACHE INTERNAL "BMI2 is unavailable on non-x86 targets" FORCE)
endif()
unset(_ava_system_processor)

# Declare aicxx submodules.
include(AICxxSubmodules)

# AppleClang's -Wshift-count-overflow fires on the pinned ai-utils
# `nearest_power_of_two` template instantiations that GCC 13/16 accept
# without warning. Keep the warning visible but non-fatal on Apple builds
# only; this touches pinned submodule code, not AVA sources.
if((CMAKE_HOST_SYSTEM_NAME STREQUAL "Darwin" OR CMAKE_SYSTEM_NAME STREQUAL "Darwin") AND TARGET utils_ObjLib)
  target_compile_options(utils_ObjLib PRIVATE
    $<$<CXX_COMPILER_ID:AppleClang>:-Wno-error=shift-count-overflow>)
endif()

# libcwd publishes libdw/libelf as interface dependencies rather than recording
# them as NEEDED entries. Linux toolchains that default to --as-needed can then
# discard both libraries and leave debug executables with unresolved symbols.
if(OptionEnableLibcwd AND CMAKE_SYSTEM_NAME STREQUAL "Linux")
  target_link_options(cwds_ObjLib INTERFACE "LINKER:--no-as-needed")
endif()

set(AICXX_OBJECTS_LIST ${AICXX_OBJECTS_LIST} enchantum::enchantum CACHE INTERNAL "List of OBJECT libaries that this project uses.")

add_subdirectory(src)
add_subdirectory(enchantum)

install(
  FILES "${CMAKE_CURRENT_SOURCE_DIR}/docs/operations/release-artifact-readme.md"
  DESTINATION "${CMAKE_INSTALL_DOCDIR}"
  RENAME README.md
  COMPONENT ava
)
install(
  FILES
    "${CMAKE_CURRENT_SOURCE_DIR}/LICENSE"
    "${CMAKE_CURRENT_SOURCE_DIR}/THIRD_PARTY_NOTICES.md"
  DESTINATION "${CMAKE_INSTALL_DOCDIR}"
  COMPONENT ava
)
install(
  FILES
    "${CMAKE_CURRENT_SOURCE_DIR}/docs/core/usage.md"
    "${CMAKE_CURRENT_SOURCE_DIR}/docs/core/configuration.md"
    "${CMAKE_CURRENT_SOURCE_DIR}/docs/core/context-resources.md"
    "${CMAKE_CURRENT_SOURCE_DIR}/docs/core/custom-providers.md"
    "${CMAKE_CURRENT_SOURCE_DIR}/docs/core/environment-variables.md"
    "${CMAKE_CURRENT_SOURCE_DIR}/docs/core/providers.md"
    "${CMAKE_CURRENT_SOURCE_DIR}/docs/core/subagents.md"
    "${CMAKE_CURRENT_SOURCE_DIR}/docs/core/thinking-modes.md"
    "${CMAKE_CURRENT_SOURCE_DIR}/docs/core/tools.md"
  DESTINATION "${CMAKE_INSTALL_DOCDIR}/docs/core"
  COMPONENT ava
)
install(
  FILES "${CMAKE_CURRENT_SOURCE_DIR}/docs/interfaces/themes-keybindings.md"
  DESTINATION "${CMAKE_INSTALL_DOCDIR}/docs/interfaces"
  COMPONENT ava
)
install(
  FILES
    "${CMAKE_CURRENT_SOURCE_DIR}/docs/operations/testing.md"
    "${CMAKE_CURRENT_SOURCE_DIR}/docs/operations/terminal-setup.md"
    "${CMAKE_CURRENT_SOURCE_DIR}/docs/operations/troubleshooting.md"
    "${CMAKE_CURRENT_SOURCE_DIR}/docs/operations/diagnostics.md"
    "${CMAKE_CURRENT_SOURCE_DIR}/docs/operations/release-checklist.md"
  DESTINATION "${CMAKE_INSTALL_DOCDIR}/docs/operations"
  COMPONENT ava
)
install(
  FILES
    "${CMAKE_CURRENT_SOURCE_DIR}/docs/extensions/lsp.md"
    "${CMAKE_CURRENT_SOURCE_DIR}/docs/extensions/mcp.md"
    "${CMAKE_CURRENT_SOURCE_DIR}/docs/extensions/plugin-system.md"
  DESTINATION "${CMAKE_INSTALL_DOCDIR}/docs/extensions"
  COMPONENT ava
)
install(
  FILES
    "${CMAKE_CURRENT_SOURCE_DIR}/docs/security/sandboxing.md"
    "${CMAKE_CURRENT_SOURCE_DIR}/docs/security/containment.md"
  DESTINATION "${CMAKE_INSTALL_DOCDIR}/docs/security"
  COMPONENT ava
)
install(
  FILES
    "${CMAKE_CURRENT_SOURCE_DIR}/docs/development/session-versioning.md"
    "${CMAKE_CURRENT_SOURCE_DIR}/docs/development/side-effect-safety-checklist.md"
  DESTINATION "${CMAKE_INSTALL_DOCDIR}/docs/development"
  COMPONENT ava
)
install(
  FILES
    "${CMAKE_CURRENT_SOURCE_DIR}/docs/headless-protocol.md"
    "${CMAKE_CURRENT_SOURCE_DIR}/docs/rpc-protocol.md"
    "${CMAKE_CURRENT_SOURCE_DIR}/docs/acp.md"
    "${CMAKE_CURRENT_SOURCE_DIR}/docs/acp-support.json"
    "${CMAKE_CURRENT_SOURCE_DIR}/docs/session-format.md"
    "${CMAKE_CURRENT_SOURCE_DIR}/docs/plugin-compatibility-policy.md"
  DESTINATION "${CMAKE_INSTALL_DOCDIR}/docs"
  COMPONENT ava
)
install(
  FILES
    "${CMAKE_CURRENT_SOURCE_DIR}/docs/interop/evidence/README.md"
    "${CMAKE_CURRENT_SOURCE_DIR}/docs/interop/evidence/zed-1.9.0-2026-07-14.md"
  DESTINATION "${CMAKE_INSTALL_DOCDIR}/docs/interop/evidence"
  COMPONENT ava
)
install(
  FILES "${CMAKE_CURRENT_SOURCE_DIR}/docs/product/mvp-coverage-ledger.md"
  DESTINATION "${CMAKE_INSTALL_DOCDIR}/docs/product"
  COMPONENT ava
)
install(
  FILES "${CMAKE_CURRENT_SOURCE_DIR}/docs/schema/theme.schema.json"
  DESTINATION "${CMAKE_INSTALL_DOCDIR}/docs/schema"
  COMPONENT ava
)

if(OptionEnableAvaBuildTests)
  enable_testing()
  add_subdirectory(tests)
endif()
