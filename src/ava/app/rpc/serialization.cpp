#include "ava/app/rpc/serialization.h"

#include <algorithm>
#include <cctype>
#include <iomanip>
#include <sstream>
#include <utility>

#include "ava/app/rpc/protocol.h"
#include "ava/core/json.h"
#include "ava/provider/registry.h"
#include "ava/session/stats.h"
#include "ava/session/validation.h"

namespace ava::app::rpc {
namespace {

std::string decimal_field_json(std::string_view key, long double value)
{
  std::ostringstream out;
  out << std::setprecision(12) << value;
  return "\"" + std::string(key) + "\":" + out.str();
}

std::string model_key(std::string_view provider_id, std::string_view model_id)
{
  return std::string(provider_id) + "\n" + std::string(model_id);
}

bool has_model_key(std::vector<std::string> const& keys, std::string_view key)
{
  for (auto const& existing : keys) {
    if (existing == key) return true;
  }
  return false;
}

void append_optional_bool(std::string& json, std::string_view key, std::optional<bool> const& value)
{
  if (!value) return;
  json += ',';
  json += bool_field_json(key, *value);
}

void append_optional_integer(std::string& json, std::string_view key, std::optional<long long> const& value)
{
  if (!value) return;
  json += ',';
  json += integer_field_json(key, *value);
}

std::string model_info_json(ava::config::ModelInfo const& model, RuntimeSession const& session, bool configured)
{
  bool const registered = ava::provider::builtin_provider_registry().contains(model.provider_id);
  std::string json = "{";
  json += string_field_json("provider", model.provider_id);
  json += ',';
  json += string_field_json("model", model.model_id);
  json += ',';
  json += string_field_json("display_name", model.display_name);
  json += ',';
  json += string_field_json("family", model.family);
  json += ',';
  json += string_field_json("api_family", model.api_family);
  json += ',';
  json += bool_field_json("registered", registered);
  json += ',';
  json += bool_field_json("selectable", registered && configured);
  append_optional_integer(json, "context_window_tokens", model.context_window_tokens);
  append_optional_integer(json, "max_output_tokens", model.max_output_tokens);
  append_optional_bool(json, "supports_tools", model.supports_tools);
  append_optional_bool(json, "supports_streaming", model.supports_streaming);
  append_optional_bool(json, "supports_reasoning", model.supports_reasoning);
  append_optional_bool(json, "reports_usage", model.reports_usage);
  json += ",\"input_modalities\":";
  json += string_array_json(model.input_modalities);
  json += ",\"output_modalities\":";
  json += string_array_json(model.output_modalities);
  json += ",\"reasoning_levels\":";
  json += string_array_json(model.reasoning_levels);
  if (!model.reasoning_format.empty()) {
    json += ',';
    json += string_field_json("reasoning_format", model.reasoning_format);
  }
  json += ",\"compatibility_quirks\":";
  json += string_array_json(model.compatibility_quirks);
  json += ',';
  json += bool_field_json("selected",
                          session.model.provider_id == model.provider_id && session.model.model_id == model.model_id);
  json += '}';
  return json;
}

std::string joined_output(std::vector<std::string> const& output)
{
  std::string text;
  for (std::size_t index = 0; index < output.size(); ++index) {
    if (index > 0) text += '\n';
    text += output[index];
  }
  return text;
}

std::string context_sources_json(RuntimeSession const& session)
{
  std::string json = "[";
  for (std::size_t index = 0; index < session.context_sources.size(); ++index) {
    auto const& source = session.context_sources[index];
    if (index > 0) json += ',';
    json += '{';
    json += string_field_json("path", source.path.string());
    json += ',';
    json += string_field_json("source_type", ava::context::to_string(source.source_type));
    json += ',';
    json += number_field_json("byte_count", source.byte_count);
    json += '}';
  }
  json += ']';
  return json;
}

std::string session_entry_json(ava::session::SessionEntry const& entry)
{
  std::string json = "{";
  json += integer_field_json("version", entry.version);
  json += ',';
  json += string_field_json("id", entry.id);
  json += ',';
  json += string_field_json("parent_id", entry.parent_id);
  json += ',';
  json += string_field_json("type", ava::session::to_string(entry.type));
  json += ',';
  json += string_field_json("timestamp", entry.timestamp);
  json += ",\"data\":";
  json += entry.data_json;
  json += '}';
  return json;
}

std::string_view trim(std::string_view value)
{
  while (!value.empty() && std::isspace(static_cast<unsigned char>(value.front())) != 0) value.remove_prefix(1);
  while (!value.empty() && std::isspace(static_cast<unsigned char>(value.back())) != 0) value.remove_suffix(1);
  return value;
}

bool json_bool_field(std::string_view object, std::string_view key)
{
  auto const start = ava::core::json::field_value_start(object, key);
  if (!start) return false;
  auto const value = trim(object.substr(*start));
  if (!value.starts_with("true")) return false;
  auto const rest = trim(value.substr(4));
  return rest.empty() || rest.front() == ',' || rest.front() == '}';
}

std::string sanitized_reasoning_entry_json(ava::session::SessionEntry const& entry)
{
  std::string data = "{";
  bool first = true;
  auto append_string = [&](std::string_view key, std::optional<std::string> const& value) {
    if (!value || value->empty()) return;
    if (!first) data += ',';
    first = false;
    data += string_field_json(key, *value);
  };
  auto append_bool = [&](std::string_view key, bool value) {
    if (!first) data += ',';
    first = false;
    data += bool_field_json(key, value);
  };

  append_string("provider", ava::core::json::string_field(entry.data_json, "provider"));
  append_string("model", ava::core::json::string_field(entry.data_json, "model"));
  append_string("format", ava::core::json::string_field(entry.data_json, "format"));
  auto const redacted = json_bool_field(entry.data_json, "redacted");
  if (!redacted) append_string("text", ava::core::json::string_field(entry.data_json, "text"));
  append_bool("redacted", redacted);
  append_bool("signature_present", ava::core::json::string_field(entry.data_json, "signature").has_value());
  data += '}';

  std::string json = "{";
  json += integer_field_json("version", entry.version);
  json += ',';
  json += string_field_json("id", entry.id);
  json += ',';
  json += string_field_json("parent_id", entry.parent_id);
  json += ',';
  json += string_field_json("type", ava::session::to_string(entry.type));
  json += ',';
  json += string_field_json("timestamp", entry.timestamp);
  json += ",\"data\":";
  json += data;
  json += '}';
  return json;
}

std::string capped_session_entry_json(ava::session::SessionEntry const& entry)
{
  auto json = entry.type == ava::session::EntryType::ReasoningBlock ? sanitized_reasoning_entry_json(entry)
                                                                    : session_entry_json(entry);
  if (json.size() <= 8192) return json;
  std::string capped = "{";
  capped += integer_field_json("version", entry.version);
  capped += ',';
  capped += string_field_json("id", entry.id);
  capped += ',';
  capped += string_field_json("parent_id", entry.parent_id);
  capped += ',';
  capped += string_field_json("type", ava::session::to_string(entry.type));
  capped += ',';
  capped += string_field_json("timestamp", entry.timestamp);
  capped += ",";
  capped += bool_field_json("truncated", true);
  capped += ',';
  capped += number_field_json("original_bytes", json.size());
  capped += '}';
  return capped;
}

std::string_view utf8_prefix_within(std::string_view value, std::size_t max_bytes)
{
  if (value.size() <= max_bytes) return value;

  std::size_t index = 0;
  std::size_t end = 0;
  while (index < value.size() && index < max_bytes) {
    auto const byte = static_cast<unsigned char>(value[index]);
    std::size_t length = 0;
    if (byte < 0x80) {
      length = 1;
    } else if ((byte & 0xE0U) == 0xC0U) {
      length = 2;
    } else if ((byte & 0xF0U) == 0xE0U) {
      length = 3;
    } else if ((byte & 0xF8U) == 0xF0U) {
      length = 4;
    } else {
      break;
    }
    if (index + length > max_bytes || index + length > value.size()) break;
    bool valid_continuation = true;
    for (std::size_t offset = 1; offset < length; ++offset) {
      auto const continuation = static_cast<unsigned char>(value[index + offset]);
      if ((continuation & 0xC0U) != 0x80U) {
        valid_continuation = false;
        break;
      }
    }
    if (!valid_continuation) break;
    end = index + length;
    index += length;
  }
  return value.substr(0, end);
}

std::string question_options_json(std::vector<ava::agent::QuestionOption> const& options)
{
  std::string json = "[";
  for (std::size_t index = 0; index < options.size(); ++index) {
    if (index > 0) json += ',';
    json += '{';
    json += string_field_json("value", options[index].value);
    json += ',';
    json += string_field_json("label", options[index].label);
    json += '}';
  }
  json += ']';
  return json;
}

}  // namespace

std::string string_field_json(std::string_view key, std::string_view value)
{
  return "\"" + std::string(key) + "\":\"" + ava::core::json::escape(value) + "\"";
}

std::string bool_field_json(std::string_view key, bool value)
{
  return "\"" + std::string(key) + "\":" + (value ? "true" : "false");
}

std::string number_field_json(std::string_view key, std::size_t value)
{
  return "\"" + std::string(key) + "\":" + std::to_string(value);
}

std::string integer_field_json(std::string_view key, long long value)
{
  return "\"" + std::string(key) + "\":" + std::to_string(value);
}

std::string output_array_json(std::vector<std::string> const& output)
{
  std::string json = "[";
  for (std::size_t index = 0; index < output.size(); ++index) {
    if (index > 0) json += ',';
    json += '"';
    json += ava::core::json::escape(output[index]);
    json += '"';
  }
  json += ']';
  return json;
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

std::vector<ava::config::ModelInfo> effective_models(ava::config::ModelRegistry const& registry)
{
  std::vector<ava::config::ModelInfo> models;
  std::vector<std::string> seen;
  for (auto model = registry.models.rbegin(); model != registry.models.rend(); ++model) {
    auto const key = model_key(model->provider_id, model->model_id);
    if (has_model_key(seen, key)) continue;
    seen.push_back(key);
    models.push_back(*model);
  }
  std::reverse(models.begin(), models.end());
  return models;
}

std::string state_result_json(RuntimeSession const& session, bool cancel_requested)
{
  std::string json = "{";
  json += "\"protocol_version\":";
  json += std::to_string(kRpcProtocolVersion);
  json += ',';
  json += string_field_json("session_id", session.store.session_id());
  json += ',';
  json += string_field_json("session_path", session.store.session_path().string());
  json += ',';
  json += string_field_json("mode", ava::agent::to_string(session.mode));
  json += ',';
  json += string_field_json("provider", session.model.provider_id);
  json += ',';
  json += string_field_json("model", session.model.model_id);
  json += ',';
  json += string_field_json("workspace_dir", session.workspace_dir.string());
  json += ',';
  json += string_field_json("current_dir", session.current_dir.string());
  json += ',';
  json += bool_field_json("created", session.created);
  json += ',';
  json += bool_field_json("cancel_requested", cancel_requested);
  json += ',';
  json += bool_field_json("reasoning_enabled", session.reasoning.has_value());
  if (session.reasoning) {
    json += ',';
    json += string_field_json("reasoning_level", session.reasoning->level);
    if (session.reasoning->budget_tokens) {
      json += ',';
      json += integer_field_json("reasoning_budget_tokens", *session.reasoning->budget_tokens);
    }
    if (!session.reasoning->display.empty()) {
      json += ',';
      json += string_field_json("reasoning_display", session.reasoning->display);
    }
  }
  json += ',';
  json += number_field_json("context_source_count", session.context_sources.size());
  json += ",\"context_sources\":";
  json += context_sources_json(session);
  json += '}';
  return json;
}

ava::core::Result<std::string> list_sessions_result_json(RuntimeSession const& session)
{
  auto sessions = ava::session::SessionStore::list_sessions(session.workspace_dir, session.paths.sessions_dir);
  if (!sessions) return std::unexpected(sessions.error());
  std::string json = "{\"sessions\":[";
  for (std::size_t index = 0; index < sessions->size(); ++index) {
    auto const& summary = (*sessions)[index];
    if (index > 0) json += ',';
    json += '{';
    json += string_field_json("session_id", summary.session_id);
    json += ',';
    json += string_field_json("path", summary.path.string());
    json += ',';
    json += string_field_json("last_updated", summary.last_updated);
    json += ',';
    json += number_field_json("entry_count", summary.entry_count);
    json += '}';
  }
  json += "]}";
  return json;
}

ava::core::Result<std::string> list_models_result_json(RuntimeSession const& session)
{
  auto registry = ava::config::load_model_registry(session.paths);
  if (!registry) return std::unexpected(std::move(registry.error()));

  auto models = effective_models(*registry);
  bool current_in_catalog = false;
  for (auto const& model : models) {
    current_in_catalog = current_in_catalog ||
                         (model.provider_id == session.model.provider_id && model.model_id == session.model.model_id);
  }
  std::string json = "{";
  json += string_field_json("default_provider", registry->default_provider_id);
  json += ',';
  json += string_field_json("default_model", registry->default_model_id);
  json += ',';
  json += string_field_json("current_provider", session.model.provider_id);
  json += ',';
  json += string_field_json("current_model", session.model.model_id);
  json += ",\"models\":[";
  for (std::size_t index = 0; index < models.size(); ++index) {
    if (index > 0) json += ',';
    json += model_info_json(models[index], session, true);
  }
  if (!current_in_catalog) {
    if (!models.empty()) json += ',';
    json += model_info_json(session.model, session, false);
  }
  json += "]}";
  return json;
}

std::string command_result_json(CommandResult const& result)
{
  std::string json = "{";
  json += bool_field_json("handled", result.handled);
  json += ',';
  json += bool_field_json("quit", result.quit);
  json += ",\"output\":";
  json += output_array_json(result.output);
  json += ',';
  json += string_field_json("text", joined_output(result.output));
  json += '}';
  return json;
}

ava::core::Result<std::string> messages_result_json(RuntimeSession const& session)
{
  auto entries = session.store.load();
  if (!entries) return std::unexpected(entries.error());

  std::string json = "{";
  json += string_field_json("session_id", session.store.session_id());
  json += ",\"messages\":[";
  bool first = true;
  bool truncated = false;
  std::size_t included = 0;
  for (auto const& entry : *entries) {
    if (ava::session::is_internal_replay_user_message(entry)) continue;
    if (entry.type != ava::session::EntryType::UserMessage && entry.type != ava::session::EntryType::AssistantMessage &&
        entry.type != ava::session::EntryType::ReasoningBlock && entry.type != ava::session::EntryType::ToolCall &&
        entry.type != ava::session::EntryType::ToolResult) {
      continue;
    }
    if (included >= kMaxRpcMessagesEntries) {
      truncated = true;
      break;
    }
    auto entry_json = capped_session_entry_json(entry);
    std::size_t const projected_size = json.size() + entry_json.size() + 16;
    if (projected_size > kMaxRpcMessagesResponseBytes) {
      truncated = true;
      break;
    }
    if (!first) json += ',';
    first = false;
    json += entry_json;
    ++included;
  }
  json += "],";
  json += bool_field_json("truncated", truncated);
  json += ',';
  json += number_field_json("message_count", included);
  json += '}';
  return json;
}

ava::core::Result<std::string> session_stats_result_json(RuntimeSession const& session)
{
  auto entries = session.store.load();
  if (!entries) return std::unexpected(entries.error());
  auto const stats = ava::session::compute_session_stats(*entries);

  std::string json = "{";
  json += string_field_json("session_id", session.store.session_id());
  json += ',';
  json += string_field_json("session_path", session.store.session_path().string());
  json += ',';
  json += number_field_json("entry_count", stats.entry_count);
  json += ',';
  json += string_field_json("first_timestamp", stats.first_timestamp);
  json += ',';
  json += string_field_json("last_timestamp", stats.last_timestamp);
  if (stats.input_tokens) {
    json += ',';
    json += integer_field_json("input_tokens", *stats.input_tokens);
  }
  if (stats.output_tokens) {
    json += ',';
    json += integer_field_json("output_tokens", *stats.output_tokens);
  }
  if (stats.reasoning_tokens) {
    json += ',';
    json += integer_field_json("reasoning_tokens", *stats.reasoning_tokens);
  }
  if (stats.cache_read_tokens) {
    json += ',';
    json += integer_field_json("cache_read_tokens", *stats.cache_read_tokens);
  }
  if (stats.cache_write_tokens) {
    json += ',';
    json += integer_field_json("cache_write_tokens", *stats.cache_write_tokens);
  }
  if (stats.total_tokens) {
    json += ',';
    json += integer_field_json("total_tokens", *stats.total_tokens);
  }
  if (stats.estimated_input_bytes) {
    json += ',';
    json += integer_field_json("estimated_input_bytes", *stats.estimated_input_bytes);
  }
  if (stats.estimated_output_bytes) {
    json += ',';
    json += integer_field_json("estimated_output_bytes", *stats.estimated_output_bytes);
  }
  if (stats.estimated_total_bytes) {
    json += ',';
    json += integer_field_json("estimated_total_bytes", *stats.estimated_total_bytes);
  }
  if (stats.total_cost_usd) {
    json += ',';
    json += decimal_field_json("total_cost_usd", *stats.total_cost_usd);
  }
  if (stats.known_cost_usd) {
    json += ',';
    json += decimal_field_json("known_cost_usd", *stats.known_cost_usd);
  }
  json += ',';
  json += bool_field_json("cost_complete", stats.cost_complete);
  json += ',';
  json += number_field_json("unknown_cost_entries", stats.unknown_cost_entries);
  json += ',';
  json += number_field_json("exact_usage_entries", stats.exact_usage_entries);
  json += ',';
  json += number_field_json("estimated_usage_entries", stats.estimated_usage_entries);
  json += ",\"counts\":{";
  json += number_field_json("session_start", stats.counts.session_start);
  json += ',';
  json += number_field_json("user_message", stats.counts.user_message);
  json += ',';
  json += number_field_json("assistant_message", stats.counts.assistant_message);
  json += ',';
  json += number_field_json("tool_call", stats.counts.tool_call);
  json += ',';
  json += number_field_json("tool_result", stats.counts.tool_result);
  json += ',';
  json += number_field_json("permission_decision", stats.counts.permission_decision);
  json += ',';
  json += number_field_json("mode_change", stats.counts.mode_change);
  json += ',';
  json += number_field_json("model_change", stats.counts.model_change);
  json += ',';
  json += number_field_json("reasoning_block", stats.counts.reasoning_block);
  json += ',';
  json += number_field_json("reasoning_change", stats.counts.reasoning_change);
  json += ',';
  json += number_field_json("compaction", stats.counts.compaction);
  json += ',';
  json += number_field_json("error", stats.counts.error);
  json += ',';
  json += number_field_json("cancel", stats.counts.cancel);
  json += "}}";
  return json;
}

ava::core::Result<std::string> session_validation_result_json(RuntimeSession const& session)
{
  auto entries = session.store.load();
  if (!entries) return std::unexpected(entries.error());
  auto const validation = ava::session::validate_session_replay(*entries);

  std::string json = "{";
  json += string_field_json("session_id", session.store.session_id());
  json += ',';
  json += string_field_json("session_path", session.store.session_path().string());
  json += ',';
  json += bool_field_json("ok", validation.ok());
  json += ',';
  json += number_field_json("error_count", validation.error_count);
  json += ',';
  json += number_field_json("warning_count", validation.warning_count);
  json += ",\"issues\":[";
  for (std::size_t index = 0; index < validation.issues.size(); ++index) {
    auto const& issue = validation.issues[index];
    if (index > 0) json += ',';
    json += '{';
    json += string_field_json("severity", ava::session::to_string(issue.severity));
    json += ',';
    json += string_field_json("kind", ava::session::to_string(issue.kind));
    json += ',';
    json += number_field_json("entry_index", issue.entry_index);
    if (!issue.entry_id.empty()) {
      json += ',';
      json += string_field_json("entry_id", issue.entry_id);
    }
    if (!issue.call_id.empty()) {
      json += ',';
      json += string_field_json("call_id", issue.call_id);
    }
    if (!issue.message.empty()) {
      json += ',';
      json += string_field_json("message", issue.message);
    }
    json += '}';
  }
  json += "]}";
  return json;
}

std::string permission_request_payload_json(std::string_view resolver_request_id,
                                            ava::permissions::PermissionPrompt const& prompt)
{
  std::string json = "{";
  json += string_field_json("resolver_request_id", resolver_request_id);
  json += ',';
  json += string_field_json("operation", ava::permissions::to_string(prompt.operation));
  json += ',';
  json += string_field_json("mode", ava::agent::to_string(prompt.mode));
  json += ',';
  json += string_field_json("target_path", prompt.target_path.string());
  json += ',';
  json += string_field_json("command", prompt.command);
  json += ',';
  json += string_field_json("tool_name", prompt.tool_name);
  json += ',';
  json += string_field_json("reason", prompt.reason);
  if (!prompt.diff_preview.empty()) {
    json += ',';
    json += string_field_json("diff_preview", prompt.diff_preview);
    json += ',';
    json += bool_field_json("diff_truncated", prompt.diff_truncated);
  }
  json += '}';
  return json;
}

std::string question_request_payload_json(std::string_view resolver_request_id,
                                          ava::agent::QuestionPrompt const& prompt)
{
  std::string json = "{";
  json += string_field_json("resolver_request_id", resolver_request_id);
  json += ',';
  json += string_field_json("header", prompt.header);
  json += ',';
  json += string_field_json("question", prompt.question);
  json += ",\"options\":";
  json += question_options_json(prompt.options);
  json += ',';
  json += bool_field_json("multiple", prompt.multiple);
  json += ',';
  json += bool_field_json("allow_custom", prompt.allow_custom);
  json += ',';
  json += bool_field_json("secret", prompt.secret);
  json += ',';
  json += bool_field_json("modal", prompt.modal);
  json += ',';
  json += bool_field_json("searchable", prompt.searchable);
  json += '}';
  return json;
}

std::string permission_reply_payload_json(std::string_view resolver_request_id, std::string_view decision)
{
  std::string json = "{";
  json += string_field_json("resolver_request_id", resolver_request_id);
  json += ',';
  json += string_field_json("decision", decision);
  json += '}';
  return json;
}

std::string question_reply_payload_json(std::string_view resolver_request_id, std::optional<std::string> const& answer,
                                        std::optional<std::string> const& selected)
{
  std::string json = "{";
  json += string_field_json("resolver_request_id", resolver_request_id);
  if (answer) {
    json += ',';
    json += string_field_json("answer", *answer);
  }
  if (selected) {
    json += ',';
    json += string_field_json("selected", *selected);
  }
  json += '}';
  return json;
}

std::string cancel_requested_payload_json(bool active_run, std::size_t cleared_steer, std::size_t cleared_follow_up,
                                          std::string_view active_request_id)
{
  std::string json = "{";
  json += bool_field_json("active_run", active_run);
  json += ',';
  json += number_field_json("cleared_steer", cleared_steer);
  json += ',';
  json += number_field_json("cleared_follow_up", cleared_follow_up);
  if (!active_request_id.empty()) {
    json += ',';
    json += string_field_json("active_request_id", active_request_id);
  }
  json += '}';
  return json;
}

std::string queued_message_payload_json(std::string_view message, std::string_view reason)
{
  bool const truncated = message.size() > kMaxRpcQueueEventMessageBytes;
  auto const event_message = truncated ? utf8_prefix_within(message, kMaxRpcQueueEventMessageBytes) : message;
  std::string json = "{";
  json += string_field_json("message", event_message);
  if (truncated) {
    json += ',';
    json += bool_field_json("message_truncated", true);
    json += ',';
    json += number_field_json("message_bytes", message.size());
  }
  if (!reason.empty()) {
    json += ',';
    json += string_field_json("reason", reason);
  }
  json += '}';
  return json;
}

std::string prompt_result_json(std::string_view session_id, ava::agent::AgentLoopResult const& result)
{
  std::string json = "{";
  json += string_field_json("session_id", session_id);
  json += ',';
  json += string_field_json("final_text", result.final_text);
  json += ',';
  json += string_field_json("stop_reason", result.stop_reason);
  json += ',';
  json += number_field_json("provider_iterations", result.provider_iterations);
  json += ',';
  json += number_field_json("tool_calls", result.tool_calls);
  json += '}';
  return json;
}

}  // namespace ava::app::rpc
