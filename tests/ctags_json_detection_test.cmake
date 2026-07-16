include("${AVA_SOURCE_DIR}/cmake/DetectCtagsJson.cmake")

file(REMOVE_RECURSE "${AVA_TEST_ROOT}")
file(MAKE_DIRECTORY "${AVA_TEST_ROOT}")

function(check_ctags name body expected)
  set(_ctags "${AVA_TEST_ROOT}/${name}")
  file(WRITE "${_ctags}" "#!/bin/sh\n${body}\n")
  file(CHMOD "${_ctags}"
       PERMISSIONS OWNER_READ OWNER_WRITE OWNER_EXECUTE GROUP_READ GROUP_EXECUTE WORLD_READ WORLD_EXECUTE)

  ava_detect_ctags_json("${_ctags}" _detected)
  if (NOT _detected STREQUAL expected)
    message(FATAL_ERROR "${name}: expected JSON detection to be ${expected}, got ${_detected}")
  endif ()
endfunction()

check_ctags(
  modern-ctags
  "if [ \"$1\" = --list-output-formats ]; then
  printf '#NAME DEFAULT AVAILABLE\\nu-ctags yes yes\\njson no yes\\n'
  exit 0
fi
exit 1"
  TRUE)

check_ctags(
  ubuntu-ctags
  "if [ \"$1\" = --list-output-formats ]; then
  exit 1
fi
if [ \"$1\" = --list-features ]; then
  printf '#NAME DESCRIPTION\\njson supports json format output\\nregex supports regex\\n'
  exit 0
fi
exit 1"
  TRUE)

check_ctags(
  no-json-ctags
  "if [ \"$1\" = --list-features ]; then
  printf '#NAME DESCRIPTION\\nregex supports regex\\n'
  exit 0
fi
exit 1"
  FALSE)
