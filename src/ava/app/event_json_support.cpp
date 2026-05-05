#include "ava/app/event_json_support.h"

#include "ava/core/json.h"

namespace ava::app::detail {
namespace {

void append_payload_separator(std::string& out, bool& has_field)
{
  if (has_field) out += ',';
  has_field = true;
}

void append_escaped_string_value(std::string& out, std::string_view value)
{
  out += '"';
  out += ava::core::json::escape(value);
  out += '"';
}

void append_quoted_key(std::string& out, std::string_view key)
{
  out += '"';
  out += key;
  out += "\":";
}

void append_string_array_value(std::string& out, std::vector<std::string> const& values)
{
  out += '[';
  for (std::size_t index = 0; index < values.size(); ++index) {
    if (index > 0) out += ',';
    append_escaped_string_value(out, values[index]);
  }
  out += ']';
}

}  // namespace

void append_event_string_field(std::string& out, std::string_view key, std::string_view value)
{
  if (value.empty()) return;
  append_event_required_string_field(out, key, value);
}

void append_event_number_field(std::string& out, std::string_view key, std::size_t value)
{
  if (value == 0) return;
  out += ',';
  append_quoted_key(out, key);
  out += std::to_string(value);
}

void append_event_bool_field(std::string& out, std::string_view key, bool value)
{
  if (!value) return;
  out += ',';
  append_quoted_key(out, key);
  out += "true";
}

void append_event_required_string_field(std::string& out, std::string_view key, std::string_view value)
{
  out += ',';
  append_quoted_key(out, key);
  append_escaped_string_value(out, value);
}

void append_event_optional_string_field(std::string& out, std::string_view key, std::optional<std::string> const& value)
{
  if (!value || value->empty()) return;
  append_event_required_string_field(out, key, *value);
}

void append_event_string_array_field(std::string& out, std::string_view key, std::vector<std::string> const& values)
{
  if (values.empty()) return;
  out += ',';
  append_quoted_key(out, key);
  append_string_array_value(out, values);
}

void append_event_json_object_field(std::string& out, std::string_view key, std::string_view value)
{
  if (value.empty()) return;
  out += ',';
  out += '"';
  out += key;
  if (ava::core::json::is_valid_object(value)) {
    out += "\":";
    out += value;
    return;
  }
  out += "_json\":";
  append_escaped_string_value(out, value);
}

void append_payload_string_field(std::string& out, bool& has_field, std::string_view key, std::string_view value)
{
  if (value.empty()) return;
  append_payload_separator(out, has_field);
  append_quoted_key(out, key);
  append_escaped_string_value(out, value);
}

void append_payload_number_field(std::string& out, bool& has_field, std::string_view key, std::size_t value)
{
  if (value == 0) return;
  append_payload_separator(out, has_field);
  append_quoted_key(out, key);
  out += std::to_string(value);
}

void append_payload_bool_field(std::string& out, bool& has_field, std::string_view key, bool value)
{
  if (!value) return;
  append_payload_separator(out, has_field);
  append_quoted_key(out, key);
  out += "true";
}

void append_payload_json_object_field(std::string& out, bool& has_field, std::string_view key, std::string_view value)
{
  if (value.empty()) return;
  append_payload_separator(out, has_field);
  out += '"';
  out += key;
  if (ava::core::json::is_valid_object(value)) {
    out += "\":";
    out += value;
    return;
  }
  out += "_json\":";
  append_escaped_string_value(out, value);
}

void append_payload_string_array_field(std::string& out, bool& has_field, std::string_view key,
                                       std::vector<std::string> const& values)
{
  if (values.empty()) return;
  append_payload_separator(out, has_field);
  append_quoted_key(out, key);
  append_string_array_value(out, values);
}

}  // namespace ava::app::detail
