#include "state.hpp"

#include <algorithm>
#include <cctype>
#include <sstream>
#include <stdexcept>
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

void erase_last_utf8_codepoint(std::string& value) {
  if(value.empty()) {
    return;
  }

  auto erase_from = value.size() - 1;
  while(erase_from > 0 && (static_cast<unsigned char>(value.at(erase_from)) & 0xC0U) == 0x80U) {
    --erase_from;
  }
  value.erase(erase_from);
}

void trim_child_runs_to_limit(std::vector<ChildRunStateView>& child_runs, std::size_t max_child_runs) {
  while(child_runs.size() > max_child_runs) {
    // Prefer dropping retained terminal summaries before hiding currently active child runs.
    const auto inactive = std::find_if(child_runs.begin(), child_runs.end(), [](const auto& child) {
      return !child.active;
    });
    child_runs.erase(inactive == child_runs.end() ? child_runs.begin() : inactive);
  }
}

[[nodiscard]] bool request_matches_kind(
    ava::control_plane::InteractiveRequestKind expected,
    const ava::control_plane::InteractiveRequestHandle& request
) {
  return !request.request_id.empty() && request.kind == expected &&
         request.state == ava::control_plane::InteractiveRequestState::Pending;
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
  erase_last_utf8_codepoint(input_buffer_);
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
  } else {
    return false;
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
    case ava::agent::AgentEventKind::SubagentComplete:
      assistant_delta_open_ = false;
      if(event.subagent_description.has_value()) {
        append_message(MessageKind::System, "subagent_complete: " + *event.subagent_description);
      }
      status_line_ = "Subagent complete.";
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
  throw std::invalid_argument("unknown agent event kind");
}

void AppState::scroll_up(std::size_t amount) {
  scroll_offset_ = amount >= scroll_offset_ ? 0 : scroll_offset_ - amount;
}

void AppState::scroll_down(std::size_t amount) {
  const auto max_offset = max_scroll_offset();
  if(scroll_offset_ >= max_offset) {
    scroll_offset_ = max_offset;
    return;
  }

  const auto remaining = max_offset - scroll_offset_;
  if(amount >= remaining) {
    scroll_offset_ = max_offset;
    return;
  }

  scroll_offset_ += amount;
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
  if(request.has_value() && !request_matches_kind(kind, *request)) {
    request.reset();
  }

  switch(kind) {
    case ava::control_plane::InteractiveRequestKind::Approval:
      interactive_requests_.approval = std::move(request);
      refresh_interactive_dock();
      return;
    case ava::control_plane::InteractiveRequestKind::Question:
      interactive_requests_.question = std::move(request);
      refresh_interactive_dock();
      return;
    case ava::control_plane::InteractiveRequestKind::Plan:
      interactive_requests_.plan = std::move(request);
      refresh_interactive_dock();
      return;
  }
  throw std::invalid_argument("unknown interactive request kind");
}

void AppState::set_interactive_request_details(std::string request_id, std::vector<std::string> details) {
  if(request_id.empty()) {
    return;
  }
  interactive_request_details_[request_id] = std::move(details);
  if(active_interactive_dock_.has_value() && active_interactive_dock_->request.request_id == request_id) {
    active_interactive_dock_->detail_lines = interactive_request_details_.at(request_id);
  }
}

void AppState::set_interactive_approval_can_approve(std::string request_id, bool can_approve) {
  if(request_id.empty()) {
    return;
  }
  interactive_approval_can_approve_[request_id] = can_approve;
  if(active_interactive_dock_.has_value() && active_interactive_dock_->kind == InteractiveDockKind::Approval &&
     active_interactive_dock_->request.request_id == request_id) {
    active_interactive_dock_->approval_can_approve = can_approve;
  }
}

void AppState::clear_interactive_requests() {
  interactive_requests_ = {};
  active_interactive_dock_.reset();
  dismissed_interactive_request_ids_.clear();
  interactive_request_details_.clear();
  interactive_approval_can_approve_.clear();
}

void AppState::dismiss_interactive_dock() {
  if(!active_interactive_dock_.has_value()) {
    return;
  }
  dismissed_interactive_request_ids_.push_back(active_interactive_dock_->request.request_id);
  status_line_ = "Interactive request dock dismissed.";
  active_interactive_dock_.reset();
}

void AppState::insert_interactive_answer_text(const std::string& text) {
  if(!active_interactive_dock_.has_value() || active_interactive_dock_->kind != InteractiveDockKind::Question) {
    return;
  }
  active_interactive_dock_->answer_draft += text;
}

void AppState::backspace_interactive_answer() {
  if(!active_interactive_dock_.has_value() || active_interactive_dock_->kind != InteractiveDockKind::Question) {
    return;
  }
  erase_last_utf8_codepoint(active_interactive_dock_->answer_draft);
}

std::optional<InteractiveAdapterAction> AppState::approve_interactive_dock_action() const {
  if(!active_interactive_dock_.has_value() || active_interactive_dock_->kind != InteractiveDockKind::Approval) {
    return std::nullopt;
  }
  if(!active_interactive_dock_->approval_can_approve) {
    return std::nullopt;
  }
  return InteractiveAdapterAction{
      .kind = InteractiveAdapterActionKind::Approve,
      .request_id = active_interactive_dock_->request.request_id,
  };
}

std::optional<InteractiveAdapterAction> AppState::reject_interactive_dock_action(std::string reason) const {
  if(!active_interactive_dock_.has_value()) {
    return std::nullopt;
  }

  switch(active_interactive_dock_->kind) {
    case InteractiveDockKind::Approval:
      return InteractiveAdapterAction{
          .kind = InteractiveAdapterActionKind::Reject,
          .request_id = active_interactive_dock_->request.request_id,
          .value = std::move(reason),
      };
    case InteractiveDockKind::Plan:
      return InteractiveAdapterAction{
          .kind = InteractiveAdapterActionKind::RejectPlan,
          .request_id = active_interactive_dock_->request.request_id,
      };
    case InteractiveDockKind::Question:
      return InteractiveAdapterAction{
          .kind = InteractiveAdapterActionKind::CancelQuestion,
          .request_id = active_interactive_dock_->request.request_id,
      };
  }
  return std::nullopt;
}

std::optional<InteractiveAdapterAction> AppState::answer_interactive_dock_action() const {
  if(!active_interactive_dock_.has_value() || active_interactive_dock_->kind != InteractiveDockKind::Question) {
    return std::nullopt;
  }
  return InteractiveAdapterAction{
      .kind = InteractiveAdapterActionKind::Answer,
      .request_id = active_interactive_dock_->request.request_id,
      .value = active_interactive_dock_->answer_draft,
  };
}

std::optional<InteractiveAdapterAction> AppState::accept_plan_interactive_dock_action() const {
  if(!active_interactive_dock_.has_value() || active_interactive_dock_->kind != InteractiveDockKind::Plan) {
    return std::nullopt;
  }
  return InteractiveAdapterAction{
      .kind = InteractiveAdapterActionKind::AcceptPlan,
      .request_id = active_interactive_dock_->request.request_id,
  };
}

void AppState::apply_interactive_action_result(const InteractiveAdapterActionResult& result) {
  if(!result.accepted) {
    status_line_ = result.error.empty() ? "Interactive action failed." : "Interactive action failed: " + result.error;
    return;
  }

  if(result.terminal_request.has_value()) {
    status_line_ = "Interactive request resolved: " + result.terminal_request->request_id;
    return;
  }

  status_line_ = "Interactive request resolved.";
}

void AppState::set_active_child_runs(const std::vector<ava::orchestration::ChildRunInfo>& runs) {
  child_runs_.erase(
      std::remove_if(child_runs_.begin(), child_runs_.end(), [&](const auto& child) {
        if(!child.active) {
          return false;
        }
        return std::none_of(runs.begin(), runs.end(), [&](const auto& run) {
          return run.run_id == child.run_id;
        });
      }),
      child_runs_.end()
  );

  for(const auto& run : runs) {
    auto view = ChildRunStateView{
        .run_id = run.run_id,
        .agent_type = run.agent_type,
        .active = true,
        .cancelled = false,
        .watchdog_timed_out = false,
        .completion_reason = "active",
        .error = std::nullopt,
    };
    const auto existing = std::find_if(child_runs_.begin(), child_runs_.end(), [&](const auto& child) {
      return child.run_id == run.run_id;
    });
    if(existing != child_runs_.end() && !existing->active) {
      // Run IDs are expected to be unique; do not reactivate a terminal summary.
      continue;
    }
    if(existing == child_runs_.end()) {
      child_runs_.push_back(std::move(view));
    } else {
      *existing = std::move(view);
    }
  }

  trim_child_runs_to_limit(child_runs_, kMaxChildRuns);
}

void AppState::record_child_run_terminal_summary(const ava::orchestration::ChildRunTerminalSummary& summary) {
  auto view = ChildRunStateView{
      .run_id = summary.run_id,
      .agent_type = summary.agent_type,
      .active = false,
      .cancelled = summary.cancelled,
      .watchdog_timed_out = summary.watchdog_timed_out,
      .completion_reason = summary.completion_reason,
      .error = summary.error,
  };

  const auto existing = std::find_if(child_runs_.begin(), child_runs_.end(), [&](const auto& child) {
    return child.run_id == summary.run_id;
  });
  const auto should_update_status = existing == child_runs_.end() || existing->active;
  if(existing == child_runs_.end()) {
    child_runs_.push_back(std::move(view));
  } else {
    *existing = std::move(view);
  }

  trim_child_runs_to_limit(child_runs_, kMaxChildRuns);

  if(should_update_status) {
    status_line_ = "Child run " + summary.agent_type + " complete: " + summary.completion_reason;
  }
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

bool AppState::active_dock_still_pending() const {
  if(!active_interactive_dock_.has_value()) {
    return false;
  }

  const auto request_id = active_interactive_dock_->request.request_id;
  switch(active_interactive_dock_->kind) {
    case InteractiveDockKind::Approval:
      return interactive_requests_.approval.has_value() && interactive_requests_.approval->request_id == request_id;
    case InteractiveDockKind::Question:
      return interactive_requests_.question.has_value() && interactive_requests_.question->request_id == request_id;
    case InteractiveDockKind::Plan:
      return interactive_requests_.plan.has_value() && interactive_requests_.plan->request_id == request_id;
  }
  return false;
}

void AppState::refresh_interactive_dock() {
  std::vector<std::string> active_request_ids;
  if(interactive_requests_.approval.has_value()) {
    active_request_ids.push_back(interactive_requests_.approval->request_id);
  }
  if(interactive_requests_.question.has_value()) {
    active_request_ids.push_back(interactive_requests_.question->request_id);
  }
  if(interactive_requests_.plan.has_value()) {
    active_request_ids.push_back(interactive_requests_.plan->request_id);
  }
  const auto is_active_request = [&](const std::string& request_id) {
    return std::find(active_request_ids.begin(), active_request_ids.end(), request_id) != active_request_ids.end();
  };
  dismissed_interactive_request_ids_.erase(
      std::remove_if(
          dismissed_interactive_request_ids_.begin(),
          dismissed_interactive_request_ids_.end(),
          [&](const std::string& request_id) { return !is_active_request(request_id); }
      ),
      dismissed_interactive_request_ids_.end()
  );
  for(auto it = interactive_request_details_.begin(); it != interactive_request_details_.end();) {
    if(!is_active_request(it->first)) {
      it = interactive_request_details_.erase(it);
    } else {
      ++it;
    }
  }
  for(auto it = interactive_approval_can_approve_.begin(); it != interactive_approval_can_approve_.end();) {
    if(!is_active_request(it->first)) {
      it = interactive_approval_can_approve_.erase(it);
    } else {
      ++it;
    }
  }

  if(active_dock_still_pending()) {
    return;
  }

  if(interactive_requests_.approval.has_value() &&
     !interactive_request_dismissed(interactive_requests_.approval->request_id)) {
    active_interactive_dock_ = InteractiveDockState{
        .kind = InteractiveDockKind::Approval,
        .request = *interactive_requests_.approval,
        .detail_lines = interactive_request_details_[interactive_requests_.approval->request_id],
        .approval_can_approve = interactive_approval_can_approve_.contains(interactive_requests_.approval->request_id)
            ? interactive_approval_can_approve_.at(interactive_requests_.approval->request_id)
            : false,
    };
    status_line_ = "Approval request pending: " + interactive_requests_.approval->request_id;
    return;
  }

  if(interactive_requests_.question.has_value() &&
     !interactive_request_dismissed(interactive_requests_.question->request_id)) {
    active_interactive_dock_ = InteractiveDockState{
        .kind = InteractiveDockKind::Question,
        .request = *interactive_requests_.question,
        .detail_lines = interactive_request_details_[interactive_requests_.question->request_id],
    };
    status_line_ = "Question pending: " + interactive_requests_.question->request_id;
    return;
  }

  if(interactive_requests_.plan.has_value() && !interactive_request_dismissed(interactive_requests_.plan->request_id)) {
    active_interactive_dock_ = InteractiveDockState{
        .kind = InteractiveDockKind::Plan,
        .request = *interactive_requests_.plan,
        .detail_lines = interactive_request_details_[interactive_requests_.plan->request_id],
    };
    status_line_ = "Plan approval pending: " + interactive_requests_.plan->request_id;
    return;
  }

  active_interactive_dock_.reset();
}

bool AppState::interactive_request_dismissed(const std::string& request_id) const {
  return std::find(dismissed_interactive_request_ids_.begin(), dismissed_interactive_request_ids_.end(), request_id) !=
         dismissed_interactive_request_ids_.end();
}

}  // namespace ava::tui
