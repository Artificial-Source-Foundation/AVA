#pragma once

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

namespace ava::tui {

struct TranscriptItem {
  std::string label;
  std::string text;
};

struct ComposerSnapshot {
  std::string mode;
  std::string provider;
  std::string model;
  std::string session_id;
  std::string input;
  std::string status;
  std::vector<TranscriptItem> transcript;
  std::size_t width = 80;
  std::size_t height = 24;
};

[[nodiscard]] std::vector<std::string> render_composer(const ComposerSnapshot& snapshot);
[[nodiscard]] std::string render_screen(const ComposerSnapshot& snapshot);
[[nodiscard]] std::string sanitize_terminal_text(std::string_view text);
[[nodiscard]] std::vector<std::string> split_lines(std::string_view text);

}  // namespace ava::tui
