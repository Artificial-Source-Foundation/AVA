#include "ava/app/runtime_json.h"

#include <cctype>
#include <utility>

#include "ava/app/runtime_json_support.h"
#include "ava/core/ids.h"
#include "ava/core/json.h"

namespace ava::app::runtime {

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
  if (!value) return {};
  return ",\"" + std::string(key) + "\":" + (*value ? "true" : "false");
}

std::string optional_integer_json(std::string_view key, std::optional<long long> const& value)
{
  if (!value) return {};
  return ",\"" + std::string(key) + "\":" + std::to_string(*value);
}

std::string string_array_json(std::vector<std::string> const& values)
{
  std::string json = "[";
  for (std::size_t index = 0; index < values.size(); ++index) {
    if (index > 0) json += ',';
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
  if (!start || *start >= object.size() || object[*start] != '[') return {};

  std::vector<std::string> values;
  bool in_string = false;
  bool escaped = false;
  bool collecting = false;
  int depth = 1;
  std::string current;
  for (std::size_t index = *start + 1; index < object.size(); ++index) {
    char const ch = object[index];
    if (escaped) {
      if (collecting) {
        auto escape_index = index;
        detail::append_json_escaped_char(current, object, escape_index);
        index = escape_index;
      }
      escaped = false;
      continue;
    }
    if (ch == '\\' && in_string) {
      escaped = true;
      continue;
    }
    if (ch == '"') {
      if (!in_string) {
        in_string = true;
        collecting = depth == 1;
        if (collecting) current.clear();
      } else {
        if (collecting) values.push_back(std::move(current));
        in_string = false;
        collecting = false;
      }
      continue;
    }
    if (in_string) {
      if (collecting) current.push_back(ch);
      continue;
    }
    if (ch == '[') {
      ++depth;
    } else if (ch == ']') {
      --depth;
      if (depth == 0) return values;
      if (depth < 0) break;
    }
  }
  return {};
}

std::optional<bool> bool_json_field(std::string_view object, std::string_view key)
{
  auto start = ava::core::json::field_value_start(object, key);
  if (!start) return std::nullopt;
  auto value = trim(object.substr(*start));
  if (value.starts_with("true")) {
    auto rest = trim(value.substr(4));
    if (rest.empty() || rest.front() == ',' || rest.front() == '}') return true;
  }
  if (value.starts_with("false")) {
    auto rest = trim(value.substr(5));
    if (rest.empty() || rest.front() == ',' || rest.front() == '}') return false;
  }
  return std::nullopt;
}

ava::core::VoidResult append_session_start(ava::session::SessionStore& store, ava::agent::Mode mode,
                                           ava::config::ModelInfo const& model,
                                           ava::config::PromptSelection const& prompt, std::size_t context_source_count)
{
  return store.append(ava::session::SessionEntry{
      .id = ava::core::make_id("entry"),
      .parent_id = "",
      .type = ava::session::EntryType::SessionStart,
      .timestamp = ava::session::now_timestamp(),
      .data_json = detail::session_start_data_json(mode, model, prompt, context_source_count),
  });
}

ava::core::VoidResult append_model_change(ava::session::SessionStore& store, ava::config::ModelInfo const& previous,
                                          ava::config::ModelInfo const& current)
{
  return store.append(ava::session::SessionEntry{.id = ava::core::make_id("entry"),
                                                 .parent_id = "",
                                                 .type = ava::session::EntryType::ModelChange,
                                                 .timestamp = ava::session::now_timestamp(),
                                                 .data_json = detail::model_change_data_json(previous, current)});
}

ava::core::VoidResult append_reasoning_change(ava::session::SessionStore& store, ava::config::ModelInfo const& model,
                                              std::optional<RuntimeReasoningSelection> const& selection)
{
  return store.append(ava::session::SessionEntry{.id = ava::core::make_id("entry"),
                                                 .parent_id = "",
                                                 .type = ava::session::EntryType::ReasoningChange,
                                                 .timestamp = ava::session::now_timestamp(),
                                                 .data_json = detail::reasoning_change_data_json(model, selection)});
}

}  // namespace ava::app::runtime
