function(ava_apply_warnings target_name)
  if(NOT TARGET ${target_name})
    message(FATAL_ERROR "ava_apply_warnings called with unknown target: ${target_name}")
  endif()

  if(MSVC)
    target_compile_options(${target_name} PRIVATE /W4 /permissive-)
    if(AVA_ENABLE_WARNINGS_AS_ERRORS)
      target_compile_options(${target_name} PRIVATE /WX)
    endif()
  else()
    target_compile_options(${target_name} PRIVATE
      -Wall
      -Wextra
      -Wpedantic
      -Wconversion
      -Wsign-conversion
      # GCC warns on intentionally partial C++20 designated initializers.
      -Wno-missing-field-initializers
    )
    if(AVA_ENABLE_WARNINGS_AS_ERRORS)
      target_compile_options(${target_name} PRIVATE -Werror)
    endif()
  endif()
endfunction()
