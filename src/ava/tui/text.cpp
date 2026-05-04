#include "ava/tui/text.h"

#include <utility>

#include "ava/core/error.h"

namespace ava::tui {
namespace {

bool starts_fence(std::string_view line)
{
  return line.starts_with("```");
}

std::size_t heading_marker_size(std::string_view line)
{
  std::size_t marker_size = 0;
  while (marker_size < line.size() && marker_size < 6 && line[marker_size] == '#') {
    ++marker_size;
  }
  if (marker_size == 0 || marker_size >= line.size() || line[marker_size] != ' ') return 0;
  return marker_size + 1;
}

void append_plain_segment(Text& text, std::string_view value)
{
  if (value.empty()) return;
  text.runs.push_back(String{.text = std::string(value)});
}

void append_markdown_inline(Text& text, std::string_view line)
{
  std::size_t plain_start = 0;
  for (std::size_t index = 0; index < line.size();) {
    if (line[index] == '[') {
      auto const label_end = line.find("](", index + 1);
      if (label_end != std::string_view::npos) {
        auto const target_end = line.find(')', label_end + 2);
        if (target_end != std::string_view::npos && label_end > index + 1 && target_end > label_end + 2) {
          append_plain_segment(text, line.substr(plain_start, index - plain_start));
          static_cast<void>(append_span(text, std::string(line.substr(index + 1, label_end - index - 1)),
                                        Rendition{.underline = true, .color = TextColorRole::Accent}));
          append_plain_segment(text, " (");
          append_plain_segment(text, line.substr(label_end + 2, target_end - label_end - 2));
          append_plain_segment(text, ")");
          index = target_end + 1;
          plain_start = index;
          continue;
        }
      }
    }
    if (index + 1 < line.size() && line[index] == '*' && line[index + 1] == '*') {
      auto const end = line.find("**", index + 2);
      if (end != std::string_view::npos && end > index + 2) {
        append_plain_segment(text, line.substr(plain_start, index - plain_start));
        static_cast<void>(
            append_span(text, std::string(line.substr(index + 2, end - index - 2)), Rendition{.bold = true}));
        index = end + 2;
        plain_start = index;
        continue;
      }
    }
    if (line[index] == '*') {
      auto const end = line.find('*', index + 1);
      if (end != std::string_view::npos && end > index + 1) {
        append_plain_segment(text, line.substr(plain_start, index - plain_start));
        static_cast<void>(
            append_span(text, std::string(line.substr(index + 1, end - index - 1)), Rendition{.italic = true}));
        index = end + 1;
        plain_start = index;
        continue;
      }
    }
    if (line[index] == '`') {
      auto const end = line.find('`', index + 1);
      if (end != std::string_view::npos && end > index + 1) {
        append_plain_segment(text, line.substr(plain_start, index - plain_start));
        static_cast<void>(append_span(text, std::string(line.substr(index + 1, end - index - 1)),
                                      Rendition{.code = true, .color = TextColorRole::Code}));
        index = end + 1;
        plain_start = index;
        continue;
      }
    }
    ++index;
  }
  append_plain_segment(text, line.substr(plain_start));
}

}  // namespace

bool text_run_has_embedded_newline(std::string_view text)
{
  return text.find('\n') != std::string_view::npos || text.find('\r') != std::string_view::npos;
}

bool text_empty(Text const& text)
{
  return text.runs.empty();
}

ava::core::VoidResult append_string(Text& text, std::string value)
{
  if (text_run_has_embedded_newline(value)) {
    return std::unexpected(ava::core::Error(ava::core::ErrorCategory::InvalidArgument,
                                            "text string run must not contain embedded newlines"));
  }
  if (!value.empty()) text.runs.push_back(String{.text = std::move(value)});
  return {};
}

ava::core::VoidResult append_span(Text& text, std::string value, Rendition rendition)
{
  if (text_run_has_embedded_newline(value)) {
    return std::unexpected(ava::core::Error(ava::core::ErrorCategory::InvalidArgument,
                                            "text span run must not contain embedded newlines"));
  }
  if (!value.empty()) text.runs.push_back(TextSpan{.text = std::move(value), .rendition = rendition});
  return {};
}

void append_newline(Text& text)
{
  text.runs.push_back(NewLine{});
}

void append_plain_text(Text& text, std::string_view value)
{
  std::size_t start = 0;
  for (std::size_t index = 0; index < value.size(); ++index) {
    if (value[index] != '\n' && value[index] != '\r') continue;
    append_plain_segment(text, value.substr(start, index - start));
    append_newline(text);
    if (value[index] == '\r' && index + 1 < value.size() && value[index + 1] == '\n') ++index;
    start = index + 1;
  }
  append_plain_segment(text, value.substr(start));
}

Text text_from_plain(std::string_view value)
{
  Text text;
  append_plain_text(text, value);
  return text;
}

Text text_from_markdown(std::string_view value)
{
  Text text;
  bool in_fence = false;
  std::size_t start = 0;
  for (std::size_t index = 0; index <= value.size(); ++index) {
    bool const at_end = index == value.size();
    bool const at_break = !at_end && (value[index] == '\n' || value[index] == '\r');
    if (!at_end && !at_break) continue;

    auto const line = value.substr(start, index - start);
    if (starts_fence(line)) {
      static_cast<void>(
          append_span(text, std::string(line), Rendition{.dim = true, .code = true, .color = TextColorRole::Code}));
      in_fence = !in_fence;
    } else if (in_fence) {
      static_cast<void>(
          append_span(text, std::string(line), Rendition{.dim = true, .code = true, .color = TextColorRole::Code}));
    } else if (auto const heading = heading_marker_size(line); heading > 0) {
      static_cast<void>(append_span(text, std::string(line.substr(heading)), Rendition{.bold = true}));
    } else {
      append_markdown_inline(text, line);
    }

    if (at_break) {
      append_newline(text);
      if (value[index] == '\r' && index + 1 < value.size() && value[index + 1] == '\n') ++index;
      start = index + 1;
    }
  }
  return text;
}

std::string to_plain_text(Text const& text)
{
  std::string output;
  for (auto const& run : text.runs) {
    if (std::holds_alternative<NewLine>(run)) {
      output.push_back('\n');
    } else if (auto const* string = std::get_if<String>(&run)) {
      output += string->text;
    } else if (auto const* span = std::get_if<TextSpan>(&run)) {
      output += span->text;
    }
  }
  return output;
}

ava::core::VoidResult validate_text(Text const& text)
{
  for (auto const& run : text.runs) {
    if (auto const* string = std::get_if<String>(&run)) {
      if (text_run_has_embedded_newline(string->text)) {
        return std::unexpected(
            ava::core::Error(ava::core::ErrorCategory::InvalidArgument, "text string run contains embedded newline"));
      }
    } else if (auto const* span = std::get_if<TextSpan>(&run)) {
      if (text_run_has_embedded_newline(span->text)) {
        return std::unexpected(
            ava::core::Error(ava::core::ErrorCategory::InvalidArgument, "text span run contains embedded newline"));
      }
    }
  }
  return {};
}

}  // namespace ava::tui
