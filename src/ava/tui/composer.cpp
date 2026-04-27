#include "ava/tui/composer.h"

#include <algorithm>
#include <sstream>

namespace ava::tui {
namespace {

constexpr std::size_t kMinWidth = 20;
constexpr std::size_t kMinHeight = 8;
constexpr std::size_t kHeaderFooterLines = 5;
constexpr std::string_view kHideCursor = "\x1b[?25l";
constexpr std::string_view kShowCursor = "\x1b[?25h";
constexpr std::string_view kHomeCursor = "\x1b[H";
constexpr std::string_view kClearLine = "\x1b[2K";

bool is_utf8_continuation(unsigned char byte) { return (byte & 0xC0U) == 0x80U; }

std::size_t utf8_sequence_length(unsigned char byte) {
  if ((byte & 0x80U) == 0) return 1;
  if ((byte & 0xE0U) == 0xC0U) return 2;
  if ((byte & 0xF0U) == 0xE0U) return 3;
  if ((byte & 0xF8U) == 0xF0U) return 4;
  return 0;
}

bool decode_utf8_codepoint(std::string_view text, std::size_t start, std::size_t length, char32_t& codepoint) {
  if (start + length > text.size() || length == 0) return false;
  const auto first = static_cast<unsigned char>(text[start]);
  if (length == 1) {
    codepoint = first;
    return true;
  }
  codepoint = first & ((1U << (7 - length - 1)) - 1U);
  for (std::size_t offset = 1; offset < length; ++offset) {
    const auto byte = static_cast<unsigned char>(text[start + offset]);
    if (!is_utf8_continuation(byte)) return false;
    codepoint = (codepoint << 6U) | (byte & 0x3FU);
  }
  return true;
}

std::string fit_line(std::string text, std::size_t width) {
  if (width == 0) return {};
  text = sanitize_terminal_text(text);
  if (text.size() <= width) return text;
  if (width <= 3) {
    auto cut = std::min(width, text.size());
    while (cut > 0 && cut < text.size() && is_utf8_continuation(static_cast<unsigned char>(text[cut]))) {
      --cut;
    }
    return text.substr(0, cut);
  }
  auto cut = width - 3;
  while (cut > 0 && cut < text.size() && is_utf8_continuation(static_cast<unsigned char>(text[cut]))) {
    --cut;
  }
  text.resize(cut);
  text += "...";
  return text;
}

}  // namespace

std::string sanitize_terminal_text(std::string_view text) {
  std::string sanitized;
  sanitized.reserve(text.size());
  for (std::size_t index = 0; index < text.size();) {
    const auto byte = static_cast<unsigned char>(text[index]);
    if (byte < 0x20 || byte == 0x7F) {
      sanitized.push_back(byte == '\t' ? '\t' : '?');
      ++index;
      continue;
    }

    const auto length = utf8_sequence_length(byte);
    char32_t codepoint = 0;
    if (!decode_utf8_codepoint(text, index, length, codepoint)) {
      sanitized.push_back('?');
      ++index;
      continue;
    }

    if (codepoint >= 0x80 && codepoint <= 0x9F) {
      sanitized.push_back('?');
    } else {
      sanitized.append(text.substr(index, length));
    }
    index += length;
  }
  return sanitized;
}

std::vector<std::string> split_lines(std::string_view text) {
  std::vector<std::string> lines;
  std::size_t start = 0;
  while (start <= text.size()) {
    const auto end = text.find('\n', start);
    if (end == std::string_view::npos) {
      lines.emplace_back(text.substr(start));
      break;
    }
    lines.emplace_back(text.substr(start, end - start));
    start = end + 1;
  }
  if (lines.empty()) lines.emplace_back();
  return lines;
}

std::vector<std::string> render_composer(const ComposerSnapshot& snapshot) {
  const auto width = std::max<std::size_t>(kMinWidth, snapshot.width);
  const auto height = std::max<std::size_t>(kMinHeight, snapshot.height);
  std::vector<std::string> lines;
  lines.reserve(height);

  lines.push_back(fit_line("AVA 0.1  mode=" + snapshot.mode + "  provider=" + snapshot.provider + "  model=" +
                               snapshot.model,
                           width));
  lines.push_back(fit_line("session=" + snapshot.session_id + "  Tab mode  Enter send  Ctrl+C clear/quit", width));

  const auto transcript_height = height > kHeaderFooterLines ? height - kHeaderFooterLines : 1;
  std::vector<std::string> rendered_transcript;
  for (const auto& item : snapshot.transcript) {
    const auto prefix = item.label.empty() ? std::string{} : item.label + ": ";
    for (const auto& part : split_lines(item.text)) {
      rendered_transcript.push_back(fit_line(prefix + part, width));
    }
  }
  const auto start = rendered_transcript.size() > transcript_height ? rendered_transcript.size() - transcript_height : 0;
  for (std::size_t index = start; index < rendered_transcript.size(); ++index) {
    lines.push_back(rendered_transcript[index]);
  }
  while (lines.size() < 2 + transcript_height) {
    lines.emplace_back();
  }

  lines.push_back(fit_line(snapshot.status, width));
  lines.push_back(fit_line("[" + snapshot.mode + "] ava> " + snapshot.input, width));
  return lines;
}

std::string render_screen(const ComposerSnapshot& snapshot) {
  std::ostringstream output;
  output << kHideCursor << kHomeCursor;
  for (const auto& line : render_composer(snapshot)) {
    output << kClearLine << line << "\r\n";
  }
  output << kShowCursor;
  return output.str();
}

}  // namespace ava::tui
