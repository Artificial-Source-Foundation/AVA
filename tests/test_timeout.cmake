# Select a runtime timeout for a CMake test driver.
#
# The authored timeout is retained normally. AVA_DEBUG_NO_TIMEOUT stretches it
# to one hour, while a positive integral AVA_DEBUG_NO_TIMEOUT_SECONDS overrides
# that default. Invalid overrides deliberately use the one-hour fallback.
function(ava_test_timeout OUTPUT_VARIABLE AUTHORED_SECONDS)
  set(EFFECTIVE_SECONDS "${AUTHORED_SECONDS}")
  if(DEFINED ENV{AVA_DEBUG_NO_TIMEOUT})
    set(EFFECTIVE_SECONDS 3600)
    if(DEFINED ENV{AVA_DEBUG_NO_TIMEOUT_SECONDS} AND "$ENV{AVA_DEBUG_NO_TIMEOUT_SECONDS}" MATCHES "^[1-9][0-9]*$")
      set(EFFECTIVE_SECONDS "$ENV{AVA_DEBUG_NO_TIMEOUT_SECONDS}")
    endif()
  endif()
  set("${OUTPUT_VARIABLE}" "${EFFECTIVE_SECONDS}" PARENT_SCOPE)
endfunction()
