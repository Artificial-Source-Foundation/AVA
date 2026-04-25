#include "events.hpp"

#include <iostream>
#include <optional>
#include <string>

#include "ava/control_plane/events.hpp"

namespace ava::app {
namespace {

[[nodiscard]] bool has_text(const std::optional<std::string>& value) {
  return value.has_value() && value->find_first_not_of(" \t\r\n") != std::string::npos;
}

[[nodiscard]] const char* completion_reason_to_string(ava::agent::AgentCompletionReason reason) {
  switch(reason) {
    case ava::agent::AgentCompletionReason::Completed:
      return "completed";
    case ava::agent::AgentCompletionReason::Cancelled:
      return "cancelled";
    case ava::agent::AgentCompletionReason::MaxTurns:
      return "max_turns";
    case ava::agent::AgentCompletionReason::Stuck:
      return "stuck";
    case ava::agent::AgentCompletionReason::Error:
      return "error";
  }
  return "error";
}

[[nodiscard]] std::optional<std::string> missing_subagent_complete_field(const ava::agent::AgentEvent& event) {
  if(!has_text(event.run_id)) {
    return "run_id";
  }
  if(!has_text(event.subagent_call_id)) {
    return "call_id";
  }
  if(!has_text(event.subagent_session_id)) {
    return "session_id";
  }
  if(!has_text(event.subagent_description)) {
    return "description";
  }
  return std::nullopt;
}

}  // namespace

nlohmann::json headless_event_to_ndjson(const ava::agent::AgentEvent& event) {
  auto with_run_id = [&](nlohmann::json payload) {
    if(event.run_id.has_value()) {
      payload["run_id"] = *event.run_id;
    }
    return payload;
  };

  switch(event.kind) {
    case ava::agent::AgentEventKind::RunStarted:
      return with_run_id(nlohmann::json{{"type", "run_started"}});
    case ava::agent::AgentEventKind::TurnStarted:
      return with_run_id(nlohmann::json{{"type", "turn_started"}, {"turn", event.turn}});
    case ava::agent::AgentEventKind::AssistantResponseDelta:
      return with_run_id(nlohmann::json{{"type", "assistant_response_delta"}, {"turn", event.turn}, {"delta", event.message}});
    case ava::agent::AgentEventKind::AssistantResponse:
      return with_run_id(nlohmann::json{{"type", "assistant_response"}, {"turn", event.turn}, {"content", event.message}});
    case ava::agent::AgentEventKind::ToolCall: {
      nlohmann::json json = with_run_id(nlohmann::json{{"type", "tool_call"}, {"turn", event.turn}});
      if(event.tool_call.has_value()) {
        json["call_id"] = event.tool_call->id;
        json["tool"] = event.tool_call->name;
        json["args"] = event.tool_call->arguments;
      }
      return json;
    }
    case ava::agent::AgentEventKind::ToolResult: {
      nlohmann::json json = with_run_id(nlohmann::json{{"type", "tool_result"}, {"turn", event.turn}});
      if(event.tool_result.has_value()) {
        json["call_id"] = event.tool_result->call_id;
        json["content"] = event.tool_result->content;
        json["is_error"] = event.tool_result->is_error;
      }
      return json;
    }
    case ava::agent::AgentEventKind::SubagentComplete: {
      if(const auto missing = missing_subagent_complete_field(event); missing.has_value()) {
        nlohmann::json error = nlohmann::json{{"type",
                                               std::string(ava::control_plane::canonical_event_kind_to_type_tag(
                                                   ava::control_plane::CanonicalEventKind::Error
                                               ))},
                                              {"run_id", has_text(event.run_id) ? *event.run_id : std::string{"unknown"}},
                                              {"message",
                                               "malformed subagent_complete event: missing required canonical field: " +
                                                   *missing}};
        return error;
      }

      nlohmann::json json = with_run_id(nlohmann::json{{"type", std::string(ava::control_plane::canonical_event_kind_to_type_tag(
                                              ava::control_plane::CanonicalEventKind::SubagentComplete))}});
      json["call_id"] = *event.subagent_call_id;
      json["session_id"] = *event.subagent_session_id;
      json["description"] = *event.subagent_description;
      if(event.subagent_message_count.has_value()) {
        json["message_count"] = *event.subagent_message_count;
      }
      return json;
    }
    case ava::agent::AgentEventKind::Completion: {
      // Preserve canonical tag spelling for overlapping lifecycle tags.
      nlohmann::json json = with_run_id(nlohmann::json{{"type", std::string(ava::control_plane::canonical_event_kind_to_type_tag(
                                              ava::control_plane::CanonicalEventKind::Complete))},
                                         {"turn", event.turn},
                                         {"message", event.message}});
      if(event.completion_reason.has_value()) {
        json["reason"] = completion_reason_to_string(*event.completion_reason);
      }
      return json;
    }
    case ava::agent::AgentEventKind::Error:
      return with_run_id(nlohmann::json{{"type",
                                         std::string(ava::control_plane::canonical_event_kind_to_type_tag(
                                             ava::control_plane::CanonicalEventKind::Error
                                         ))},
                                        {"turn", event.turn},
                                        {"message", event.message}});
  }

  return nlohmann::json{{"type", "error"}, {"message", "unhandled event kind"}};
}

void print_headless_event_text(const ava::agent::AgentEvent& event) {
  switch(event.kind) {
    case ava::agent::AgentEventKind::RunStarted:
      std::cout << "[run] started\n";
      return;
    case ava::agent::AgentEventKind::TurnStarted:
      std::cout << "\n[turn " << event.turn << "]\n";
      return;
    case ava::agent::AgentEventKind::AssistantResponse:
      if(!event.message.empty()) {
        std::cout << event.message << "\n";
      }
      return;
    case ava::agent::AgentEventKind::AssistantResponseDelta:
      if(!event.message.empty()) {
        std::cout << event.message;
      }
      return;
    case ava::agent::AgentEventKind::ToolCall:
      if(event.tool_call.has_value()) {
        std::cout << "[tool_call] " << event.tool_call->name << "\n";
      }
      return;
    case ava::agent::AgentEventKind::ToolResult:
      if(event.tool_result.has_value()) {
        std::cout << "[tool_result] " << (event.tool_result->is_error ? "error" : "ok") << "\n";
      }
      return;
    case ava::agent::AgentEventKind::SubagentComplete:
      if(event.subagent_description.has_value()) {
        std::cout << "[subagent_complete] " << *event.subagent_description << "\n";
      } else {
        std::cout << "[subagent_complete]\n";
      }
      return;
    case ava::agent::AgentEventKind::Completion:
      std::cout << "\n";
      return;
    case ava::agent::AgentEventKind::Error:
      std::cerr << "[error] " << event.message << "\n";
      return;
  }
}

}  // namespace ava::app
