#pragma once

#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "ava/agent/runtime.hpp"
#include "ava/control_plane/interactive.hpp"
#include "ava/orchestration/task.hpp"
#include "interactive_action_adapter.hpp"

namespace ava::tui {

enum class MessageKind {
  User,
  Assistant,
  System,
  Error,
};

struct MessageLine {
  MessageKind kind{MessageKind::System};
  std::string text;
};

struct InteractiveRequestStateView {
  std::optional<ava::control_plane::InteractiveRequestHandle> approval;
  std::optional<ava::control_plane::InteractiveRequestHandle> question;
  std::optional<ava::control_plane::InteractiveRequestHandle> plan;

  [[nodiscard]] std::size_t pending_count() const {
    return (approval.has_value() ? 1U : 0U) + (question.has_value() ? 1U : 0U) + (plan.has_value() ? 1U : 0U);
  }
};

struct ChildRunStateView {
  std::string run_id;
  std::string agent_type;
  bool active{false};
  bool cancelled{false};
  bool watchdog_timed_out{false};
  std::string completion_reason;
  std::optional<std::string> error;
};

enum class InteractiveDockKind {
  Approval,
  Question,
  Plan,
};

struct InteractiveDockState {
  InteractiveDockKind kind{InteractiveDockKind::Approval};
  ava::control_plane::InteractiveRequestHandle request;
  std::string answer_draft;
  std::vector<std::string> detail_lines;
  bool approval_can_approve{true};
};

class AppState {
 public:
  static constexpr std::size_t kMaxChildRuns = 256;

  [[nodiscard]] const std::vector<MessageLine>& messages() const { return messages_; }
  [[nodiscard]] const std::string& input_buffer() const { return input_buffer_; }
  [[nodiscard]] const std::string& status_line() const { return status_line_; }
  [[nodiscard]] bool running() const { return running_; }
  [[nodiscard]] bool quit_requested() const { return quit_requested_; }
  [[nodiscard]] std::size_t scroll_offset() const { return scroll_offset_; }
  [[nodiscard]] std::size_t viewport_rows() const { return viewport_rows_; }
  [[nodiscard]] const InteractiveRequestStateView& interactive_requests() const { return interactive_requests_; }
  [[nodiscard]] const std::vector<ChildRunStateView>& child_runs() const { return child_runs_; }
  [[nodiscard]] const std::optional<InteractiveDockState>& active_interactive_dock() const {
    return active_interactive_dock_;
  }

  void request_quit() { quit_requested_ = true; }
  void clear_quit_request() { quit_requested_ = false; }
  void set_running(bool value) { running_ = value; }
  void set_status_line(std::string text) { status_line_ = std::move(text); }
  void set_model_identity(std::string provider, std::string model);

  void insert_text(const std::string& text);
  void backspace();
  [[nodiscard]] bool history_previous();
  [[nodiscard]] bool history_next();

  [[nodiscard]] std::optional<std::string> take_submission();
  void apply_agent_event(const ava::agent::AgentEvent& event);

  void scroll_up(std::size_t amount);
  void scroll_down(std::size_t amount);
  void scroll_to_top();
  void scroll_to_bottom();
  void page_up();
  void page_down();
  void set_viewport_rows(std::size_t rows);
  [[nodiscard]] std::string message_navigation_line() const;

  void set_interactive_request(
      ava::control_plane::InteractiveRequestKind kind,
      std::optional<ava::control_plane::InteractiveRequestHandle> request
  );
  void set_interactive_request_details(std::string request_id, std::vector<std::string> details);
  void set_interactive_approval_can_approve(std::string request_id, bool can_approve);
  void clear_interactive_requests();
  void dismiss_interactive_dock();
  void insert_interactive_answer_text(const std::string& text);
  void backspace_interactive_answer();
  [[nodiscard]] std::optional<InteractiveAdapterAction> approve_interactive_dock_action() const;
  [[nodiscard]] std::optional<InteractiveAdapterAction> reject_interactive_dock_action(std::string reason) const;
  [[nodiscard]] std::optional<InteractiveAdapterAction> answer_interactive_dock_action() const;
  [[nodiscard]] std::optional<InteractiveAdapterAction> accept_plan_interactive_dock_action() const;
  void apply_interactive_action_result(const InteractiveAdapterActionResult& result);
  void set_active_child_runs(const std::vector<ava::orchestration::ChildRunInfo>& runs);
  void record_child_run_terminal_summary(const ava::orchestration::ChildRunTerminalSummary& summary);

  [[nodiscard]] std::vector<MessageLine> visible_messages() const;

 private:
  [[nodiscard]] std::size_t max_scroll_offset() const;
  void append_message(MessageKind kind, std::string text);
  void append_assistant_delta(std::string_view delta);
  void refresh_interactive_dock();
  [[nodiscard]] bool active_dock_still_pending() const;
  [[nodiscard]] bool interactive_request_dismissed(const std::string& request_id) const;

  std::vector<MessageLine> messages_;
  std::string input_buffer_;
  std::string status_line_{"Ready. Type and press Enter to submit. q to quit."};
  std::string active_provider_{"unknown"};
  std::string active_model_{"unknown"};
  bool running_{false};
  bool quit_requested_{false};
  bool assistant_delta_open_{false};
  std::size_t scroll_offset_{0};
  std::size_t viewport_rows_{12};
  std::vector<std::string> input_history_;
  std::optional<std::size_t> history_cursor_;
  std::string history_draft_;
  InteractiveRequestStateView interactive_requests_;
  std::optional<InteractiveDockState> active_interactive_dock_;
  std::vector<std::string> dismissed_interactive_request_ids_;
  std::unordered_map<std::string, std::vector<std::string>> interactive_request_details_;
  std::unordered_map<std::string, bool> interactive_approval_can_approve_;
  std::vector<ChildRunStateView> child_runs_;
};

}  // namespace ava::tui
