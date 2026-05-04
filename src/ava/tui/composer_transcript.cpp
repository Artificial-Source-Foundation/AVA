#include <algorithm>
#include <cctype>

#include "ava/tui/composer_internal.h"
#include "ava/tui/tool_cards.h"

namespace ava::tui {
namespace detail {
namespace {

constexpr auto kBlockMinWidth = std::size_t{32};
constexpr auto kTurnSpacingMinWidth = std::size_t{44};

bool wide_blocks(std::size_t width) { return width >= kBlockMinWidth; }

std::vector<std::string> render_thinking_block(const std::string& text, std::size_t width);

std::string trim_left(std::string_view text) {
  std::size_t start = 0;
  while (start < text.size() && std::isspace(static_cast<unsigned char>(text[start])) != 0) {
    ++start;
  }
  return std::string(text.substr(start));
}

bool is_blank(std::string_view text) {
  return std::ranges::all_of(text, [](char ch) { return std::isspace(static_cast<unsigned char>(ch)) != 0; });
}

std::string trim_ascii(std::string text) {
  auto begin = text.begin();
  while (begin != text.end() && std::isspace(static_cast<unsigned char>(*begin)) != 0) {
    ++begin;
  }
  auto end = text.end();
  while (end != begin && std::isspace(static_cast<unsigned char>(*(end - 1))) != 0) {
    --end;
  }
  return std::string(begin, end);
}

std::string remove_redacted_markers(std::string text) {
  constexpr std::string_view marker = "[REDACTED]";
  for (auto found = text.find(marker); found != std::string::npos; found = text.find(marker, found)) {
    text.erase(found, marker.size());
  }
  return text;
}

std::vector<std::string> hard_wrap_sanitized(std::string_view text, std::size_t width) {
  const auto content_width = std::max<std::size_t>(1, width);
  std::vector<std::string> wrapped;
  std::string current;
  std::size_t columns = 0;
  for (std::size_t index = 0; index < text.size();) {
    const auto byte = static_cast<unsigned char>(text[index]);
    const auto length = utf8_sequence_length(byte);
    char32_t codepoint = 0;
    const auto valid = decode_utf8_codepoint(text, index, length, codepoint);
    const auto chunk_length = valid ? length : std::size_t{1};
    const auto chunk_columns = valid ? codepoint_columns(codepoint) : std::size_t{1};
    if (columns + chunk_columns > content_width && !current.empty()) {
      wrapped.push_back(std::move(current));
      current.clear();
      columns = 0;
    }
    current.append(text.substr(index, chunk_length));
    columns += chunk_columns;
    index += chunk_length;
  }
  if (!current.empty() || wrapped.empty()) wrapped.push_back(std::move(current));
  return wrapped;
}

std::vector<std::string> words_in(std::string_view text) {
  std::vector<std::string> words;
  for (std::size_t index = 0; index < text.size();) {
    while (index < text.size() && std::isspace(static_cast<unsigned char>(text[index])) != 0) {
      ++index;
    }
    const auto start = index;
    while (index < text.size() && std::isspace(static_cast<unsigned char>(text[index])) == 0) {
      ++index;
    }
    if (start < index) words.emplace_back(text.substr(start, index - start));
  }
  return words;
}

std::string text_span_sgr(const Rendition& rendition) {
  std::string sgr;
  if (rendition.bold) sgr += std::string(kSgrBold);
  if (rendition.code) {
    sgr += std::string(kSgrWarning);
  } else if (rendition.color == TextColorRole::Thinking) {
    sgr += std::string(kSgrThinking);
  } else if (rendition.color == TextColorRole::Muted || rendition.dim) {
    sgr += std::string(kSgrDim);
  } else if (rendition.color == TextColorRole::Success || rendition.color == TextColorRole::Added) {
    sgr += std::string(kSgrSuccess);
  } else if (rendition.color == TextColorRole::Error || rendition.color == TextColorRole::Removed) {
    sgr += std::string(kSgrError);
  } else if (rendition.color == TextColorRole::Warning || rendition.color == TextColorRole::Code) {
    sgr += std::string(kSgrWarning);
  } else if (rendition.color == TextColorRole::Accent) {
    sgr += std::string(kSgrAccent);
  }
  return sgr;
}

std::string render_inline_text(const Text& text) {
  std::string rendered;
  for (const auto& run : text.runs) {
    if (std::holds_alternative<NewLine>(run)) {
      rendered.push_back(' ');
    } else if (const auto* string = std::get_if<String>(&run)) {
      rendered += string->text;
    } else if (const auto* span = std::get_if<TextSpan>(&run)) {
      const auto sgr = text_span_sgr(span->rendition);
      if (sgr.empty()) {
        rendered += span->text;
      } else {
        rendered += sgr;
        rendered += span->text;
        rendered += std::string(kSgrReset);
      }
    }
  }
  return rendered;
}

std::string text_model_or(const Text& model, const std::string& fallback) {
  if (text_empty(model)) return fallback;
  return to_plain_text(model);
}

void push_wrapped_word(std::vector<std::string>& output, const std::string& word, const std::string& next_prefix,
                       std::string& current, std::size_t& current_cols, std::size_t width) {
  auto remaining = word;
  while (!remaining.empty()) {
    const auto prefix_cols = terminal_text_columns(current);
    const auto available = width > prefix_cols ? width - prefix_cols : std::size_t{1};
    auto pieces = hard_wrap_sanitized(remaining, available);
    const auto consumed = pieces.front().size();
    current += pieces.front();
    current_cols = terminal_text_columns(current);
    if (pieces.size() == 1) return;
    output.push_back(fit_line_preserving_sgr(std::move(current), width));
    current = next_prefix;
    current_cols = terminal_text_columns(current);
    remaining.erase(0, consumed);
  }
}

std::vector<std::string> wrap_words_with_prefix(std::string_view text, std::size_t width,
                                                const std::string& first_prefix = {},
                                                const std::string& next_prefix = {}) {
  const auto content_width = std::max<std::size_t>(1, width);
  std::vector<std::string> output;
  std::string current = first_prefix;
  std::size_t current_cols = terminal_text_columns(current);
  const auto continuation = next_prefix.empty() ? std::string(terminal_text_columns(first_prefix), ' ') : next_prefix;
  const auto continuation_cols = terminal_text_columns(continuation);
  const auto words = words_in(text);
  if (words.empty()) {
    output.push_back(std::move(current));
    return output;
  }

  bool line_has_word = false;
  for (const auto& word : words) {
    const auto word_cols = terminal_text_columns(word);
    if (line_has_word && current_cols + 1 + word_cols <= content_width) {
      current.push_back(' ');
      ++current_cols;
      current += word;
      current_cols += word_cols;
      continue;
    }

    if (line_has_word) {
      output.push_back(std::move(current));
      current = continuation;
      current_cols = continuation_cols;
      line_has_word = false;
    }
    if (current_cols + word_cols <= content_width) {
      current += word;
      current_cols += word_cols;
      line_has_word = true;
    } else {
      push_wrapped_word(output, word, continuation, current, current_cols, content_width);
      line_has_word = current_cols > continuation_cols;
    }
  }
  output.push_back(std::move(current));
  return output;
}

std::string render_inline_markup(std::string_view sanitized_text) {
  return render_inline_text(text_from_markdown(sanitized_text));
}

std::string render_wide_content(std::string_view border_sgr, std::string content, std::size_t width) {
  auto line = std::string("  ") + std::string(border_sgr) + "│" + std::string(kSgrReset) + " " + std::move(content);
  return fit_line_preserving_sgr(std::move(line), width);
}

std::vector<std::string> render_narrow_assistant_lines(const std::vector<std::string>& content_lines,
                                                       std::size_t width) {
  std::vector<std::string> lines;
  const auto prefix = std::string("  ");
  const auto content_width = width > prefix.size() ? width - prefix.size() : std::size_t{1};
  bool in_code = false;
  for (const auto& content : content_lines) {
    const auto is_fence = content.rfind("```", 0) == 0;
    const auto format_inline_markup = !is_fence && !in_code;
    auto wrapped = wrap_words_with_prefix(content, content_width);
    for (auto& part : wrapped) {
      auto rendered_part = format_inline_markup ? render_inline_markup(part) : part;
      auto line = prefix + std::move(rendered_part);
      lines.push_back(fit_line_preserving_sgr(std::move(line), width));
    }
    if (is_fence) in_code = !in_code;
  }
  if (lines.empty()) lines.push_back(fit_line(prefix, width));
  return lines;
}

std::vector<std::string> render_assistant_meta_lines(const std::string& meta, std::size_t width) {
  if (meta.empty()) return {};
  const auto sanitized = sanitize_terminal_text(meta);
  if (!wide_blocks(width)) {
    auto line = std::string("     ") + std::string(kSgrAccent) + "* " + std::string(kSgrDim) + sanitized +
                std::string(kSgrReset);
    return {fit_line_preserving_sgr(std::move(line), width)};
  }
  auto line = std::string(kSgrAccent) + "* " + std::string(kSgrDim) + sanitized + std::string(kSgrReset);
  return {render_wide_content(kSgrMuted, std::move(line), width)};
}

std::vector<std::string> render_user_block(const std::string& text, std::size_t width) {
  std::vector<std::string> lines;
  std::vector<std::string> plain_lines;
  for (const auto& raw_line : split_lines(text)) {
    const auto sanitized = sanitize_terminal_text(raw_line);
    if (wide_blocks(width)) {
      const auto content_width = width > 4 ? width - 4 : std::size_t{1};
      auto wrapped = wrap_words_with_prefix(sanitized, content_width);
      plain_lines.insert(plain_lines.end(), wrapped.begin(), wrapped.end());
    } else {
      plain_lines.push_back(sanitized);
    }
  }
  if (!wide_blocks(width)) {
    for (const auto& part : plain_lines) {
      lines.push_back(
          fit_line_preserving_sgr(std::string(kSgrAccent) + "│" + std::string(kSgrReset) + " " + part, width));
    }
    return lines;
  }

  for (const auto& part : plain_lines) {
    const auto content_width = width > 5 ? width - 5 : std::size_t{1};
    auto content =
        std::string(" ") + std::string(kSgrTextDimmed) + fit_line(part, content_width) + std::string(kSgrReset);
    auto panel = surface_line(kSgrComposerBg, std::move(content), width > 3 ? width - 3 : std::size_t{1});
    lines.push_back(fit_line_preserving_sgr(
        std::string("  ") + std::string(kSgrAccent) + "│" + std::string(kSgrReset) + std::move(panel), width));
  }
  return lines;
}

bool parse_bullet(std::string_view line, std::string& marker, std::string& body) {
  const auto trimmed = trim_left(line);
  if (trimmed.size() >= 2 && (trimmed[0] == '-' || trimmed[0] == '*') && trimmed[1] == ' ') {
    marker = std::string(1, trimmed[0]) + " ";
    body = std::string(trimmed.substr(2));
    return true;
  }
  return false;
}

bool parse_numbered(std::string_view line, std::string& marker, std::string& body) {
  const auto trimmed = trim_left(line);
  std::size_t index = 0;
  while (index < trimmed.size() && std::isdigit(static_cast<unsigned char>(trimmed[index])) != 0) {
    ++index;
  }
  if (index == 0 || index + 1 >= trimmed.size() || trimmed[index] != '.' || trimmed[index + 1] != ' ') {
    return false;
  }
  marker = std::string(trimmed.substr(0, index + 2));
  body = std::string(trimmed.substr(index + 2));
  return true;
}

bool parse_blockquote(std::string_view line, std::string& body) {
  const auto trimmed = trim_left(line);
  if (trimmed.size() >= 2 && trimmed[0] == '>' && trimmed[1] == ' ') {
    body = std::string(trimmed.substr(2));
    return true;
  }
  return false;
}

std::vector<std::string> assistant_content_lines(const std::string& text, std::size_t content_width) {
  std::vector<std::string> output;
  const auto raw_lines = split_lines(text);
  bool in_code = false;

  for (const auto& raw_line : raw_lines) {
    const auto sanitized = sanitize_terminal_text(raw_line);
    if (sanitized.rfind("```", 0) == 0) {
      in_code = !in_code;
      auto fence_label = std::string("```");
      if (sanitized.size() > 3) fence_label += " " + trim_left(std::string_view(sanitized).substr(3));
      output.push_back(fence_label);
      continue;
    }
    if (in_code) {
      auto wrapped = hard_wrap_sanitized(sanitized, content_width > 2 ? content_width - 2 : std::size_t{1});
      for (auto& part : wrapped) {
        output.push_back("  " + std::move(part));
      }
      continue;
    }
    if (is_blank(sanitized)) {
      if (!output.empty() && !output.back().empty()) output.emplace_back();
      continue;
    }

    std::string marker;
    std::string body;
    if (parse_bullet(sanitized, marker, body) || parse_numbered(sanitized, marker, body)) {
      const auto indent = std::string(terminal_text_columns(marker), ' ');
      auto wrapped = wrap_words_with_prefix(body, content_width, marker, indent);
      output.insert(output.end(), wrapped.begin(), wrapped.end());
      continue;
    }
    if (parse_blockquote(sanitized, body)) {
      auto wrapped = wrap_words_with_prefix(body, content_width, "> ", "  ");
      output.insert(output.end(), wrapped.begin(), wrapped.end());
      continue;
    }

    auto wrapped = wrap_words_with_prefix(sanitized, content_width);
    output.insert(output.end(), wrapped.begin(), wrapped.end());
  }
  if (output.empty()) output.emplace_back();
  return output;
}

std::vector<std::string> render_assistant_text_block(const std::vector<std::string>& content, std::size_t width) {
  std::vector<std::string> lines;
  bool in_code = false;
  for (const auto& part : content) {
    const auto is_fence = part.rfind("```", 0) == 0;
    std::string rendered;
    if (is_fence || in_code) {
      rendered = std::string(kSgrDim) + part + std::string(kSgrReset);
    } else {
      rendered = render_inline_markup(part);
    }
    lines.push_back(fit_line_preserving_sgr(std::string("  ") + std::move(rendered), width));
    if (is_fence) in_code = !in_code;
  }
  return lines;
}

std::vector<std::string> render_assistant_block(const std::string& text, const std::string& meta,
                                                const std::string& thinking, std::size_t width) {
  const auto content_width = width > 4 ? width - 4 : std::size_t{1};
  const auto content = text.empty() ? std::vector<std::string>{} : assistant_content_lines(text, content_width);
  std::vector<std::string> lines;

  if (!thinking.empty()) {
    auto meta_lines = render_assistant_meta_lines(meta, width);
    lines.insert(lines.end(), meta_lines.begin(), meta_lines.end());
    auto thinking_lines = render_thinking_block(thinking, width);
    lines.insert(lines.end(), thinking_lines.begin(), thinking_lines.end());
    if (!content.empty()) lines.emplace_back();
  }

  if (!content.empty()) {
    auto text_lines = wide_blocks(width) ? render_assistant_text_block(content, width)
                                         : render_narrow_assistant_lines(content, width);
    lines.insert(lines.end(), text_lines.begin(), text_lines.end());
  }

  if (thinking.empty()) {
    auto meta_lines = render_assistant_meta_lines(meta, width);
    lines.insert(lines.end(), meta_lines.begin(), meta_lines.end());
  }

  return lines;
}

std::vector<std::string> render_thinking_block(const std::string& text, std::size_t width) {
  const auto visible_text = trim_ascii(remove_redacted_markers(text));
  if (visible_text.empty()) return {};

  const auto content_width = width > 4 ? width - 4 : std::size_t{1};
  std::vector<std::string> lines;
  bool first_content_line = true;
  for (const auto& raw_line : split_lines(visible_text)) {
    if (is_blank(raw_line)) {
      const auto blank = wide_blocks(width) ? render_wide_content(kSgrThinking, {}, width) : std::string{};
      lines.push_back(fit_line_preserving_sgr(blank, width));
      continue;
    }

    const auto sanitized = sanitize_terminal_text(raw_line);
    const auto first_prefix = first_content_line ? std::string("Thinking: ") : std::string{};
    const auto next_prefix = first_content_line ? std::string(first_prefix.size(), ' ') : std::string{};
    auto wrapped = wrap_words_with_prefix(sanitized, content_width, first_prefix, next_prefix);
    bool first_wrapped_line = true;
    for (auto& part : wrapped) {
      std::string styled;
      if (first_content_line && first_wrapped_line && part.rfind("Thinking: ", 0) == 0) {
        styled = std::string(kSgrThinking) + "Thinking:" + std::string(kSgrReset) + " " + std::string(kSgrThinking) +
                 part.substr(std::string_view("Thinking: ").size()) + std::string(kSgrReset);
      } else {
        styled = std::string(kSgrThinking) + std::move(part) + std::string(kSgrReset);
      }
      if (wide_blocks(width)) {
        lines.push_back(render_wide_content(kSgrThinking, std::move(styled), width));
      } else {
        lines.push_back(fit_line_preserving_sgr(std::move(styled), width));
      }
      first_wrapped_line = false;
    }
    first_content_line = false;
  }
  return lines;
}

std::string render_error_line(const std::string& text, std::size_t width) {
  auto prefix = std::string("  ") + std::string(kSgrError) + '!' + std::string(kSgrReset) + " ";
  constexpr auto kPrefixCols = std::size_t{4};
  auto content_width = width > kPrefixCols ? width - kPrefixCols : 0;
  auto content = fit_line(sanitize_terminal_text(text), content_width);
  return prefix + content;
}

}  // namespace

std::string render_generic_line(const std::string& text, std::size_t width) {
  auto prefix = std::string("  ") + std::string(kSgrDim) + "·" + std::string(kSgrReset) + " ";
  constexpr auto kPrefixCols = std::size_t{4};
  auto content_width = width > kPrefixCols ? width - kPrefixCols : 0;
  auto content = fit_line(sanitize_terminal_text(text), content_width);
  return prefix + content;
}

std::vector<std::string> render_transcript_lines(const std::vector<TranscriptItem>& transcript, std::size_t width,
                                                 bool tool_details_visible, bool thinking_visible) {
  std::vector<std::string> rendered_transcript;
  for (const auto& item : transcript) {
    const auto should_space = width >= kTurnSpacingMinWidth && !rendered_transcript.empty() &&
                              (item.label == "you" || item.label == "ava" || item.tool);
    if (should_space) rendered_transcript.emplace_back();
    if (item.tool) {
      auto card = render_tool_card(*item.tool, width, tool_details_visible);
      rendered_transcript.insert(rendered_transcript.end(), card.begin(), card.end());
      continue;
    }
    if (item.label == "you") {
      auto block = render_user_block(text_model_or(item.text_model, item.text), width);
      rendered_transcript.insert(rendered_transcript.end(), block.begin(), block.end());
    } else if (item.label == "ava") {
      auto assistant_text = item.text;
      if (assistant_text.empty() && !text_empty(item.text_model)) assistant_text = to_plain_text(item.text_model);
      auto thinking_text = thinking_visible ? text_model_or(item.thinking_model, item.thinking) : std::string{};
      auto block = render_assistant_block(assistant_text, item.meta, thinking_text, width);
      rendered_transcript.insert(rendered_transcript.end(), block.begin(), block.end());
    } else if (item.label == "thinking" && thinking_visible) {
      auto block = render_thinking_block(text_model_or(item.text_model, item.text), width);
      rendered_transcript.insert(rendered_transcript.end(), block.begin(), block.end());
    } else {
      const auto text = text_model_or(item.text_model, item.text);
      const auto text_lines = split_lines(text);
      for (const auto& part : text_lines) {
        for (const auto& wrapped : wrap_transcript_text(part, width)) {
          if (item.label == "error") {
            rendered_transcript.push_back(render_error_line(wrapped, width));
          } else {
            rendered_transcript.push_back(render_generic_line(wrapped, width));
          }
        }
      }
    }
  }
  return rendered_transcript;
}

std::vector<std::string> visible_transcript_lines(const std::vector<std::string>& rendered_transcript,
                                                  std::size_t width, std::size_t transcript_height,
                                                  std::size_t transcript_scroll_offset) {
  static_cast<void>(width);
  std::vector<std::string> visible_transcript;
  if (rendered_transcript.size() > transcript_height && transcript_height > 0) {
    const auto visible_count = transcript_height;
    const auto max_offset = rendered_transcript.size() > visible_count ? rendered_transcript.size() - visible_count : 0;
    const auto scroll_offset = std::min(transcript_scroll_offset, max_offset);
    const auto end = rendered_transcript.size() - scroll_offset;
    const auto start = end > visible_count ? end - visible_count : 0;
    for (std::size_t index = start; index < end; ++index) {
      visible_transcript.push_back(rendered_transcript[index]);
    }
  } else if (transcript_height > 0) {
    for (std::size_t index = 0; index < rendered_transcript.size(); ++index) {
      visible_transcript.push_back(rendered_transcript[index]);
    }
  }
  return visible_transcript;
}

}  // namespace detail

std::string to_string(ToolTimelineStatus status) {
  switch (status) {
    case ToolTimelineStatus::Running:
      return "running";
    case ToolTimelineStatus::Success:
      return "success";
    case ToolTimelineStatus::Error:
      return "error";
  }
  return "unknown";
}

}  // namespace ava::tui
