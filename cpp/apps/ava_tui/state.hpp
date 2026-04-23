#pragma once

#include <cstddef>
#include <optional>
#include <string>
#include <vector>

#include "ava/agent/runtime.hpp"

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

class AppState {
 public:
  [[nodiscard]] const std::vector<MessageLine>& messages() const { return messages_; }
  [[nodiscard]] const std::string& input_buffer() const { return input_buffer_; }
  [[nodiscard]] const std::string& status_line() const { return status_line_; }
  [[nodiscard]] bool running() const { return running_; }
  [[nodiscard]] bool quit_requested() const { return quit_requested_; }
  [[nodiscard]] std::size_t scroll_offset() const { return scroll_offset_; }
  [[nodiscard]] std::size_t viewport_rows() const { return viewport_rows_; }

  void request_quit() { quit_requested_ = true; }
  void set_running(bool value) { running_ = value; }
  void set_status_line(std::string text) { status_line_ = std::move(text); }

  void insert_text(const std::string& text);
  void backspace();

  [[nodiscard]] std::optional<std::string> take_submission();
  void apply_agent_event(const ava::agent::AgentEvent& event);

  void scroll_up(std::size_t amount);
  void scroll_down(std::size_t amount);
  void page_up();
  void page_down();
  void set_viewport_rows(std::size_t rows);

  [[nodiscard]] std::vector<MessageLine> visible_messages() const;

 private:
  [[nodiscard]] std::size_t max_scroll_offset() const;
  void append_message(MessageKind kind, std::string text);

  std::vector<MessageLine> messages_;
  std::string input_buffer_;
  std::string status_line_{"Ready. Type and press Enter to submit. q to quit."};
  bool running_{false};
  bool quit_requested_{false};
  std::size_t scroll_offset_{0};
  std::size_t viewport_rows_{12};
};

}  // namespace ava::tui
