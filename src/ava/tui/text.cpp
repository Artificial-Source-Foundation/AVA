#include "ava/tui/text.h"
#include "ava/core/error.h"

#include <cctype>
#include <optional>
#include <utility>

namespace ava::tui {
namespace {

bool starts_fence(std::string_view line)
{
  return line.starts_with("```");
}

std::size_t heading_marker_size(std::string_view line)
{
  std::size_t marker_size = 0;
  while (marker_size < line.size() && marker_size < 6 && line[marker_size] == '#')
  {
    ++marker_size;
  }
  if (marker_size == 0 || marker_size >= line.size() || line[marker_size] != ' ')
    return 0;
  return marker_size + 1;
}

void append_plain_segment(Text& text, std::string_view value)
{
  if (value.empty())
    return;
  text.runs.push_back(String{.text = std::string(value)});
}

bool default_rendition(Rendition const& rendition)
{
  return !rendition.bold && !rendition.dim && !rendition.underline && !rendition.italic && !rendition.strikethrough &&
         !rendition.code && rendition.color == TextColorRole::Default;
}

Rendition merge_rendition(Rendition base, Rendition overlay)
{
  base.bold = base.bold || overlay.bold;
  base.dim = base.dim || overlay.dim;
  base.underline = base.underline || overlay.underline;
  base.italic = base.italic || overlay.italic;
  base.strikethrough = base.strikethrough || overlay.strikethrough;
  base.code = base.code || overlay.code;
  if (overlay.color != TextColorRole::Default)
    base.color = overlay.color;
  return base;
}

void append_markdown_segment(Text& text, std::string_view value, Rendition rendition)
{
  if (value.empty())
    return;
  if (default_rendition(rendition))
  {
    append_plain_segment(text, value);
    return;
  }
  static_cast<void>(append_span(text, std::string(value), rendition));
}

ava::core::VoidResult append_link_span(Text& text, std::string value, Rendition rendition, std::string link_target)
{
  if (text_run_has_embedded_newline(value))
  {
    return std::unexpected(ava::core::Error(ava::core::ErrorCategory::InvalidArgument, "text link span run must not contain embedded newlines"));
  }
  if (text_run_has_embedded_newline(link_target))
  {
    return std::unexpected(ava::core::Error(ava::core::ErrorCategory::InvalidArgument, "text link target must not contain embedded newlines"));
  }
  if (!value.empty())
    text.runs.push_back(TextSpan{.text = std::move(value), .rendition = rendition, .link_target = std::move(link_target)});
  return {};
}

std::optional<std::size_t> markdown_strikethrough_end(std::string_view line, std::size_t marker_start)
{
  if (marker_start + 2 >= line.size() || line.substr(marker_start, 2) != "~~")
    return std::nullopt;
  auto const first = static_cast<unsigned char>(line[marker_start + 2]);
  if (std::isspace(first) != 0 || line[marker_start + 2] == '~')
    return std::nullopt;
  auto search = marker_start + 2;
  while (search < line.size())
  {
    auto const end = line.find("~~", search);
    if (end == std::string_view::npos)
      return std::nullopt;
    if (end > marker_start + 2)
    {
      auto const before_end = static_cast<unsigned char>(line[end - 1]);
      if (std::isspace(before_end) == 0 && line[end - 1] != '~')
        return end;
    }
    search = end + 2;
  }
  return std::nullopt;
}

std::string_view markdown_link_comparison_target(std::string_view target)
{
  constexpr auto kMailtoPrefix = std::string_view{"mailto:"};
  if (target.starts_with(kMailtoPrefix))
    return target.substr(kMailtoPrefix.size());
  return target;
}

bool starts_markdown_bare_url(std::string_view value)
{
  return value.starts_with("https://") || value.starts_with("http://");
}

std::optional<std::size_t> markdown_bare_url_end(std::string_view line, std::size_t start)
{
  if (!starts_markdown_bare_url(line.substr(start)))
    return std::nullopt;
  auto end = start;
  while (end < line.size() && std::isspace(static_cast<unsigned char>(line[end])) == 0)
  {
    ++end;
  }
  while (end > start)
  {
    auto const ch = line[end - 1];
    if (ch != '.' && ch != ',' && ch != ';' && ch != ':' && ch != '!' && ch != '?' && ch != ')' && ch != ']')
      break;
    --end;
  }
  return end > start ? std::optional<std::size_t>{end} : std::nullopt;
}

bool markdown_email_start_boundary(std::string_view line, std::size_t start)
{
  if (start == 0)
    return true;
  auto const previous = static_cast<unsigned char>(line[start - 1]);
  return std::isspace(previous) != 0 || line[start - 1] == '<' || line[start - 1] == '(' || line[start - 1] == '[' ||
         line[start - 1] == '{' || line[start - 1] == '"' || line[start - 1] == '\'';
}

bool markdown_email_local_char(char value)
{
  auto const ch = static_cast<unsigned char>(value);
  return std::isalnum(ch) != 0 || value == '.' || value == '_' || value == '%' || value == '+' || value == '-';
}

bool markdown_email_domain_char(char value)
{
  auto const ch = static_cast<unsigned char>(value);
  return std::isalnum(ch) != 0 || value == '.' || value == '-';
}

std::optional<std::size_t> markdown_bare_email_end(std::string_view line, std::size_t start)
{
  if (!markdown_email_start_boundary(line, start) || start >= line.size() ||
      std::isalnum(static_cast<unsigned char>(line[start])) == 0)
    return std::nullopt;

  auto index = start;
  while (index < line.size() && markdown_email_local_char(line[index]))
  {
    ++index;
  }
  if (index == start || index >= line.size() || line[index] != '@' || line[index - 1] == '.')
    return std::nullopt;

  auto const domain_start = index + 1;
  if (domain_start >= line.size() || std::isalnum(static_cast<unsigned char>(line[domain_start])) == 0)
    return std::nullopt;

  index = domain_start;
  while (index < line.size() && markdown_email_domain_char(line[index]))
  {
    ++index;
  }

  auto end = index;
  while (end > domain_start && (line[end - 1] == '.' || line[end - 1] == '-'))
  {
    --end;
  }
  if (end <= domain_start || std::isalnum(static_cast<unsigned char>(line[end - 1])) == 0)
    return std::nullopt;

  auto has_domain_dot = false;
  for (auto dot = domain_start + 1; dot + 1 < end; ++dot)
  {
    has_domain_dot = has_domain_dot || line[dot] == '.';
  }
  if (!has_domain_dot)
    return std::nullopt;

  return end;
}

void append_markdown_inline(Text& text, std::string_view line, Rendition base_rendition = {},
                            bool include_link_fallbacks = true)
{
  std::size_t plain_start = 0;
  for (std::size_t index = 0; index < line.size();)
  {
    if (line[index] == '[')
    {
      auto const label_end = line.find("](", index + 1);
      if (label_end != std::string_view::npos)
      {
        auto const target_end = line.find(')', label_end + 2);
        if (target_end != std::string_view::npos && label_end > index + 1 && target_end > label_end + 2)
        {
          auto const label = line.substr(index + 1, label_end - index - 1);
          auto const target = line.substr(label_end + 2, target_end - label_end - 2);
          append_markdown_segment(text, line.substr(plain_start, index - plain_start), base_rendition);
          static_cast<void>(append_link_span(text, std::string(label),
                                             merge_rendition(base_rendition, Rendition{.underline = true, .color = TextColorRole::Accent}),
                                             std::string(target)));
          if (include_link_fallbacks && label != markdown_link_comparison_target(target))
          {
            append_markdown_segment(text, " (", base_rendition);
            append_markdown_segment(text, target, base_rendition);
            append_markdown_segment(text, ")", base_rendition);
          }
          index = target_end + 1;
          plain_start = index;
          continue;
        }
      }
    }
    if (index + 1 < line.size() && line[index] == '~' && line[index + 1] == '~')
    {
      auto const end = markdown_strikethrough_end(line, index);
      if (end.has_value())
      {
        append_markdown_segment(text, line.substr(plain_start, index - plain_start), base_rendition);
        static_cast<void>(append_span(text, std::string(line.substr(index + 2, *end - index - 2)),
                                      merge_rendition(base_rendition, Rendition{.strikethrough = true})));
        index = *end + 2;
        plain_start = index;
        continue;
      }
    }
    if (index + 1 < line.size() && line[index] == '*' && line[index + 1] == '*')
    {
      auto const end = line.find("**", index + 2);
      if (end != std::string_view::npos && end > index + 2)
      {
        append_markdown_segment(text, line.substr(plain_start, index - plain_start), base_rendition);
        static_cast<void>(append_span(text, std::string(line.substr(index + 2, end - index - 2)),
                                      merge_rendition(base_rendition, Rendition{.bold = true})));
        index = end + 2;
        plain_start = index;
        continue;
      }
    }
    if (line[index] == '*')
    {
      auto const end = line.find('*', index + 1);
      if (end != std::string_view::npos && end > index + 1)
      {
        append_markdown_segment(text, line.substr(plain_start, index - plain_start), base_rendition);
        static_cast<void>(append_span(text, std::string(line.substr(index + 1, end - index - 1)),
                                      merge_rendition(base_rendition, Rendition{.italic = true})));
        index = end + 1;
        plain_start = index;
        continue;
      }
    }
    if (auto const end = markdown_bare_url_end(line, index); end.has_value())
    {
      append_markdown_segment(text, line.substr(plain_start, index - plain_start), base_rendition);
      auto const url = line.substr(index, *end - index);
      static_cast<void>(append_link_span(text, std::string(url),
                                         merge_rendition(base_rendition, Rendition{.underline = true, .color = TextColorRole::Accent}),
                                         std::string(url)));
      index = *end;
      plain_start = index;
      continue;
    }
    if (auto const end = markdown_bare_email_end(line, index); end.has_value())
    {
      append_markdown_segment(text, line.substr(plain_start, index - plain_start), base_rendition);
      auto const email = line.substr(index, *end - index);
      static_cast<void>(append_link_span(text, std::string(email),
                                         merge_rendition(base_rendition, Rendition{.underline = true, .color = TextColorRole::Accent}),
                                         "mailto:" + std::string(email)));
      index = *end;
      plain_start = index;
      continue;
    }
    if (line[index] == '`')
    {
      auto const end = line.find('`', index + 1);
      if (end != std::string_view::npos && end > index + 1)
      {
        append_markdown_segment(text, line.substr(plain_start, index - plain_start), base_rendition);
        static_cast<void>(append_span(text, std::string(line.substr(index + 1, end - index - 1)),
                                      merge_rendition(base_rendition, Rendition{.code = true, .color = TextColorRole::Code})));
        index = end + 1;
        plain_start = index;
        continue;
      }
    }
    ++index;
  }
  append_markdown_segment(text, line.substr(plain_start), base_rendition);
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
  if (text_run_has_embedded_newline(value))
  {
    return std::unexpected(ava::core::Error(ava::core::ErrorCategory::InvalidArgument, "text string run must not contain embedded newlines"));
  }
  if (!value.empty())
    text.runs.push_back(String{.text = std::move(value)});
  return {};
}

ava::core::VoidResult append_span(Text& text, std::string value, Rendition rendition)
{
  if (text_run_has_embedded_newline(value))
  {
    return std::unexpected(ava::core::Error(ava::core::ErrorCategory::InvalidArgument, "text span run must not contain embedded newlines"));
  }
  if (!value.empty())
    text.runs.push_back(TextSpan{.text = std::move(value), .rendition = rendition});
  return {};
}

void append_newline(Text& text)
{
  text.runs.emplace_back(std::in_place_type<NewLine>);
}

void append_plain_text(Text& text, std::string_view value)
{
  std::size_t start = 0;
  for (std::size_t index = 0; index < value.size(); ++index)
  {
    if (value[index] != '\n' && value[index] != '\r')
      continue;
    append_plain_segment(text, value.substr(start, index - start));
    append_newline(text);
    if (value[index] == '\r' && index + 1 < value.size() && value[index + 1] == '\n')
      ++index;
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

Text text_from_markdown(std::string_view value, bool include_link_fallbacks)
{
  Text text;
  bool in_fence = false;
  std::size_t start = 0;
  for (std::size_t index = 0; index <= value.size(); ++index)
  {
    bool const at_end = index == value.size();
    bool const at_break = !at_end && (value[index] == '\n' || value[index] == '\r');
    if (!at_end && !at_break)
      continue;

    auto const line = value.substr(start, index - start);
    if (starts_fence(line))
    {
      static_cast<void>(append_span(text, std::string(line), Rendition{.dim = true, .code = true, .color = TextColorRole::Code}));
      in_fence = !in_fence;
    }
    else if (in_fence)
    {
      static_cast<void>(append_span(text, std::string(line), Rendition{.dim = true, .code = true, .color = TextColorRole::Code}));
    }
    else if (auto const heading = heading_marker_size(line); heading > 0)
    {
      append_markdown_inline(text, line.substr(heading), Rendition{.bold = true}, include_link_fallbacks);
    }
    else
    {
      append_markdown_inline(text, line, {}, include_link_fallbacks);
    }

    if (at_break)
    {
      append_newline(text);
      if (value[index] == '\r' && index + 1 < value.size() && value[index + 1] == '\n')
        ++index;
      start = index + 1;
    }
  }
  return text;
}

std::string to_plain_text(Text const& text)
{
  std::string output;
  for (auto const& run : text.runs)
  {
    if (std::holds_alternative<NewLine>(run))
    {
      output.push_back('\n');
    }
    else if (auto const* string = std::get_if<String>(&run))
    {
      output += string->text;
    }
    else if (auto const* span = std::get_if<TextSpan>(&run))
    {
      output += span->text;
    }
  }
  return output;
}

ava::core::VoidResult validate_text(Text const& text)
{
  for (auto const& run : text.runs)
  {
    if (auto const* string = std::get_if<String>(&run))
    {
      if (text_run_has_embedded_newline(string->text))
      {
        return std::unexpected(ava::core::Error(ava::core::ErrorCategory::InvalidArgument, "text string run contains embedded newline"));
      }
    }
    else if (auto const* span = std::get_if<TextSpan>(&run))
    {
      if (text_run_has_embedded_newline(span->text))
      {
        return std::unexpected(ava::core::Error(ava::core::ErrorCategory::InvalidArgument, "text span run contains embedded newline"));
      }
      if (text_run_has_embedded_newline(span->link_target))
      {
        return std::unexpected(ava::core::Error(ava::core::ErrorCategory::InvalidArgument, "text link target contains embedded newline"));
      }
    }
  }
  return {};
}

}  // namespace ava::tui
