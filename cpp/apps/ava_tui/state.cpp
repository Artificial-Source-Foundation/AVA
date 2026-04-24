#include "state.hpp"

#include <algorithm>
#include <cctype>
#include <sstream>
#include <utility>

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

[[nodiscard]] std::string lowercase_copy(std::string text) {
  std::transform(text.begin(), text.end(), text.begin(), [](unsigned char ch) {
    return static_cast<char>(std::tolower(ch));
  });
  return text;
}

struct SlashCommand {
  std::string name;
  std::string args;
};

[[nodiscard]] std::optional<SlashCommand> parse_slash_command(const std::string& text) {
  if(text.empty() || text.front() != '/') {
    return std::nullopt;
  }

  const auto body = text.substr(1);
  if(body.empty()) {
    return std::nullopt;
  }

  const auto split = body.find_first_of(" \t");
  if(split == std::string::npos) {
    return SlashCommand{.name = lowercase_copy(body), .args = ""};
  }

  return SlashCommand{
      .name = lowercase_copy(body.substr(0, split)),
      .args = trim_copy(body.substr(split + 1)),
  };
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

[[nodiscard]] std::vector<std::string> slash_help_lines() {
  return {
      "Slash commands:",
      "  /help             Show this help in the message log.",
      "  /clear            Clear message log and interactive request visibility.",
      "  /model            Show current provider/model.",
      "  /compact          Reserved command (currently unsupported in C++ TUI).",
  };
}

}  // namespace

void AppState::set_model_identity(std::string provider, std::string model) {
  active_provider_ = std::move(provider);
  active_model_ = std::move(model);
}

void AppState::insert_text(const std::string& text) {
  if(history_cursor_.has_value()) {
    history_cursor_.reset();
  }
  input_buffer_ += text;
}

void AppState::backspace() {
  if(history_cursor_.has_value()) {
    history_cursor_.reset();
  }
  if(input_buffer_.empty()) {
    return;
  }
  input_buffer_.pop_back();
}

bool AppState::history_previous() {
  if(input_history_.empty()) {
    return false;
  }

  if(!history_cursor_.has_value()) {
    history_draft_ = input_buffer_;
    history_cursor_ = input_history_.size() - 1;
  } else if(*history_cursor_ > 0) {
    --(*history_cursor_);
  }

  input_buffer_ = input_history_.at(*history_cursor_);
  status_line_ = "History: older entry";
  return true;
}

bool AppState::history_next() {
  if(!history_cursor_.has_value()) {
    if(!history_draft_.empty()) {
      input_buffer_ = history_draft_;
      history_draft_.clear();
      status_line_ = "History: draft restored";
      return true;
    }
    return false;
  }

  if(*history_cursor_ + 1 < input_history_.size()) {
    ++(*history_cursor_);
    input_buffer_ = input_history_.at(*history_cursor_);
    status_line_ = "History: newer entry";
    return true;
  }

  history_cursor_.reset();
  input_buffer_ = history_draft_;
  history_draft_.clear();
  status_line_ = "History: draft restored";
  return true;
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

  if(const auto slash = parse_slash_command(trimmed); slash.has_value()) {
    if(slash->name == "help") {
      for(const auto& line : slash_help_lines()) {
        append_message(MessageKind::System, line);
      }
      status_line_ = "Displayed slash help.";
      return std::nullopt;
    }

    if(slash->name == "clear") {
      messages_.clear();
      assistant_delta_open_ = false;
      scroll_offset_ = 0;
      clear_interactive_requests();
      status_line_ = "Cleared message log.";
      return std::nullopt;
    }

    if(slash->name == "model") {
      if(slash->args.empty()) {
        const auto model_line = "Current model: " + active_provider_ + "/" + active_model_;
        append_message(MessageKind::System, model_line);
        status_line_ = model_line;
      } else {
        append_message(MessageKind::System, "Model switching is not implemented yet in C++ TUI.");
        status_line_ = "Unsupported /model arguments: model switching is not implemented yet.";
      }
      return std::nullopt;
    }

    if(slash->name == "compact") {
      append_message(MessageKind::System, "The /compact command is not implemented yet in C++ TUI.");
      status_line_ = "Unsupported command: /compact";
      return std::nullopt;
    }

    append_message(MessageKind::Error, "Unsupported slash command: /" + slash->name);
    status_line_ = "Unsupported slash command: /" + slash->name;
    return std::nullopt;
  }

  running_ = true;
  status_line_ = "Submitting prompt...";
  input_history_.push_back(trimmed);
  history_cursor_.reset();
  history_draft_.clear();
  append_message(MessageKind::User, trimmed);
  return trimmed;
}

void AppState::apply_agent_event(const ava::agent::AgentEvent& event) {
  switch(event.kind) {
    case ava::agent::AgentEventKind::RunStarted:
      running_ = true;
      assistant_delta_open_ = false;
      status_line_ = event.message.empty() ? "Agent run started." : event.message;
      return;
    case ava::agent::AgentEventKind::TurnStarted:
      running_ = true;
      assistant_delta_open_ = false;
      status_line_ = turn_status_line(event.turn);
      return;
    case ava::agent::AgentEventKind::AssistantResponse:
      assistant_delta_open_ = false;
      if(!event.message.empty()) {
        append_message(MessageKind::Assistant, event.message);
      }
      return;
    case ava::agent::AgentEventKind::AssistantResponseDelta:
      if(!event.message.empty()) {
        append_assistant_delta(event.message);
        assistant_delta_open_ = true;
      }
      return;
    case ava::agent::AgentEventKind::ToolCall:
      assistant_delta_open_ = false;
      if(event.tool_call.has_value()) {
        append_message(MessageKind::System, "tool_call: " + event.tool_call->name);
        status_line_ = "Running tool: " + event.tool_call->name;
      }
      return;
    case ava::agent::AgentEventKind::ToolResult:
      assistant_delta_open_ = false;
      if(event.tool_result.has_value()) {
        append_message(event.tool_result->is_error ? MessageKind::Error : MessageKind::System,
                       format_tool_result_line(*event.tool_result));
        status_line_ = format_tool_result_status(event.tool_result->is_error);
      }
      return;
    case ava::agent::AgentEventKind::Completion:
      running_ = false;
      assistant_delta_open_ = false;
      if(event.completion_reason.has_value()) {
        status_line_ = std::string("Run complete: ") + completion_reason_to_string(*event.completion_reason);
      } else {
        status_line_ = "Run complete.";
      }
      return;
    case ava::agent::AgentEventKind::Error:
      running_ = false;
      assistant_delta_open_ = false;
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

void AppState::scroll_to_top() {
  scroll_offset_ = 0;
}

void AppState::scroll_to_bottom() {
  scroll_offset_ = max_scroll_offset();
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

std::string AppState::message_navigation_line() const {
  if(messages_.empty()) {
    return "Messages: 0/0";
  }

  const auto start = std::min(scroll_offset_, messages_.size());
  const auto end = std::min(start + viewport_rows_, messages_.size());
  return "Messages: " + std::to_string(start + 1) + "-" + std::to_string(end) + "/" + std::to_string(messages_.size());
}

void AppState::set_interactive_request(
    ava::control_plane::InteractiveRequestKind kind,
    std::optional<ava::control_plane::InteractiveRequestHandle> request
) {
  switch(kind) {
    case ava::control_plane::InteractiveRequestKind::Approval:
      interactive_requests_.approval = std::move(request);
      return;
    case ava::control_plane::InteractiveRequestKind::Question:
      interactive_requests_.question = std::move(request);
      return;
    case ava::control_plane::InteractiveRequestKind::Plan:
      interactive_requests_.plan = std::move(request);
      return;
  }
}

void AppState::clear_interactive_requests() {
  interactive_requests_ = {};
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

void AppState::append_assistant_delta(std::string_view delta) {
  if(delta.empty()) {
    return;
  }

  if(!assistant_delta_open_ || messages_.empty() || messages_.back().kind != MessageKind::Assistant) {
    messages_.push_back(MessageLine{.kind = MessageKind::Assistant, .text = ""});
  }

  for(const char ch : delta) {
    if(ch == '\n') {
      messages_.push_back(MessageLine{.kind = MessageKind::Assistant, .text = ""});
      continue;
    }
    messages_.back().text.push_back(ch);
  }

  scroll_offset_ = max_scroll_offset();
}

}  // namespace ava::tui
