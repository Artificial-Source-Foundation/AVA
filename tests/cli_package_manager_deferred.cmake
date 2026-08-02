if(NOT DEFINED AVA_EXE)
  message(FATAL_ERROR "AVA_EXE is required")
endif()

execute_process(
  COMMAND "${AVA_EXE}" packages list
  OUTPUT_VARIABLE PACKAGE_OUTPUT
  ERROR_VARIABLE PACKAGE_ERROR
  RESULT_VARIABLE PACKAGE_RESULT
  TIMEOUT 10
)

if(NOT PACKAGE_RESULT EQUAL 0)
  message(FATAL_ERROR "ava packages list exited with ${PACKAGE_RESULT}\nstdout:\n${PACKAGE_OUTPUT}\nstderr:\n${PACKAGE_ERROR}")
endif()

foreach(NEEDLE "package manager is deferred" "provenance" "docs/extensions/plugin-system.md")
  string(FIND "${PACKAGE_OUTPUT}" "${NEEDLE}" NEEDLE_INDEX)
  if(NEEDLE_INDEX EQUAL -1)
    message(FATAL_ERROR "ava packages list output did not contain ${NEEDLE}\nstdout:\n${PACKAGE_OUTPUT}\nstderr:\n${PACKAGE_ERROR}")
  endif()
endforeach()
