add_library(ava_agent
  "agent_loop.cpp"
  "agent_loop_session.cpp"
  "assistant_turn.cpp"
  "message_builder.cpp"
  "mode.cpp"
  "provider_output_validation.cpp"
  "question.cpp"
  "stream_bridge.cpp"
  "subagent_config.cpp"
  "tool_dispatch_common.cpp"
  "tool_dispatch_patch.cpp"
  "tool_dispatcher.cpp"
  "tool_registry.cpp"
  "tool_result.cpp"
  "tool_summaries.cpp"
  "tool_timeline.cpp"
  "usage_accounting.cpp"
)

target_include_directories(ava_agent PUBLIC "${CMAKE_SOURCE_DIR}/src")
ava_apply_warnings(ava_agent)
ava_apply_sanitizers(ava_agent)
