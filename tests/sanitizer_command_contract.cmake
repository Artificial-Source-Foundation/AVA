if(NOT DEFINED AVA_BUILD_TOOL OR NOT DEFINED AVA_BUILD_DIR)
  message(FATAL_ERROR "Sanitizer command contract requires AVA_BUILD_TOOL and AVA_BUILD_DIR")
endif()

execute_process(
  COMMAND "${AVA_BUILD_TOOL}" -C "${AVA_BUILD_DIR}" -t commands ava_terminal
  RESULT_VARIABLE terminal_result
  OUTPUT_VARIABLE terminal_commands
  ERROR_VARIABLE terminal_error
)
if(NOT terminal_result EQUAL 0)
  message(FATAL_ERROR "Could not inspect ava_terminal build commands: ${terminal_error}")
endif()

set(terminal_compile_command "")
string(REPLACE "\n" ";" terminal_command_lines "${terminal_commands}")
foreach(command IN LISTS terminal_command_lines)
  if(command MATCHES "BasicWindow[.]cxx")
    set(terminal_compile_command "${command}")
    break()
  endif()
endforeach()
if(terminal_compile_command STREQUAL "")
  message(FATAL_ERROR "Could not find the representative ava_terminal compile command")
endif()

foreach(required_flag IN ITEMS
    "-fsanitize=address,undefined"
    "-fno-sanitize-recover=undefined"
    "-fno-omit-frame-pointer")
  string(FIND "${terminal_compile_command}" "${required_flag}" flag_index)
  if(flag_index EQUAL -1)
    message(FATAL_ERROR "ava_terminal compile command is missing ${required_flag}: ${terminal_compile_command}")
  endif()
endforeach()

execute_process(
  COMMAND "${AVA_BUILD_TOOL}" -C "${AVA_BUILD_DIR}" -t commands ava_sanitizer_static_consumer
  RESULT_VARIABLE consumer_result
  OUTPUT_VARIABLE consumer_commands
  ERROR_VARIABLE consumer_error
)
if(NOT consumer_result EQUAL 0)
  message(FATAL_ERROR "Could not inspect the sanitizer static consumer build commands: ${consumer_error}")
endif()

set(consumer_link_command "")
string(REPLACE "\n" ";" consumer_command_lines "${consumer_commands}")
foreach(command IN LISTS consumer_command_lines)
  if(command MATCHES "(^| )-o +([^ ]*/)?ava_sanitizer_static_consumer( |$)")
    set(consumer_link_command "${command}")
    break()
  endif()
endforeach()
if(consumer_link_command STREQUAL "")
  message(FATAL_ERROR "Could not find the sanitizer static consumer link command")
endif()

foreach(required_flag IN ITEMS
    "-fsanitize=address,undefined"
    "-fno-sanitize-recover=undefined")
  string(FIND "${consumer_link_command}" "${required_flag}" flag_index)
  if(flag_index EQUAL -1)
    message(FATAL_ERROR "Sanitizer static consumer link command is missing ${required_flag}: ${consumer_link_command}")
  endif()
endforeach()
