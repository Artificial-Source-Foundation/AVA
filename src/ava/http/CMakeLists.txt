add_library(ava_http
  "curl_transport.cpp"
  "transport.cpp"
)

target_include_directories(ava_http PUBLIC "${CMAKE_SOURCE_DIR}/src")
target_link_libraries(ava_http PUBLIC ava_core ava_observability)
ava_apply_debugging(ava_http)
ava_apply_warnings(ava_http)
ava_apply_sanitizers(ava_http)
