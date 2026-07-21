add_library(ava_agent
  "agent_loop.cpp"
  "agent_loop_session.cpp"
  "assistant_turn.cpp"
  "background_job_registry.cpp"
  "job_control.cpp"
  "job_journal.cpp"
  "message_builder.cpp"
  "mode.cpp"
  "provider_output_validation.cpp"
  "question.cpp"
  "stream_bridge.cpp"
  "subagent_config.cpp"
  "subagent_coordinator.cpp"
  "subagent_job.cpp"
  "tool_dispatch_common.cpp"
  "tool_dispatch_patch.cpp"
  "tool_dispatcher.cpp"
  "tool_registry.cpp"
  "tool_result.cpp"
  "tool_scheduler.cpp"
  "tool_summaries.cpp"
  "tool_timeline.cpp"
  "tool_visibility.cpp"
  "usage_accounting.cpp"
)

target_include_directories(ava_agent PUBLIC "${CMAKE_SOURCE_DIR}/src")
target_link_libraries(ava_agent PUBLIC ava_observability PRIVATE ava_command ava_diagnostics nlohmann_json::nlohmann_json)
ava_apply_debugging(ava_agent)
ava_apply_warnings(ava_agent)
ava_apply_sanitizers(ava_agent)
