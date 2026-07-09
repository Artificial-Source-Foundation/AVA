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

cw_option(EnableAvaBuildTests
    "Build AVA tests" ON
    "" OFF)
cw_option(EnableAvaSanitizers
    "Build AVA with AddressSanitizer and UndefinedBehaviorSanitizer" OFF
    "" OFF)

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
  if(OptionEnableAvaSanitizers AND NOT MSVC)
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
set(AICXX_OBJECTS_LIST ${AICXX_OBJECTS_LIST} enchantum::enchantum CACHE INTERNAL "List of OBJECT libaries that this project uses.")

add_subdirectory(src)
add_subdirectory(enchantum)

if(OptionEnableAvaBuildTests)
  enable_testing()
  add_subdirectory(tests)
endif()
