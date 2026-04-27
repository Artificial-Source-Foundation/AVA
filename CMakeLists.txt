cmake_minimum_required(VERSION 3.25)

project(ava VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(AVA_BUILD_TESTS "Build AVA tests" ON)
option(AVA_ENABLE_SANITIZERS "Build AVA with AddressSanitizer and UndefinedBehaviorSanitizer" OFF)

function(ava_apply_warnings target)
  if(MSVC)
    target_compile_options(${target} PRIVATE /W4 /WX)
  else()
    target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic -Werror)
  endif()
endfunction()

function(ava_apply_sanitizers target)
  if(AVA_ENABLE_SANITIZERS AND NOT MSVC)
    target_compile_options(${target} PRIVATE -fsanitize=address,undefined -fno-omit-frame-pointer)
    target_link_options(${target} PRIVATE -fsanitize=address,undefined)
  endif()
endfunction()

add_library(ava_core
  src/ava/agent/agent_loop.cpp
  src/ava/agent/mode.cpp
  src/ava/agent/tool_dispatcher.cpp
  src/ava/config/auth.cpp
  src/ava/config/model_config.cpp
  src/ava/config/prompt_config.cpp
  src/ava/config/xdg_paths.cpp
  src/ava/core/error.cpp
  src/ava/core/ids.cpp
  src/ava/core/json.cpp
  src/ava/permissions/permission.cpp
  src/ava/provider/curl_transport.cpp
  src/ava/provider/openai_provider.cpp
  src/ava/provider/provider.cpp
  src/ava/session/session_store.cpp
  src/ava/tools/bash_tool.cpp
  src/ava/tools/file_tools.cpp
  src/ava/tools/search_tools.cpp
  src/ava/tui/composer.cpp
  src/ava/tui/terminal.cpp
)

target_include_directories(ava_core PUBLIC src)
ava_apply_warnings(ava_core)
ava_apply_sanitizers(ava_core)

add_executable(ava src/main.cpp)
target_link_libraries(ava PRIVATE ava_core)
ava_apply_warnings(ava)
ava_apply_sanitizers(ava)

if(AVA_BUILD_TESTS)
  enable_testing()
  add_executable(ava_tests
    tests/core_tests.cpp
  )
  target_link_libraries(ava_tests PRIVATE ava_core)
  ava_apply_warnings(ava_tests)
  ava_apply_sanitizers(ava_tests)
  add_test(NAME ava_tests COMMAND ava_tests)
endif()
