#include "ava/agent/runtime.hpp"

#include <algorithm>
#include <chrono>
#include <cctype>
#include <ctime>
#include <iomanip>
#include <sstream>
#include <stdexcept>
#include <utility>

#include <nlohmann/json.hpp>

#include "ava/agent/response.hpp"

namespace ava::agent {

namespace {

[[nodiscard]] std::string now_utc_rfc3339() {
  const auto now = std::chrono::system_clock::now();
  const auto time = std::chrono::system_clock::to_time_t(now);
  std::tm utc{};
#if defined(_WIN32)
  gmtime_s(&utc, &time);
#else
  gmtime_r(&time, &utc);
#endif

  std::ostringstream out;
  out << std::put_time(&utc, "%Y-%m-%dT%H:%M:%SZ");
  return out.str();
}

[[nodiscard]] std::string make_message_id(std::uint64_t index) {
  return "m7_msg_" + std::to_string(index);
}

[[nodiscard]] std::string normalize_role(const std::string& role) {
  std::string out = role;
  std::transform(out.begin(), out.end(), out.begin(), [](const auto ch) {
    return static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
  });
  return out;
}

void append_session_message(
    ava::types::SessionRecord& session,
    std::uint64_t& id_counter,
    const std::string& role,
    std::string content
) {
  const auto now = now_utc_rfc3339();
  const auto parent = session.messages.empty() ? std::optional<std::string>{} : std::optional<std::string>{session.messages.back().id};
  session.messages.push_back(ava::types::SessionMessage{
      .id = make_message_id(++id_counter),
      .role = role,
      .content = std::move(content),
      .timestamp = now,
      .parent_id = parent,
  });
  session.updated_at = now;
  session.branch_head = session.messages.back().id;
}

[[nodiscard]] ava::llm::ChatMessage to_chat_message(const ava::types::SessionMessage& message) {
  const auto role = normalize_role(message.role);
  if(role == "system") {
    return ava::llm::ChatMessage::system(message.content);
  }
  if(role == "assistant") {
    return ava::llm::ChatMessage::assistant(message.content);
  }
  if(role == "tool") {
    try {
      const auto parsed = nlohmann::json::parse(message.content);
      if(parsed.is_object() && parsed.contains("content")) {
        return ava::llm::ChatMessage::tool(
            parsed.value("content", std::string{}),
            parsed.value("call_id", std::string{"unknown_call"})
        );
      }
    } catch(const std::exception&) {
      // Fall through to content-only tool fallback.
    }
    return ava::llm::ChatMessage::tool(message.content, "unknown_call");
  }

  return ava::llm::ChatMessage::user(message.content);
}

[[nodiscard]] std::vector<ava::llm::ChatMessage> build_chat_messages(
    const ava::types::SessionRecord& session,
    const std::string& system_prompt
) {
  std::vector<ava::llm::ChatMessage> messages;
  messages.reserve(session.messages.size() + 1);
  messages.push_back(ava::llm::ChatMessage::system(system_prompt));
  for(const auto& message : session.messages) {
    messages.push_back(to_chat_message(message));
  }
  return messages;
}

[[nodiscard]] std::string encode_tool_result_message(const ava::types::ToolResult& result) {
  return nlohmann::json{
      {"call_id", result.call_id},
      {"content", result.content},
      {"is_error", result.is_error},
  }
      .dump();
}

void emit_event(const AgentEventSink& sink, AgentEvent event) {
  if(sink) {
    sink(event);
  }
}

}  // namespace

AgentRuntime::AgentRuntime(const ava::llm::Provider& provider, const ava::tools::ToolRegistry& tools, AgentConfig config)
    : provider_(provider), tools_(tools), config_(std::move(config)) {}

std::string AgentRuntime::build_system_prompt() const {
  std::ostringstream prompt;
  prompt << config_.system_prompt_preamble << "\n\n";
  prompt << "Use tools when they materially improve correctness.\n";
  prompt << "When you are done, provide a direct assistant answer.\n\n";
  prompt << "Available tools:\n";

  const auto tools = tools_.list_tools();
  for(const auto& tool : tools) {
    prompt << "- " << tool.name << ": " << tool.description << "\n";
  }

  return prompt.str();
}

AgentRunResult AgentRuntime::run(
    ava::types::SessionRecord& session,
    const AgentRunInput& input,
    AgentEventSink on_event
) const {
  if(input.goal.empty() && session.messages.empty()) {
    throw std::invalid_argument("agent runtime requires a goal or an existing session history");
  }

  std::uint64_t id_counter = session.messages.size();
  if(!input.goal.empty()) {
    append_session_message(session, id_counter, "user", input.goal);
  }

  emit_event(on_event, AgentEvent{.kind = AgentEventKind::RunStarted, .turn = 0, .message = "agent run started"});

  auto stuck = StuckDetector(config_.stuck);
  auto result = AgentRunResult{};
  const auto system_prompt = build_system_prompt();
  const auto tool_definitions = tools_.list_tools();

  auto completion = [&](AgentCompletionReason reason, std::string message) {
    result.reason = reason;
    result.final_response = std::move(message);
    session.metadata["agent"]["last_completion_reason"] = completion_reason_to_string(reason);
    session.metadata["agent"]["last_turns_used"] = result.turns_used;
    emit_event(
        on_event,
        AgentEvent{
            .kind = AgentEventKind::Completion,
            .turn = result.turns_used,
            .message = result.final_response,
            .completion_reason = reason,
        }
    );
    return result;
  };

  try {
    for(std::size_t turn = 1; turn <= config_.max_turns; ++turn) {
      result.turns_used = turn;
      emit_event(on_event, AgentEvent{.kind = AgentEventKind::TurnStarted, .turn = turn});

      if(input.queue != nullptr && input.queue->has_steering()) {
        for(auto& steering_message : input.queue->drain_steering()) {
          append_session_message(session, id_counter, "user", steering_message);
        }
      }

      const auto chat_messages = build_chat_messages(session, system_prompt);
      const auto response = provider_.generate(chat_messages, tool_definitions, config_.thinking);
      result.usage = response.usage;

      auto tool_calls = response::coalesce_tool_calls(response);
      const auto assistant_text = response.content;

      emit_event(on_event, AgentEvent{.kind = AgentEventKind::AssistantResponse, .turn = turn, .message = assistant_text});

      const auto stuck_action = config_.enable_stuck_detector
                                    ? stuck.check(assistant_text, tool_calls)
                                    : StuckAction{};
      if(stuck_action.kind == StuckActionKind::InjectMessage) {
        append_session_message(session, id_counter, "assistant", assistant_text);
        append_session_message(session, id_counter, "user", stuck_action.message);
        continue;
      } else if(stuck_action.kind == StuckActionKind::Stop) {
        append_session_message(session, id_counter, "assistant", assistant_text);
        emit_event(on_event, AgentEvent{.kind = AgentEventKind::Error, .turn = turn, .message = stuck_action.message});
        return completion(AgentCompletionReason::Stuck, stuck_action.message);
      }

      if(tool_calls.empty()) {
        append_session_message(session, id_counter, "assistant", assistant_text);
        return completion(AgentCompletionReason::Completed, assistant_text);
      }

      append_session_message(session, id_counter, "assistant", assistant_text);
      for(const auto& tool_call : tool_calls) {
        emit_event(
            on_event,
            AgentEvent{
                .kind = AgentEventKind::ToolCall,
                .turn = turn,
                .tool_call = tool_call,
            }
        );

        const auto tool_result = tools_.execute(tool_call);
        append_session_message(session, id_counter, "tool", encode_tool_result_message(tool_result));

        emit_event(
            on_event,
            AgentEvent{
                .kind = AgentEventKind::ToolResult,
                .turn = turn,
                .tool_result = tool_result,
            }
        );
      }
    }

    return completion(
        AgentCompletionReason::MaxTurns,
        "agent stopped after reaching max_turns without a completion response"
    );
  } catch(const std::exception& ex) {
    result.reason = AgentCompletionReason::Error;
    result.error = ex.what();
    emit_event(
        on_event,
        AgentEvent{
            .kind = AgentEventKind::Error,
            .turn = result.turns_used,
            .message = ex.what(),
        }
    );
    return result;
  } catch(...) {
    constexpr auto* kUnknownError = "agent runtime failed with a non-standard exception";
    result.reason = AgentCompletionReason::Error;
    result.error = kUnknownError;
    emit_event(
        on_event,
        AgentEvent{
            .kind = AgentEventKind::Error,
            .turn = result.turns_used,
            .message = kUnknownError,
        }
    );
    return result;
  }
}

std::string AgentRuntime::completion_reason_to_string(AgentCompletionReason reason) {
  switch(reason) {
    case AgentCompletionReason::Completed:
      return "completed";
    case AgentCompletionReason::MaxTurns:
      return "max_turns";
    case AgentCompletionReason::Stuck:
      return "stuck";
    case AgentCompletionReason::Error:
      return "error";
  }
  return "error";
}

}  // namespace ava::agent
