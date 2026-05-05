#include "ava/session/export_markdown_support.h"

#include <algorithm>
#include <cctype>

#include "ava/core/json.h"

namespace ava::session::detail {
namespace {

bool json_value_delimiter(char ch) noexcept
{
  return ch == ',' || ch == '}' || ch == ']';
}

bool field_token_is(std::string_view object, std::size_t start, std::string_view token)
{
  if (object.substr(start, token.size()) != token) return false;
  auto end = start + token.size();
  while (end < object.size() && std::isspace(static_cast<unsigned char>(object[end])) != 0) ++end;
  return end < object.size() && json_value_delimiter(object[end]);
}

}  // namespace

std::string export_json_string(std::string_view value)
{
  return "\"" + ava::core::json::escape(value) + "\"";
}

std::size_t export_longest_backtick_run(std::string_view text) noexcept
{
  std::size_t longest = 0;
  std::size_t current = 0;
  for (char const ch : text) {
    if (ch == '`') {
      ++current;
      longest = std::max(longest, current);
    } else {
      current = 0;
    }
  }
  return longest;
}

std::string export_fence_for(std::string_view text)
{
  return std::string(std::max<std::size_t>(3, export_longest_backtick_run(text) + 1), '`');
}

std::string sanitize_fenced_export_content(std::string_view content)
{
  std::string sanitized;
  sanitized.reserve(content.size());
  constexpr char kHex[] = "0123456789ABCDEF";
  for (char const ch : content) {
    auto const byte = static_cast<unsigned char>(ch);
    if ((byte < 0x20 && ch != '\n' && ch != '\t') || byte == 0x7F) {
      sanitized += "\\u00";
      sanitized.push_back(kHex[(byte >> 4U) & 0x0FU]);
      sanitized.push_back(kHex[byte & 0x0FU]);
    } else {
      sanitized.push_back(ch);
    }
  }
  return sanitized;
}

void append_export_heading(std::string& out, std::string_view heading)
{
  out += "## ";
  out += heading;
  out += "\n\n";
}

void append_export_fenced_block(std::string& out, std::string_view label, std::string_view content,
                                std::string_view language)
{
  out += label;
  out += ":\n\n";
  auto const sanitized_content = sanitize_fenced_export_content(content);
  auto const fence = export_fence_for(sanitized_content);
  out += fence;
  if (!language.empty()) out += language;
  out += '\n';
  out += sanitized_content;
  if (!sanitized_content.empty() && sanitized_content.back() != '\n') out += '\n';
  out += fence;
  out += "\n\n";
}

void append_optional_export_fenced_block(std::string& out, std::string_view label,
                                         std::optional<std::string> const& content, std::string_view language)
{
  if (content && !content->empty()) append_export_fenced_block(out, label, *content, language);
}

std::string export_metadata_json(SessionEntry const& entry)
{
  return "{\"id\":" + export_json_string(entry.id) + ",\"parent_id\":" + export_json_string(entry.parent_id) +
         ",\"type\":" + export_json_string(to_string(entry.type)) +
         ",\"timestamp\":" + export_json_string(entry.timestamp) + "}";
}

void append_export_metadata(std::string& out, SessionEntry const& entry, ExportOptions const& options)
{
  if (!options.include_metadata) return;
  append_export_fenced_block(out, "Metadata", export_metadata_json(entry), "json");
}

std::optional<std::string> export_string_field(SessionEntry const& entry, std::string_view key)
{
  return ava::core::json::string_field(entry.data_json, key);
}

std::optional<long long> export_integer_field(SessionEntry const& entry, std::string_view key)
{
  return ava::core::json::integer_field(entry.data_json, key);
}

std::optional<std::string> export_object_field(SessionEntry const& entry, std::string_view key)
{
  return ava::core::json::object_field(entry.data_json, key);
}

bool export_bool_field_is_true(SessionEntry const& entry, std::string_view key)
{
  auto const start = ava::core::json::field_value_start(entry.data_json, key);
  return start && field_token_is(entry.data_json, *start, "true");
}

std::string export_success_text(SessionEntry const& entry)
{
  auto const start = ava::core::json::field_value_start(entry.data_json, "success");
  if (!start) return "unknown";
  if (field_token_is(entry.data_json, *start, "true")) return "true";
  if (field_token_is(entry.data_json, *start, "false")) return "false";
  return "unknown";
}

}  // namespace ava::session::detail
