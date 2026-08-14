add_library(ava_provider
  "anthropic_provider.cpp"
  "anthropic_request.cpp"
  "catalog.cpp"
  "gemini_provider.cpp"
  "openai_compatible_provider.cpp"
  "openai_compatible_request.cpp"
  "openai_provider.cpp"
  "openai_request.cpp"
  "openai_response_parser.cpp"
  "openai_stream_events.cpp"
  "openai_stream_function_events.cpp"
  "openai_stream_message_events.cpp"
  "openai_stream_output_item_events.cpp"
  "openai_stream_parser.cpp"
  "openai_stream_parser_support.cpp"
  "openai_stream_reasoning_events.cpp"
  "openai_stream_terminal_events.cpp"
  "provider.cpp"
  "provider_utils.cpp"
  "registry.cpp"
)

target_include_directories(ava_provider
  PUBLIC
    "${CMAKE_SOURCE_DIR}/src"
)

target_link_libraries(ava_provider
  PUBLIC
    AVA::http
    AVA::config
)

ava_apply_debugging(ava_provider)
ava_apply_warnings(ava_provider)
ava_apply_sanitizers(ava_provider)

# Create an ALIAS target.
add_library(AVA::provider ALIAS ava_provider)
