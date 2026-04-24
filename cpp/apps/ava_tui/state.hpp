#pragma once

#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "ava/agent/runtime.hpp"
#include "ava/control_plane/interactive.hpp"

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

class AppState {
 public:
  [[nodiscard]] const std::vector<MessageLine>& messages() const { return messages_; }
  [[nodiscard]] const std::string& input_buffer() const { return input_buffer_; }
  [[nodiscard]] const std::string& status_line() const { return status_line_; }
  [[nodiscard]] bool running() const { return running_; }
  [[nodiscard]] bool quit_requested() const { return quit_requested_; }
  [[nodiscard]] std::size_t scroll_offset() const { return scroll_offset_; }
  [[nodiscard]] std::size_t viewport_rows() const { return viewport_rows_; }
  [[nodiscard]] const InteractiveRequestStateView& interactive_requests() const { return interactive_requests_; }

  void request_quit() { quit_requested_ = true; }
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
  void clear_interactive_requests();

  [[nodiscard]] std::vector<MessageLine> visible_messages() const;

 private:
  [[nodiscard]] std::size_t max_scroll_offset() const;
  void append_message(MessageKind kind, std::string text);
  void append_assistant_delta(std::string_view delta);

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
};

}  // namespace ava::tui
