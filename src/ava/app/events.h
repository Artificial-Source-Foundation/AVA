#pragma once

#include <functional>
#include <string>

#include "ava/agent/mode.h"
#include "ava/core/result.h"

namespace ava::app {

enum class RuntimeEventType {
  SessionStart,
  UserMessage,
  AssistantMessage,
  ToolStart,
  ToolResult,
  Error,
  Done,
};

struct RuntimeEvent {
  RuntimeEventType type = RuntimeEventType::Done;
  std::string timestamp;
  std::string session_id;
  ava::agent::Mode mode = ava::agent::Mode::Build;
  std::string provider_id;
  std::string model_id;
  std::string text;
  std::string call_id;
  std::string tool_name;
  std::string status;
  std::string error_category;
  std::string error_message;
  std::string error_details;
  std::string stop_reason;
  std::size_t provider_iterations = 0;
  std::size_t tool_calls = 0;
};

using RuntimeEventSink = std::function<ava::core::VoidResult(const RuntimeEvent&)>;

[[nodiscard]] std::string to_string(RuntimeEventType type);
[[nodiscard]] std::string serialize_event_json(const RuntimeEvent& event);
[[nodiscard]] std::string serialize_event_jsonl(const RuntimeEvent& event);
[[nodiscard]] ava::core::VoidResult emit_event(const RuntimeEventSink& sink, const RuntimeEvent& event);

}  // namespace ava::app
