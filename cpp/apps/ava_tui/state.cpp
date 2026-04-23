#include "state.hpp"

#include <algorithm>
#include <cctype>
#include <sstream>

namespace ava::tui {
namespace {

[[nodiscard]] std::string trim_copy(const std::string& text) {
  const auto begin = std::find_if_not(text.begin(), text.end(), [](unsigned char ch) {
    return std::isspace(ch) != 0;
  });
  if(begin == text.end()) {
    return "";
  }

  const auto end = std::find_if_not(text.rbegin(), text.rend(), [](unsigned char ch) {
    return std::isspace(ch) != 0;
  }).base();

  return std::string(begin, end);
}

[[nodiscard]] const char* completion_reason_to_string(ava::agent::AgentCompletionReason reason) {
  switch(reason) {
    case ava::agent::AgentCompletionReason::Completed:
      return "completed";
    case ava::agent::AgentCompletionReason::MaxTurns:
      return "max_turns";
    case ava::agent::AgentCompletionReason::Stuck:
      return "stuck";
    case ava::agent::AgentCompletionReason::Error:
      return "error";
  }
  return "error";
}

[[nodiscard]] std::string turn_status_line(std::size_t turn) {
  return "Turn " + std::to_string(turn) + " started.";
}

[[nodiscard]] std::string format_tool_result_status(bool is_error) {
  return std::string{"Tool completed: "} + (is_error ? "error." : "ok.");
}

[[nodiscard]] std::string format_tool_result_line(const ava::types::ToolResult& result) {
  const auto suffix = result.is_error ? "error" : "ok";
  if(result.call_id.empty()) {
    return std::string{"tool_result: "} + suffix;
  }
  return "tool_result[" + result.call_id + "]: " + suffix;
}

}  // namespace

void AppState::insert_text(const std::string& text) {
  input_buffer_ += text;
}

void AppState::backspace() {
  if(input_buffer_.empty()) {
    return;
  }
  input_buffer_.pop_back();
}

std::optional<std::string> AppState::take_submission() {
  if(running_) {
    status_line_ = "Run is already in progress.";
    return std::nullopt;
  }

  const auto trimmed = trim_copy(input_buffer_);
  input_buffer_.clear();
  if(trimmed.empty()) {
    status_line_ = "Input is empty.";
    return std::nullopt;
  }

  running_ = true;
  status_line_ = "Submitting prompt...";
  append_message(MessageKind::User, trimmed);
  return trimmed;
}

void AppState::apply_agent_event(const ava::agent::AgentEvent& event) {
  switch(event.kind) {
    case ava::agent::AgentEventKind::RunStarted:
      running_ = true;
      status_line_ = event.message.empty() ? "Agent run started." : event.message;
      return;
    case ava::agent::AgentEventKind::TurnStarted:
      running_ = true;
      status_line_ = turn_status_line(event.turn);
      return;
    case ava::agent::AgentEventKind::AssistantResponse:
      if(!event.message.empty()) {
        append_message(MessageKind::Assistant, event.message);
      }
      return;
    case ava::agent::AgentEventKind::ToolCall:
      if(event.tool_call.has_value()) {
        append_message(MessageKind::System, "tool_call: " + event.tool_call->name);
        status_line_ = "Running tool: " + event.tool_call->name;
      }
      return;
    case ava::agent::AgentEventKind::ToolResult:
      if(event.tool_result.has_value()) {
        append_message(event.tool_result->is_error ? MessageKind::Error : MessageKind::System,
                       format_tool_result_line(*event.tool_result));
        status_line_ = format_tool_result_status(event.tool_result->is_error);
      }
      return;
    case ava::agent::AgentEventKind::Completion:
      running_ = false;
      if(event.completion_reason.has_value()) {
        status_line_ = std::string("Run complete: ") + completion_reason_to_string(*event.completion_reason);
      } else {
        status_line_ = "Run complete.";
      }
      return;
    case ava::agent::AgentEventKind::Error:
      running_ = false;
      status_line_ = event.message.empty() ? "Run error." : "Run error: " + event.message;
      if(!event.message.empty()) {
        append_message(MessageKind::Error, event.message);
      }
      return;
  }
}

void AppState::scroll_up(std::size_t amount) {
  scroll_offset_ = amount >= scroll_offset_ ? 0 : scroll_offset_ - amount;
}

void AppState::scroll_down(std::size_t amount) {
  scroll_offset_ = std::min(scroll_offset_ + amount, max_scroll_offset());
}

void AppState::page_up() {
  scroll_up(std::max<std::size_t>(1, viewport_rows_));
}

void AppState::page_down() {
  scroll_down(std::max<std::size_t>(1, viewport_rows_));
}

void AppState::set_viewport_rows(std::size_t rows) {
  viewport_rows_ = std::max<std::size_t>(1, rows);
  scroll_offset_ = std::min(scroll_offset_, max_scroll_offset());
}

std::vector<MessageLine> AppState::visible_messages() const {
  if(messages_.empty()) {
    return {};
  }

  const auto start = std::min(scroll_offset_, messages_.size());
  const auto remaining = messages_.size() - start;
  const auto count = std::min(viewport_rows_, remaining);
  return std::vector<MessageLine>{messages_.begin() + static_cast<std::ptrdiff_t>(start),
                                  messages_.begin() + static_cast<std::ptrdiff_t>(start + count)};
}

std::size_t AppState::max_scroll_offset() const {
  if(messages_.size() <= viewport_rows_) {
    return 0;
  }
  return messages_.size() - viewport_rows_;
}

void AppState::append_message(MessageKind kind, std::string text) {
  if(text.empty()) {
    return;
  }

  std::stringstream stream(text);
  std::string line;
  while(std::getline(stream, line)) {
    messages_.push_back(MessageLine{.kind = kind, .text = line});
  }
  if(text.ends_with('\n')) {
    messages_.push_back(MessageLine{.kind = kind, .text = ""});
  }
  scroll_offset_ = max_scroll_offset();
}

}  // namespace ava::tui
