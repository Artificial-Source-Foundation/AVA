#include "ava/tui/composer_internal.h"
#include "ava/tui/tool_cards.h"

#include <algorithm>
#include <cctype>

namespace ava::tui {
namespace detail {
namespace {

constexpr auto kBlockMinWidth = std::size_t{32};
constexpr auto kTurnSpacingMinWidth = std::size_t{44};

bool wide_blocks(std::size_t width)
{
  return width >= kBlockMinWidth;
}

std::vector<std::string> render_thinking_block(std::string const& text, std::size_t width);

std::string trim_left(std::string_view text)
{
  std::size_t start = 0;
  while (start < text.size() && std::isspace(static_cast<unsigned char>(text[start])) != 0) {
    ++start;
  }
  return std::string(text.substr(start));
}

bool is_blank(std::string_view text)
{
  return std::ranges::all_of(text, [](char ch) { return std::isspace(static_cast<unsigned char>(ch)) != 0; });
}

std::string trim_ascii(std::string text)
{
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

std::string lower_ascii(std::string_view text)
{
  std::string lowered;
  lowered.reserve(text.size());
  for (char const ch : text) lowered.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(ch))));
  return lowered;
}

std::string join_lines(std::vector<std::string> const& lines, std::size_t first)
{
  std::string joined;
  for (std::size_t index = first; index < lines.size(); ++index) {
    if (index > first) joined.push_back('\n');
    joined += lines[index];
  }
  return joined;
}

std::string remove_redacted_markers(std::string text)
{
  constexpr std::string_view marker = "[REDACTED]";
  for (auto found = text.find(marker); found != std::string::npos; found = text.find(marker, found)) {
    text.erase(found, marker.size());
  }
  return text;
}

std::vector<std::string> hard_wrap_sanitized(std::string_view text, std::size_t width)
{
  auto const content_width = std::max<std::size_t>(1, width);
  std::vector<std::string> wrapped;
  std::string current;
  std::size_t columns = 0;
  for (std::size_t index = 0; index < text.size();) {
    auto const byte = static_cast<unsigned char>(text[index]);
    auto const length = utf8_sequence_length(byte);
    char32_t codepoint = 0;
    auto const valid = decode_utf8_codepoint(text, index, length, codepoint);
    auto const chunk_length = valid ? length : std::size_t{1};
    auto const chunk_columns = valid ? codepoint_columns(codepoint) : std::size_t{1};
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

std::vector<std::string> words_in(std::string_view text)
{
  std::vector<std::string> words;
  for (std::size_t index = 0; index < text.size();) {
    while (index < text.size() && std::isspace(static_cast<unsigned char>(text[index])) != 0) {
      ++index;
    }
    auto const start = index;
    while (index < text.size() && std::isspace(static_cast<unsigned char>(text[index])) == 0) {
      ++index;
    }
    if (start < index) words.emplace_back(text.substr(start, index - start));
  }
  return words;
}

std::string text_span_sgr(Rendition const& rendition)
{
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

std::string render_inline_text(Text const& text)
{
  std::string rendered;
  for (auto const& run : text.runs) {
    if (std::holds_alternative<NewLine>(run)) {
      rendered.push_back(' ');
    } else if (auto const* string = std::get_if<String>(&run)) {
      rendered += string->text;
    } else if (auto const* span = std::get_if<TextSpan>(&run)) {
      auto const sgr = text_span_sgr(span->rendition);
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

std::string text_model_or(Text const& model, std::string const& fallback)
{
  if (text_empty(model)) return fallback;
  return to_plain_text(model);
}

void push_wrapped_word(std::vector<std::string>& output, std::string const& word, std::string const& next_prefix,
                       std::string& current, std::size_t& current_cols, std::size_t width)
{
  auto remaining = word;
  while (!remaining.empty()) {
    auto const prefix_cols = terminal_text_columns(current);
    auto const available = width > prefix_cols ? width - prefix_cols : std::size_t{1};
    auto pieces = hard_wrap_sanitized(remaining, available);
    auto const consumed = pieces.front().size();
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
                                                std::string const& first_prefix = {},
                                                std::string const& next_prefix = {})
{
  auto const content_width = std::max<std::size_t>(1, width);
  std::vector<std::string> output;
  std::string current = first_prefix;
  std::size_t current_cols = terminal_text_columns(current);
  auto const continuation = next_prefix.empty() ? std::string(terminal_text_columns(first_prefix), ' ') : next_prefix;
  auto const continuation_cols = terminal_text_columns(continuation);
  auto const words = words_in(text);
  if (words.empty()) {
    output.push_back(std::move(current));
    return output;
  }

  bool line_has_word = false;
  for (auto const& word : words) {
    auto const word_cols = terminal_text_columns(word);
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

std::string render_inline_markup(std::string_view sanitized_text)
{
  return render_inline_text(text_from_markdown(sanitized_text));
}

std::string render_wide_content(std::string_view border_sgr, std::string content, std::size_t width)
{
  auto line = std::string("  ") + std::string(border_sgr) + "│" + std::string(kSgrReset) + " " + std::move(content);
  return fit_line_preserving_sgr(std::move(line), width);
}

std::vector<std::string> render_narrow_assistant_lines(std::vector<std::string> const& content_lines, std::size_t width)
{
  std::vector<std::string> lines;
  auto const prefix = std::string("  ");
  auto const content_width = width > prefix.size() ? width - prefix.size() : std::size_t{1};
  bool in_code = false;
  for (auto const& content : content_lines) {
    auto const is_fence = content.rfind("```", 0) == 0;
    auto const format_inline_markup = !is_fence && !in_code;
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

std::vector<std::string> render_assistant_meta_lines(std::string const& meta, std::size_t width)
{
  if (meta.empty()) return {};
  auto const sanitized = sanitize_terminal_text(meta);
  if (!wide_blocks(width)) {
    auto line = std::string("     ") + std::string(kSgrAccent) + "* " + std::string(kSgrDim) + sanitized +
                std::string(kSgrReset);
    return {fit_line_preserving_sgr(std::move(line), width)};
  }
  auto line = std::string(kSgrAccent) + "* " + std::string(kSgrDim) + sanitized + std::string(kSgrReset);
  return {render_wide_content(kSgrMuted, std::move(line), width)};
}

std::vector<std::string> render_user_block(std::string const& text, std::size_t width)
{
  std::vector<std::string> lines;
  std::vector<std::string> plain_lines;
  auto const wide = wide_blocks(width);
  constexpr auto kBubblePaddingColumns = std::size_t{2};
  constexpr auto kWidePrefixColumns = std::size_t{5};
  auto const max_panel_columns = width > kWidePrefixColumns ? width - kWidePrefixColumns : std::size_t{1};
  auto const preferred_panel_columns = std::max<std::size_t>(18, (width * 3) / 4);
  auto const panel_budget = std::max<std::size_t>(1, std::min(max_panel_columns, preferred_panel_columns));
  auto const wrap_width =
      panel_budget > kBubblePaddingColumns * 2 ? panel_budget - (kBubblePaddingColumns * 2) : std::size_t{1};
  for (auto const& raw_line : split_lines(text)) {
    auto const sanitized = sanitize_terminal_text(raw_line);
    if (wide) {
      auto wrapped = wrap_words_with_prefix(sanitized, wrap_width);
      plain_lines.insert(plain_lines.end(), wrapped.begin(), wrapped.end());
    } else {
      plain_lines.push_back(sanitized);
    }
  }
  if (!wide) {
    for (auto const& part : plain_lines) {
      lines.push_back(
          fit_line_preserving_sgr(std::string(kSgrAccent) + "│" + std::string(kSgrReset) + " " + part, width));
    }
    return lines;
  }

  auto panel_text_columns = std::size_t{1};
  for (auto const& part : plain_lines) {
    panel_text_columns = std::max(panel_text_columns, std::min(wrap_width, terminal_text_columns(part)));
  }
  auto const panel_columns = std::min(max_panel_columns, panel_text_columns + (kBubblePaddingColumns * 2));
  auto const prefix = std::string("  ") + std::string(kSgrAccent) + "│" + std::string(kSgrReset) + "  ";
  for (auto const& part : plain_lines) {
    auto clipped = fit_line(part, panel_text_columns);
    auto const clipped_columns = terminal_text_columns(clipped);
    std::string content(kBubblePaddingColumns, ' ');
    content += std::string(kSgrTextDimmed) + std::move(clipped) + std::string(kSgrReset);
    if (clipped_columns < panel_text_columns) content += std::string(panel_text_columns - clipped_columns, ' ');
    content += std::string(kBubblePaddingColumns, ' ');
    auto panel = surface_line(kSgrComposerBg, std::move(content), panel_columns);
    lines.push_back(fit_line_preserving_sgr(prefix + std::move(panel), width));
  }
  return lines;
}

bool parse_bullet(std::string_view line, std::string& marker, std::string& body)
{
  auto const trimmed = trim_left(line);
  if (trimmed.size() >= 2 && (trimmed[0] == '-' || trimmed[0] == '*') && trimmed[1] == ' ') {
    marker = std::string(1, trimmed[0]) + " ";
    body = std::string(trimmed.substr(2));
    return true;
  }
  return false;
}

bool parse_numbered(std::string_view line, std::string& marker, std::string& body)
{
  auto const trimmed = trim_left(line);
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

bool parse_blockquote(std::string_view line, std::string& body)
{
  auto const trimmed = trim_left(line);
  if (trimmed.size() >= 2 && trimmed[0] == '>' && trimmed[1] == ' ') {
    body = std::string(trimmed.substr(2));
    return true;
  }
  return false;
}

std::vector<std::string> assistant_content_lines(std::string const& text, std::size_t content_width)
{
  std::vector<std::string> output;
  auto const raw_lines = split_lines(text);
  bool in_code = false;

  for (auto const& raw_line : raw_lines) {
    auto const sanitized = sanitize_terminal_text(raw_line);
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
      auto const indent = std::string(terminal_text_columns(marker), ' ');
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

std::vector<std::string> render_assistant_text_block(std::vector<std::string> const& content, std::size_t width)
{
  std::vector<std::string> lines;
  bool in_code = false;
  for (auto const& part : content) {
    auto const is_fence = part.rfind("```", 0) == 0;
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

std::vector<std::string> render_assistant_block(std::string const& text, std::string const& meta,
                                                std::string const& thinking, std::size_t width)
{
  auto const content_width = width > 4 ? width - 4 : std::size_t{1};
  auto const content = text.empty() ? std::vector<std::string>{} : assistant_content_lines(text, content_width);
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

std::vector<std::string> render_thinking_block(std::string const& text, std::size_t width)
{
  auto const visible_text = trim_ascii(remove_redacted_markers(text));
  if (visible_text.empty()) return {};

  auto const content_width = width > 4 ? width - 4 : std::size_t{1};
  std::vector<std::string> lines;
  bool first_content_line = true;
  for (auto const& raw_line : split_lines(visible_text)) {
    if (is_blank(raw_line)) {
      auto const blank = wide_blocks(width) ? render_wide_content(kSgrThinking, {}, width) : std::string{};
      lines.push_back(fit_line_preserving_sgr(blank, width));
      continue;
    }

    auto const sanitized = sanitize_terminal_text(raw_line);
    auto const first_prefix = first_content_line ? std::string("Thinking: ") : std::string{};
    auto const next_prefix = first_content_line ? std::string(first_prefix.size(), ' ') : std::string{};
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

std::string render_error_line(std::string const& text, std::size_t width)
{
  auto prefix = std::string("  ") + std::string(kSgrError) + '!' + std::string(kSgrReset) + " ";
  constexpr auto kPrefixCols = std::size_t{4};
  auto content_width = width > kPrefixCols ? width - kPrefixCols : 0;
  auto content = fit_line(sanitize_terminal_text(text), content_width);
  return prefix + content;
}

std::vector<std::string> render_error_detail_lines(std::string const& details, std::size_t width)
{
  if (details.empty()) return {};
  constexpr auto kMaxErrorDetailLines = std::size_t{8};
  auto raw_lines = split_lines(details);
  std::vector<std::string> lines;
  auto const prefix = wide_blocks(width) ? std::string("  │     ") : std::string("      ");
  auto const content_prefix = wide_blocks(width) ? std::string("  │       ") : std::string("        ");
  auto header = prefix + std::string(kSgrDim) + "details:" + std::string(kSgrReset);
  lines.push_back(fit_line_preserving_sgr(std::move(header), width));
  auto const visible = std::min(raw_lines.size(), kMaxErrorDetailLines);
  for (std::size_t index = 0; index < visible; ++index) {
    lines.push_back(fit_line_preserving_sgr(
        content_prefix + std::string(kSgrMuted) + sanitize_terminal_text(raw_lines[index]) + std::string(kSgrReset),
        width));
  }
  if (raw_lines.size() > visible) {
    auto hidden = content_prefix + std::string(kSgrDim) + "… " + std::to_string(raw_lines.size() - visible) +
                  " more detail lines" + std::string(kSgrReset);
    lines.push_back(fit_line_preserving_sgr(std::move(hidden), width));
  }
  return lines;
}

std::vector<std::string> render_error_block(std::string const& text, std::string const& meta, std::size_t width,
                                            bool details_visible)
{
  auto raw_lines = split_lines(text);
  auto summary = raw_lines.empty() ? std::string{} : raw_lines.front();
  if (summary.empty()) summary = "error";

  auto details = meta.empty() && raw_lines.size() > 1 ? join_lines(raw_lines, 1) : meta;
  std::vector<std::string> lines{render_error_line(summary, width)};
  if (details.empty()) return lines;

  if (!details_visible) {
    auto const prefix = wide_blocks(width) ? std::string("  │     ") : std::string("      ");
    auto hint = prefix + std::string(kSgrDim) + "details hidden · /details" + std::string(kSgrReset);
    lines.push_back(fit_line_preserving_sgr(std::move(hint), width));
    return lines;
  }

  auto detail_lines = render_error_detail_lines(details, width);
  lines.insert(lines.end(), detail_lines.begin(), detail_lines.end());
  return lines;
}

std::vector<std::string> render_compaction_block(std::string const& text, std::size_t width)
{
  auto const sanitized = sanitize_terminal_text(text);
  auto const first_prefix = std::string("  ") + std::string(kSgrWarning) + "compact" + std::string(kSgrReset) + " ";
  auto const next_prefix = std::string(terminal_text_columns(first_prefix), ' ');
  auto wrapped = wrap_words_with_prefix(sanitized, width, first_prefix, next_prefix);
  for (auto& line : wrapped) line = fit_line_preserving_sgr(std::move(line), width);
  return wrapped;
}

}  // namespace

std::string render_generic_line(std::string const& text, std::size_t width)
{
  auto prefix = std::string("  ") + std::string(kSgrDim) + "·" + std::string(kSgrReset) + " ";
  constexpr auto kPrefixCols = std::size_t{4};
  auto content_width = width > kPrefixCols ? width - kPrefixCols : 0;
  auto content = fit_line(sanitize_terminal_text(text), content_width);
  return prefix + content;
}

namespace {

bool is_context_gathering_tool(TranscriptItem const& item)
{
  if (!item.tool) return false;
  auto const name = lower_ascii(item.tool->name);
  return name == "read_file" || name == "glob" || name == "grep" || name == "list_directory" ||
         name == "lsp_diagnostics";
}

std::size_t context_tool_run_size(std::vector<TranscriptItem> const& transcript, std::size_t index)
{
  std::size_t count = 0;
  while (index + count < transcript.size() && is_context_gathering_tool(transcript[index + count])) ++count;
  return count;
}

std::string render_context_tool_group_heading(std::size_t count, std::size_t width)
{
  auto label = std::string(kSgrAccent) + "context gathering" + std::string(kSgrReset) + " " + std::string(kSgrDim) +
               "· " + std::to_string(count) + (count == 1 ? " tool" : " tools") + std::string(kSgrReset);
  if (wide_blocks(width)) return render_wide_content(kSgrMuted, std::move(label), width);
  return fit_line_preserving_sgr("  " + std::move(label), width);
}

std::vector<std::string> render_transcript_item_lines(TranscriptItem const& item, std::size_t width,
                                                      bool tool_details_visible, bool thinking_visible)
{
  if (item.tool) return render_tool_card(*item.tool, width, tool_details_visible);
  if (item.label == "you") return render_user_block(text_model_or(item.text_model, item.text), width);
  if (item.label == "ava") {
    auto assistant_text = item.text;
    if (assistant_text.empty() && !text_empty(item.text_model)) assistant_text = to_plain_text(item.text_model);
    auto thinking_text = thinking_visible ? text_model_or(item.thinking_model, item.thinking) : std::string{};
    return render_assistant_block(assistant_text, item.meta, thinking_text, width);
  }
  if (item.label == "thinking" && thinking_visible) {
    return render_thinking_block(text_model_or(item.text_model, item.text), width);
  }
  if (item.label == "compaction") return render_compaction_block(text_model_or(item.text_model, item.text), width);
  if (item.label == "error") {
    return render_error_block(text_model_or(item.text_model, item.text), item.meta, width, tool_details_visible);
  }

  std::vector<std::string> lines;
  auto const text = text_model_or(item.text_model, item.text);
  auto const text_lines = split_lines(text);
  for (auto const& part : text_lines) {
    for (auto const& wrapped : wrap_transcript_text(part, width)) {
      lines.push_back(render_generic_line(wrapped, width));
    }
  }
  return lines;
}

}  // namespace

std::vector<std::string> render_transcript_lines(std::vector<TranscriptItem> const& transcript, std::size_t width,
                                                  bool tool_details_visible, bool thinking_visible)
{
  std::vector<std::string> rendered_transcript;
  for (std::size_t index = 0; index < transcript.size(); ++index) {
    auto const& item = transcript[index];
    auto const should_space = width >= kTurnSpacingMinWidth && !rendered_transcript.empty() &&
                              (item.label == "you" || item.label == "ava" || item.tool);
    if (should_space) rendered_transcript.emplace_back();

    auto const context_run_size = context_tool_run_size(transcript, index);
    if (context_run_size >= 2 && (index == 0 || !is_context_gathering_tool(transcript[index - 1]))) {
      rendered_transcript.push_back(render_context_tool_group_heading(context_run_size, width));
    }

    auto block = render_transcript_item_lines(item, width, tool_details_visible, thinking_visible);
    rendered_transcript.insert(rendered_transcript.end(), block.begin(), block.end());
  }
  if (!rendered_transcript.empty() && wide_blocks(width)) rendered_transcript.emplace_back();
  return rendered_transcript;
}

std::vector<std::size_t> transcript_message_start_lines(std::vector<TranscriptItem> const& transcript,
                                                        std::size_t width, bool tool_details_visible,
                                                        bool thinking_visible)
{
  std::vector<std::size_t> starts;
  std::size_t cursor = 0;
  for (std::size_t index = 0; index < transcript.size(); ++index) {
    auto const& item = transcript[index];
    auto const should_space = width >= kTurnSpacingMinWidth && cursor > 0 &&
                              (item.label == "you" || item.label == "ava" || item.tool);
    if (should_space) ++cursor;

    auto const item_start = cursor;
    auto const context_run_size = context_tool_run_size(transcript, index);
    if (context_run_size >= 2 && (index == 0 || !is_context_gathering_tool(transcript[index - 1]))) ++cursor;

    auto block = render_transcript_item_lines(item, width, tool_details_visible, thinking_visible);
    if (!block.empty()) starts.push_back(item_start);
    cursor += block.size();
  }
  return starts;
}

std::vector<std::string> visible_transcript_lines(std::vector<std::string> const& rendered_transcript,
                                                  std::size_t width, std::size_t transcript_height,
                                                  std::size_t transcript_scroll_offset)
{
  static_cast<void>(width);
  std::vector<std::string> visible_transcript;
  if (rendered_transcript.size() > transcript_height && transcript_height > 0) {
    auto const visible_count = transcript_height;
    auto const max_offset = rendered_transcript.size() > visible_count ? rendered_transcript.size() - visible_count : 0;
    auto const scroll_offset = std::min(transcript_scroll_offset, max_offset);
    auto const end = rendered_transcript.size() - scroll_offset;
    auto const start = end > visible_count ? end - visible_count : 0;
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

std::string to_string(ToolTimelineStatus status)
{
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
