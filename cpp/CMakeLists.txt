cmake_minimum_required(VERSION 3.28)

project(ava_cpp VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(AVA_BUILD_TESTS "Build C++ workspace tests" ON)
option(AVA_BUILD_SMOKE "Build C++ smoke executable" ON)
option(AVA_ENABLE_WARNINGS_AS_ERRORS "Treat warnings as errors" OFF)
option(AVA_ENABLE_SANITIZERS "Enable address/undefined sanitizers" OFF)
option(AVA_ENABLE_LTO "Enable link time optimization" OFF)
option(AVA_WITH_FTXUI "Enable FTXUI-backed ava_tui executable" OFF)
option(AVA_WITH_CPR "Enable optional CPR integration placeholders" OFF)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")

include(Dependencies)
include(Warnings)
include(Sanitizers)
include(ToolchainHygiene)

if(AVA_ENABLE_LTO)
  include(CheckIPOSupported)
  check_ipo_supported(RESULT ava_ipo_supported OUTPUT ava_ipo_error)
  if(ava_ipo_supported)
    set(CMAKE_INTERPROCEDURAL_OPTIMIZATION ON)
  else()
    message(WARNING "AVA_ENABLE_LTO requested but not supported: ${ava_ipo_error}")
  endif()
endif()

set(AVA_GENERATED_INCLUDE_DIR "${CMAKE_CURRENT_BINARY_DIR}/generated")
file(MAKE_DIRECTORY "${AVA_GENERATED_INCLUDE_DIR}/ava/core")
set(AVA_CPP_INCLUDE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/include")

if(CMAKE_CONFIGURATION_TYPES)
  set(AVA_BUILD_TYPE_VALUE "multi-config")
elseif(CMAKE_BUILD_TYPE)
  set(AVA_BUILD_TYPE_VALUE "${CMAKE_BUILD_TYPE}")
else()
  set(AVA_BUILD_TYPE_VALUE "unknown")
endif()

configure_file(
  "${CMAKE_CURRENT_SOURCE_DIR}/include/ava/core/version.hpp.in"
  "${AVA_GENERATED_INCLUDE_DIR}/ava/core/version.hpp"
  @ONLY
)

configure_file(
  "${CMAKE_CURRENT_SOURCE_DIR}/include/ava/core/build_config.hpp.in"
  "${AVA_GENERATED_INCLUDE_DIR}/ava/core/build_config.hpp"
  @ONLY
)

add_subdirectory(src)
add_subdirectory(apps)

if(AVA_BUILD_TESTS)
  enable_testing()
  add_subdirectory(tests)
endif()
