#include "sys.h"
#include "ava/app/runtime_json.h"
#include "ava/core/ids.h"
#include "ava/core/json.h"

#include <cctype>
#include <utility>

namespace ava::app::runtime {
namespace {

int hex_value(char ch)
{
  if (ch >= '0' && ch <= '9')
    return ch - '0';
  if (ch >= 'a' && ch <= 'f')
    return 10 + (ch - 'a');
  if (ch >= 'A' && ch <= 'F')
    return 10 + (ch - 'A');
  return -1;
}

std::optional<unsigned int> parse_hex_code_unit(std::string_view text, std::size_t hex_start)
{
  if (hex_start + 3 >= text.size())
    return std::nullopt;
  unsigned int codepoint = 0;
  for (std::size_t offset = 0; offset < 4; ++offset)
  {
    int const value = hex_value(text[hex_start + offset]);
    if (value < 0)
      return std::nullopt;
    codepoint = (codepoint << 4) | static_cast<unsigned int>(value);
  }
  return codepoint;
}

bool is_high_surrogate(unsigned int code_unit)
{
  return code_unit >= 0xD800 && code_unit <= 0xDBFF;
}

bool is_low_surrogate(unsigned int code_unit)
{
  return code_unit >= 0xDC00 && code_unit <= 0xDFFF;
}

void append_utf8(std::string& output, unsigned int codepoint)
{
  if (codepoint <= 0x7F)
  {
    output.push_back(static_cast<char>(codepoint));
  }
  else if (codepoint <= 0x7FF)
  {
    output.push_back(static_cast<char>(0xC0 | (codepoint >> 6)));
    output.push_back(static_cast<char>(0x80 | (codepoint & 0x3F)));
  }
  else if (codepoint <= 0xFFFF)
  {
    output.push_back(static_cast<char>(0xE0 | (codepoint >> 12)));
    output.push_back(static_cast<char>(0x80 | ((codepoint >> 6) & 0x3F)));
    output.push_back(static_cast<char>(0x80 | (codepoint & 0x3F)));
  }
  else
  {
    output.push_back(static_cast<char>(0xF0 | ((codepoint >> 18) & 0x07)));
    output.push_back(static_cast<char>(0x80 | ((codepoint >> 12) & 0x3F)));
    output.push_back(static_cast<char>(0x80 | ((codepoint >> 6) & 0x3F)));
    output.push_back(static_cast<char>(0x80 | (codepoint & 0x3F)));
  }
}

void append_json_escaped_char(std::string& output, std::string_view object, std::size_t& index)
{
  if (index >= object.size())
    return;
  char const escaped = object[index];
  switch (escaped)
  {
    case '"':
      output.push_back('"');
      return;
    case '\\':
      output.push_back('\\');
      return;
    case '/':
      output.push_back('/');
      return;
    case 'b':
      output.push_back('\b');
      return;
    case 'f':
      output.push_back('\f');
      return;
    case 'n':
      output.push_back('\n');
      return;
    case 'r':
      output.push_back('\r');
      return;
    case 't':
      output.push_back('\t');
      return;
    case 'u': {
      auto const code_unit = parse_hex_code_unit(object, index + 1);
      if (!code_unit)
      {
        output.push_back('u');
        return;
      }
      if (is_high_surrogate(*code_unit))
      {
        if (index + 10 < object.size() && object[index + 5] == '\\' && object[index + 6] == 'u')
        {
          auto const low = parse_hex_code_unit(object, index + 7);
          if (low && is_low_surrogate(*low))
          {
            append_utf8(output, ((*code_unit - 0xD800) << 10) + (*low - 0xDC00) + 0x10000);
            index += 10;
            return;
          }
        }
        append_utf8(output, 0xFFFD);
        index += 4;
        return;
      }
      append_utf8(output, is_low_surrogate(*code_unit) ? 0xFFFD : *code_unit);
      index += 4;
      return;
    }
    default:
      output.push_back(escaped);
      return;
  }
}

std::string session_start_data_json(ava::agent::Mode mode, ava::config::ModelInfo const& model, runtime::RuntimeBasePromptMetadata const& base_prompt,
                                    std::size_t context_source_count)
{
  std::string json = "{\"mode\":\"" + ava::agent::to_string(mode) + "\",\"provider\":\"" + ava::core::json::escape(model.provider_id) + "\",\"model\":\"" +
                     ava::core::json::escape(model.model_id) +
                     "\",\"prompt_override\":" + (base_prompt.from_override ? std::string("true") : std::string("false")) +
                     ",\"context_sources\":" + std::to_string(context_source_count);
  json += ",\"input_modalities\":" + string_array_json(model.input_modalities);
  json += ",\"output_modalities\":" + string_array_json(model.output_modalities);
  json += ",\"reasoning_levels\":" + string_array_json(model.reasoning_levels);
  json += ",\"compatibility_quirks\":" + string_array_json(model.compatibility_quirks);
  json += optional_bool_json("supports_tools", model.supports_tools);
  json += optional_bool_json("supports_streaming", model.supports_streaming);
  json += optional_bool_json("supports_reasoning", model.supports_reasoning);
  json += optional_bool_json("reports_usage", model.reports_usage);
  if (!model.display_name.empty())
  {
    json += ",\"display_name\":\"" + ava::core::json::escape(model.display_name) + "\"";
  }
  if (!model.family.empty())
    json += ",\"family\":\"" + ava::core::json::escape(model.family) + "\"";
  if (!model.api_family.empty())
  {
    json += ",\"api_family\":\"" + ava::core::json::escape(model.api_family) + "\"";
  }
  if (model.context_window_tokens)
    json += ",\"context_window_tokens\":" + std::to_string(*model.context_window_tokens);
  if (model.max_output_tokens)
    json += ",\"max_output_tokens\":" + std::to_string(*model.max_output_tokens);
  if (!model.reasoning_format.empty())
  {
    json += ",\"reasoning_format\":\"" + ava::core::json::escape(model.reasoning_format) + "\"";
  }
  json += '}';
  return json;
}

std::string model_change_data_json(ava::config::ModelInfo const& previous, ava::config::ModelInfo const& current)
{
  std::string json = "{";
  json += "\"previous_provider\":\"" + ava::core::json::escape(previous.provider_id) + "\"";
  json += ",\"previous_model\":\"" + ava::core::json::escape(previous.model_id) + "\"";
  json += ",\"provider\":\"" + ava::core::json::escape(current.provider_id) + "\"";
  json += ",\"model\":\"" + ava::core::json::escape(current.model_id) + "\"";
  json += ",\"display_name\":\"" + ava::core::json::escape(current.display_name) + "\"";
  json += ",\"family\":\"" + ava::core::json::escape(current.family) + "\"";
  json += ",\"api_family\":\"" + ava::core::json::escape(current.api_family) + "\"";
  json += ",\"input_modalities\":" + string_array_json(current.input_modalities);
  json += ",\"output_modalities\":" + string_array_json(current.output_modalities);
  json += ",\"reasoning_levels\":" + string_array_json(current.reasoning_levels);
  json += ",\"compatibility_quirks\":" + string_array_json(current.compatibility_quirks);
  json += optional_integer_json("context_window_tokens", current.context_window_tokens);
  json += optional_integer_json("max_output_tokens", current.max_output_tokens);
  json += optional_bool_json("supports_tools", current.supports_tools);
  json += optional_bool_json("supports_streaming", current.supports_streaming);
  json += optional_bool_json("supports_reasoning", current.supports_reasoning);
  json += optional_bool_json("reports_usage", current.reports_usage);
  if (!current.reasoning_format.empty())
  {
    json += ",\"reasoning_format\":\"" + ava::core::json::escape(current.reasoning_format) + "\"";
  }
  json += "}";
  return json;
}

std::string reasoning_change_data_json(ava::config::ModelInfo const& model, std::optional<runtime::RuntimeReasoningSelection> const& selection)
{
  std::string json = "{\"provider\":\"" + ava::core::json::escape(model.provider_id) + "\",\"model\":\"" + ava::core::json::escape(model.model_id) + "\"";
  if (!model.reasoning_format.empty())
  {
    json += ",\"format\":\"" + ava::core::json::escape(model.reasoning_format) + "\"";
  }
  json += ",\"enabled\":";
  json += selection ? "true" : "false";
  if (selection)
  {
    json += ",\"level\":\"" + ava::core::json::escape(selection->level) + "\"";
    if (selection->budget_tokens)
      json += ",\"budget_tokens\":" + std::to_string(*selection->budget_tokens);
    if (!selection->display.empty())
      json += ",\"display\":\"" + ava::core::json::escape(selection->display) + "\"";
  }
  json += '}';
  return json;
}

}  // namespace

std::string_view trim(std::string_view value)
{
  while (!value.empty() && std::isspace(static_cast<unsigned char>(value.front())) != 0) value.remove_prefix(1);
  while (!value.empty() && std::isspace(static_cast<unsigned char>(value.back())) != 0) value.remove_suffix(1);
  return value;
}

std::string trimmed_copy(std::string_view value)
{
  return std::string(trim(value));
}

std::string json_string_field(std::string_view key, std::string_view value)
{
  return "\"" + std::string(key) + "\":\"" + ava::core::json::escape(value) + "\"";
}

std::string json_bool_field(std::string_view key, bool value)
{
  return "\"" + std::string(key) + "\":" + (value ? "true" : "false");
}

std::string optional_bool_json(std::string_view key, std::optional<bool> const& value)
{
  if (!value)
    return {};
  return ",\"" + std::string(key) + "\":" + (*value ? "true" : "false");
}

std::string optional_integer_json(std::string_view key, std::optional<long long> const& value)
{
  if (!value)
    return {};
  return ",\"" + std::string(key) + "\":" + std::to_string(*value);
}

std::string string_array_json(std::vector<std::string> const& values)
{
  std::string json = "[";
  for (std::size_t index = 0; index < values.size(); ++index)
  {
    if (index > 0)
      json += ',';
    json += '"';
    json += ava::core::json::escape(values[index]);
    json += '"';
  }
  json += ']';
  return json;
}

std::vector<std::string> string_array_field(std::string_view object, std::string_view key)
{
  auto const start = ava::core::json::field_value_start(object, key);
  if (!start || *start >= object.size() || object[*start] != '[')
    return {};

  std::vector<std::string> values;
  bool in_string = false;
  bool escaped = false;
  bool collecting = false;
  int depth = 1;
  std::string current;
  for (std::size_t index = *start + 1; index < object.size(); ++index)
  {
    char const ch = object[index];
    if (escaped)
    {
      if (collecting)
      {
        auto escape_index = index;
        append_json_escaped_char(current, object, escape_index);
        index = escape_index;
      }
      escaped = false;
      continue;
    }
    if (ch == '\\' && in_string)
    {
      escaped = true;
      continue;
    }
    if (ch == '"')
    {
      if (!in_string)
      {
        in_string = true;
        collecting = depth == 1;
        if (collecting)
          current.clear();
      }
      else
      {
        if (collecting)
          values.push_back(std::move(current));
        in_string = false;
        collecting = false;
      }
      continue;
    }
    if (in_string)
    {
      if (collecting)
        current.push_back(ch);
      continue;
    }
    if (ch == '[')
    {
      ++depth;
    }
    else if (ch == ']')
    {
      --depth;
      if (depth == 0)
        return values;
      if (depth < 0)
        break;
    }
  }
  return {};
}

std::optional<bool> bool_json_field(std::string_view object, std::string_view key)
{
  auto start = ava::core::json::field_value_start(object, key);
  if (!start)
    return std::nullopt;
  auto value = trim(object.substr(*start));
  if (value.starts_with("true"))
  {
    auto rest = trim(value.substr(4));
    if (rest.empty() || rest.front() == ',' || rest.front() == '}')
      return true;
  }
  if (value.starts_with("false"))
  {
    auto rest = trim(value.substr(5));
    if (rest.empty() || rest.front() == ',' || rest.front() == '}')
      return false;
  }
  return std::nullopt;
}

ava::core::VoidResult append_session_start(ava::session::SessionStore& store, ava::agent::Mode mode, ava::config::ModelInfo const& model,
                                           runtime::RuntimeBasePromptMetadata const& base_prompt, std::size_t context_source_count)
{
  return store.append(ava::session::SessionEntry{
      .id = ava::core::make_id("entry"),
      .parent_id = "",
      .type = ava::session::EntryType::SessionStart,
      .timestamp = ava::session::now_timestamp(),
      .data_json = session_start_data_json(mode, model, base_prompt, context_source_count),
  });
}

ava::core::VoidResult append_model_change(ava::session::SessionStore& store, ava::config::ModelInfo const& previous, ava::config::ModelInfo const& current)
{
  return store.append(ava::session::SessionEntry{.id = ava::core::make_id("entry"),
                                                 .parent_id = "",
                                                 .type = ava::session::EntryType::ModelChange,
                                                 .timestamp = ava::session::now_timestamp(),
                                                 .data_json = model_change_data_json(previous, current)});
}

ava::core::VoidResult append_reasoning_change(ava::session::SessionStore& store, ava::config::ModelInfo const& model,
                                              std::optional<runtime::RuntimeReasoningSelection> const& selection)
{
  return store.append(ava::session::SessionEntry{.id = ava::core::make_id("entry"),
                                                 .parent_id = "",
                                                 .type = ava::session::EntryType::ReasoningChange,
                                                 .timestamp = ava::session::now_timestamp(),
                                                 .data_json = reasoning_change_data_json(model, selection)});
}

}  // namespace ava::app::runtime
