function(ava_detect_ctags_json ctags_executable result_variable)
  set(_has_json FALSE)

  execute_process(
    COMMAND "${ctags_executable}" --list-output-formats
    RESULT_VARIABLE _formats_result
    OUTPUT_VARIABLE _formats_output
    ERROR_QUIET
    OUTPUT_STRIP_TRAILING_WHITESPACE
  )
  if (_formats_result EQUAL 0)
    string(REPLACE ";" "\\;" _formats_output "${_formats_output}")
    string(REGEX REPLACE "\r?\n" ";" _format_lines "${_formats_output}")
    foreach(_line ${_format_lines})
      string(REGEX REPLACE "^[ \t]+" "" _line "${_line}")
      if (_line STREQUAL "" OR _line MATCHES "^#")
        continue()
      endif ()
      string(REGEX REPLACE "[ \t]+" ";" _tokens "${_line}")
      list(FILTER _tokens EXCLUDE REGEX "^$")
      list(LENGTH _tokens _ntokens)
      if (_ntokens GREATER_EQUAL 3)
        list(GET _tokens 0 _format_name)
        list(GET _tokens 2 _format_available)
        if (_format_name STREQUAL "json" AND _format_available STREQUAL "yes")
          set(_has_json TRUE)
          break()
        endif ()
      endif ()
    endforeach()
  endif ()

  # Universal Ctags versions shipped by Ubuntu 24.04 predate
  # --list-output-formats, but advertise JSON through --list-features.
  if (NOT _has_json)
    execute_process(
      COMMAND "${ctags_executable}" --list-features
      RESULT_VARIABLE _features_result
      OUTPUT_VARIABLE _features_output
      ERROR_QUIET
      OUTPUT_STRIP_TRAILING_WHITESPACE
    )
    if (_features_result EQUAL 0)
      string(REPLACE ";" "\\;" _features_output "${_features_output}")
      string(REGEX REPLACE "\r?\n" ";" _feature_lines "${_features_output}")
      foreach(_line ${_feature_lines})
        string(REGEX REPLACE "^[ \t]+" "" _line "${_line}")
        if (_line MATCHES "^json([ \t]|$)")
          set(_has_json TRUE)
          break()
        endif ()
      endforeach()
    endif ()
  endif ()

  set(${result_variable} "${_has_json}" PARENT_SCOPE)
endfunction()
