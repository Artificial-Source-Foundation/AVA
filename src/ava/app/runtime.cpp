#include "ava/app/runtime.h"

#include <algorithm>
#include <cctype>
#include <iterator>
#include <optional>
#include <utility>

#include "ava/app/plugin_event_hooks.h"
#include "ava/config/provider_profiles.h"
#include "ava/core/ids.h"
#include "ava/core/json.h"
#include "ava/provider/provider.h"
#include "ava/provider/registry.h"

namespace ava::app {
namespace {

constexpr std::size_t kMaxCompactionPromptEntryBytes = 8192;

std::string_view trim(std::string_view value) {
  while (!value.empty() && std::isspace(static_cast<unsigned char>(value.front())) != 0) value.remove_prefix(1);
  while (!value.empty() && std::isspace(static_cast<unsigned char>(value.back())) != 0) value.remove_suffix(1);
  return value;
}

std::string trimmed_copy(std::string_view value) { return std::string(trim(value)); }

int hex_value(char ch) {
  if (ch >= '0' && ch <= '9') return ch - '0';
  if (ch >= 'a' && ch <= 'f') return 10 + (ch - 'a');
  if (ch >= 'A' && ch <= 'F') return 10 + (ch - 'A');
  return -1;
}

std::optional<unsigned int> parse_hex_code_unit(std::string_view text, std::size_t hex_start) {
  if (hex_start + 3 >= text.size()) return std::nullopt;
  unsigned int codepoint = 0;
  for (std::size_t offset = 0; offset < 4; ++offset) {
    const int value = hex_value(text[hex_start + offset]);
    if (value < 0) return std::nullopt;
    codepoint = (codepoint << 4) | static_cast<unsigned int>(value);
  }
  return codepoint;
}

bool is_high_surrogate(unsigned int code_unit) { return code_unit >= 0xD800 && code_unit <= 0xDBFF; }

bool is_low_surrogate(unsigned int code_unit) { return code_unit >= 0xDC00 && code_unit <= 0xDFFF; }

void append_utf8(std::string& output, unsigned int codepoint) {
  if (codepoint <= 0x7F) {
    output.push_back(static_cast<char>(codepoint));
  } else if (codepoint <= 0x7FF) {
    output.push_back(static_cast<char>(0xC0 | (codepoint >> 6)));
    output.push_back(static_cast<char>(0x80 | (codepoint & 0x3F)));
  } else if (codepoint <= 0xFFFF) {
    output.push_back(static_cast<char>(0xE0 | (codepoint >> 12)));
    output.push_back(static_cast<char>(0x80 | ((codepoint >> 6) & 0x3F)));
    output.push_back(static_cast<char>(0x80 | (codepoint & 0x3F)));
  } else {
    output.push_back(static_cast<char>(0xF0 | ((codepoint >> 18) & 0x07)));
    output.push_back(static_cast<char>(0x80 | ((codepoint >> 12) & 0x3F)));
    output.push_back(static_cast<char>(0x80 | ((codepoint >> 6) & 0x3F)));
    output.push_back(static_cast<char>(0x80 | (codepoint & 0x3F)));
  }
}

void append_json_escaped_char(std::string& output, std::string_view object, std::size_t& index) {
  if (index >= object.size()) return;
  const char escaped = object[index];
  switch (escaped) {
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
      const auto code_unit = parse_hex_code_unit(object, index + 1);
      if (!code_unit) {
        output.push_back('u');
        return;
      }
      if (is_high_surrogate(*code_unit)) {
        if (index + 10 < object.size() && object[index + 5] == '\\' && object[index + 6] == 'u') {
          const auto low = parse_hex_code_unit(object, index + 7);
          if (low && is_low_surrogate(*low)) {
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

std::string capped_entry_data(std::string_view data) {
  if (data.size() <= kMaxCompactionPromptEntryBytes) return std::string(data);
  return std::string(data.substr(0, kMaxCompactionPromptEntryBytes)) + "\n[entry data truncated from " +
         std::to_string(data.size()) + " bytes]";
}

std::string json_string_field(std::string_view key, std::string_view value) {
  return "\"" + std::string(key) + "\":\"" + ava::core::json::escape(value) + "\"";
}

std::string json_bool_field(std::string_view key, bool value) {
  return "\"" + std::string(key) + "\":" + (value ? "true" : "false");
}

bool json_bool_value(std::string_view object, std::string_view key) {
  const auto start = ava::core::json::field_value_start(object, key);
  if (!start) return false;
  const auto value = trim(object.substr(*start));
  if (!value.starts_with("true")) return false;
  const auto rest = trim(value.substr(4));
  return rest.empty() || rest.front() == ',' || rest.front() == '}';
}

std::string sanitized_reasoning_data_for_compaction(const ava::session::SessionEntry& entry) {
  std::string data = "{";
  bool first = true;
  auto append_string = [&](std::string_view key, const std::optional<std::string>& value) {
    if (!value || value->empty()) return;
    if (!first) data += ',';
    first = false;
    data += json_string_field(key, *value);
  };
  auto append_bool = [&](std::string_view key, bool value) {
    if (!first) data += ',';
    first = false;
    data += json_bool_field(key, value);
  };

  const bool redacted = json_bool_value(entry.data_json, "redacted");
  append_string("provider", ava::core::json::string_field(entry.data_json, "provider"));
  append_string("model", ava::core::json::string_field(entry.data_json, "model"));
  append_string("format", ava::core::json::string_field(entry.data_json, "format"));
  if (!redacted) append_string("text", ava::core::json::string_field(entry.data_json, "text"));
  append_bool("redacted", redacted);
  append_bool("signature_present", ava::core::json::string_field(entry.data_json, "signature").has_value());
  data += '}';
  return capped_entry_data(data);
}

std::string compaction_entry_data(const ava::session::SessionEntry& entry) {
  if (entry.type == ava::session::EntryType::ReasoningBlock) return sanitized_reasoning_data_for_compaction(entry);
  return capped_entry_data(entry.data_json);
}

bool is_utf8_continuation_byte(char value) { return (static_cast<unsigned char>(value) & 0xC0U) == 0x80U; }

std::size_t utf8_suffix_start(std::string_view text, std::size_t suffix_bytes) {
  if (suffix_bytes >= text.size()) return 0;
  auto start = text.size() - suffix_bytes;
  while (start < text.size() && is_utf8_continuation_byte(text[start])) ++start;
  return start;
}

std::string truncate_recent_context_to_token_budget(std::string tail, std::size_t keep_recent_tokens) {
  if (keep_recent_tokens == 0 || tail.empty()) return {};
  if (ava::session::estimate_tokens(tail) <= keep_recent_tokens) return tail;

  const std::string marker =
      "[AVA: recent context tail truncated to keep_recent_tokens=" + std::to_string(keep_recent_tokens) + "]\n";
  const auto max_bytes = keep_recent_tokens * 4;
  if (max_bytes <= marker.size()) return marker;
  const auto suffix_bytes = max_bytes - marker.size();
  if (tail.size() > suffix_bytes) {
    tail = tail.substr(utf8_suffix_start(tail, suffix_bytes));
  }
  return marker + tail;
}

void erase_replayed_active_user_messages(std::vector<ava::provider::ChatMessage>& messages,
                                         const std::vector<std::string>& replayed_user_messages) {
  for (auto replay = replayed_user_messages.rbegin(); replay != replayed_user_messages.rend(); ++replay) {
    const auto match = std::ranges::find_if(messages.rbegin(), messages.rend(), [&](const auto& message) {
      return message.role == "user" && message.content == *replay;
    });
    if (match != messages.rend()) messages.erase(std::next(match).base());
  }
}

ava::core::Result<std::string> build_recent_context_tail(const std::vector<ava::session::SessionEntry>& entries,
                                                         std::size_t keep_recent_messages,
                                                         std::size_t keep_recent_tokens,
                                                         const std::vector<std::string>& replayed_user_messages) {
  if (keep_recent_messages == 0 || keep_recent_tokens == 0) return std::string{};
  auto messages = ava::agent::build_provider_messages_from_entries(entries);
  if (!messages) return std::unexpected(std::move(messages.error()));
  if (!replayed_user_messages.empty()) {
    erase_replayed_active_user_messages(*messages, replayed_user_messages);
  }
  const auto count = std::min(keep_recent_messages, messages->size());
  const auto start = messages->size() - count;
  std::string tail;
  for (std::size_t index = start; index < messages->size(); ++index) {
    if (!tail.empty()) tail += "\n\n";
    tail += messages->at(index).role;
    tail += ":\n";
    tail += messages->at(index).content;
  }
  return truncate_recent_context_to_token_budget(std::move(tail), keep_recent_tokens);
}

ava::core::Result<std::string> parse_compaction_response_text(const ava::provider::Provider& provider,
                                                              const ava::provider::HttpResponse& response,
                                                              bool stream) {
  auto events = provider.parse_response(response, stream);
  if (!events) {
    auto error = ava::core::Error(ava::core::ErrorCategory::Provider, "compaction summary request failed with status " +
                                                                          std::to_string(response.status_code));
    error.with_context("status", std::to_string(response.status_code));
    error.with_context("provider_error", events.error().format());
    return std::unexpected(std::move(error));
  }
  std::string streamed_text;
  for (const auto& event : *events) {
    if (event.type == ava::provider::StreamEventType::TextDelta) {
      streamed_text += event.text;
    } else if (event.type == ava::provider::StreamEventType::Error && !event.error_message.empty()) {
      auto error = ava::core::Error(ava::core::ErrorCategory::Provider, "compaction summary stream error");
      error.with_context("provider_message", event.error_message);
      return std::unexpected(std::move(error));
    }
  }
  if (!streamed_text.empty()) return streamed_text;
  return std::unexpected(
      ava::core::Error(ava::core::ErrorCategory::Provider, "compaction summary response text is missing"));
}

ava::core::Result<std::filesystem::path> current_path_result() {
  std::error_code error;
  auto path = std::filesystem::current_path(error);
  if (!error) return path;
  auto result_error = ava::core::Error(ava::core::ErrorCategory::Io, "failed to resolve current directory");
  result_error.with_context("cause", error.message());
  return std::unexpected(std::move(result_error));
}

ava::core::Result<std::string> resolve_session_id(const std::filesystem::path& workspace_dir,
                                                  const std::filesystem::path& root_dir,
                                                  std::string_view requested_id) {
  auto sessions = ava::session::SessionStore::list_sessions(workspace_dir, root_dir);
  if (!sessions) return std::unexpected(sessions.error());

  std::vector<std::string> matches;
  for (const auto& session : *sessions) {
    if (session.session_id == requested_id || session.session_id.starts_with(requested_id)) {
      matches.push_back(session.session_id);
    }
  }
  if (matches.empty()) {
    auto error = ava::core::Error(ava::core::ErrorCategory::NotFound, "session not found");
    error.with_context("session_id", std::string(requested_id));
    return std::unexpected(std::move(error));
  }
  if (matches.size() > 1) {
    auto error = ava::core::Error(ava::core::ErrorCategory::InvalidArgument, "session id prefix is ambiguous");
    error.with_context("session_id", std::string(requested_id));
    error.with_context("matches", std::to_string(matches.size()));
    return std::unexpected(std::move(error));
  }
  return matches.front();
}

std::string string_array_json(const std::vector<std::string>& values);

std::vector<std::string> string_array_field(std::string_view object, std::string_view key);

std::string optional_bool_json(std::string_view key, const std::optional<bool>& value);

std::string optional_integer_json(std::string_view key, const std::optional<long long>& value);

std::string session_start_data_json(ava::agent::Mode mode, const ava::config::ModelInfo& model,
                                    const ava::config::PromptSelection& prompt, std::size_t context_source_count) {
  std::string json = "{\"mode\":\"" + ava::agent::to_string(mode) + "\",\"provider\":\"" +
                     ava::core::json::escape(model.provider_id) + "\",\"model\":\"" +
                     ava::core::json::escape(model.model_id) +
                     "\",\"prompt_override\":" + (prompt.from_override ? std::string("true") : std::string("false")) +
                     ",\"context_sources\":" + std::to_string(context_source_count);
  json += ",\"input_modalities\":" + string_array_json(model.input_modalities);
  json += ",\"output_modalities\":" + string_array_json(model.output_modalities);
  json += ",\"reasoning_levels\":" + string_array_json(model.reasoning_levels);
  json += ",\"compatibility_quirks\":" + string_array_json(model.compatibility_quirks);
  json += optional_bool_json("supports_tools", model.supports_tools);
  json += optional_bool_json("supports_streaming", model.supports_streaming);
  json += optional_bool_json("supports_reasoning", model.supports_reasoning);
  json += optional_bool_json("reports_usage", model.reports_usage);
  if (!model.display_name.empty()) {
    json += ",\"display_name\":\"" + ava::core::json::escape(model.display_name) + "\"";
  }
  if (!model.family.empty()) json += ",\"family\":\"" + ava::core::json::escape(model.family) + "\"";
  if (!model.api_family.empty()) {
    json += ",\"api_family\":\"" + ava::core::json::escape(model.api_family) + "\"";
  }
  if (model.context_window_tokens) json += ",\"context_window_tokens\":" + std::to_string(*model.context_window_tokens);
  if (model.max_output_tokens) json += ",\"max_output_tokens\":" + std::to_string(*model.max_output_tokens);
  if (!model.reasoning_format.empty()) {
    json += ",\"reasoning_format\":\"" + ava::core::json::escape(model.reasoning_format) + "\"";
  }
  json += '}';
  return json;
}

ava::core::VoidResult append_session_start(ava::session::SessionStore& store, ava::agent::Mode mode,
                                           const ava::config::ModelInfo& model,
                                           const ava::config::PromptSelection& prompt,
                                           std::size_t context_source_count) {
  return store.append(ava::session::SessionEntry{
      .id = ava::core::make_id("entry"),
      .parent_id = "",
      .type = ava::session::EntryType::SessionStart,
      .timestamp = ava::session::now_timestamp(),
      .data_json = session_start_data_json(mode, model, prompt, context_source_count),
  });
}

std::string optional_bool_json(std::string_view key, const std::optional<bool>& value) {
  if (!value) return {};
  return ",\"" + std::string(key) + "\":" + (*value ? "true" : "false");
}

std::string optional_integer_json(std::string_view key, const std::optional<long long>& value) {
  if (!value) return {};
  return ",\"" + std::string(key) + "\":" + std::to_string(*value);
}

std::string string_array_json(const std::vector<std::string>& values) {
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

std::vector<std::string> string_array_field(std::string_view object, std::string_view key) {
  const auto start = ava::core::json::field_value_start(object, key);
  if (!start || *start >= object.size() || object[*start] != '[') return {};

  std::vector<std::string> values;
  bool in_string = false;
  bool escaped = false;
  bool collecting = false;
  int depth = 1;
  std::string current;
  for (std::size_t index = *start + 1; index < object.size(); ++index) {
    const char ch = object[index];
    if (escaped) {
      if (collecting) {
        auto escape_index = index;
        append_json_escaped_char(current, object, escape_index);
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

std::string model_change_data_json(const ava::config::ModelInfo& previous, const ava::config::ModelInfo& current) {
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
  if (!current.reasoning_format.empty()) {
    json += ",\"reasoning_format\":\"" + ava::core::json::escape(current.reasoning_format) + "\"";
  }
  json += "}";
  return json;
}

ava::core::VoidResult append_model_change(ava::session::SessionStore& store, const ava::config::ModelInfo& previous,
                                          const ava::config::ModelInfo& current) {
  return store.append(ava::session::SessionEntry{.id = ava::core::make_id("entry"),
                                                 .parent_id = "",
                                                 .type = ava::session::EntryType::ModelChange,
                                                 .timestamp = ava::session::now_timestamp(),
                                                 .data_json = model_change_data_json(previous, current)});
}

bool has_reasoning_level(const ava::config::ModelInfo& model, std::string_view level) {
  return std::ranges::find(model.reasoning_levels, level) != model.reasoning_levels.end();
}

std::string reasoning_change_data_json(const ava::config::ModelInfo& model,
                                       const std::optional<RuntimeReasoningSelection>& selection) {
  std::string json = "{\"provider\":\"" + ava::core::json::escape(model.provider_id) + "\",\"model\":\"" +
                     ava::core::json::escape(model.model_id) + "\"";
  if (!model.reasoning_format.empty()) {
    json += ",\"format\":\"" + ava::core::json::escape(model.reasoning_format) + "\"";
  }
  json += ",\"enabled\":";
  json += selection ? "true" : "false";
  if (selection) {
    json += ",\"level\":\"" + ava::core::json::escape(selection->level) + "\"";
    if (selection->budget_tokens) json += ",\"budget_tokens\":" + std::to_string(*selection->budget_tokens);
    if (!selection->display.empty()) json += ",\"display\":\"" + ava::core::json::escape(selection->display) + "\"";
  }
  json += '}';
  return json;
}

ava::core::VoidResult append_reasoning_change(ava::session::SessionStore& store, const ava::config::ModelInfo& model,
                                              const std::optional<RuntimeReasoningSelection>& selection) {
  return store.append(ava::session::SessionEntry{.id = ava::core::make_id("entry"),
                                                 .parent_id = "",
                                                 .type = ava::session::EntryType::ReasoningChange,
                                                 .timestamp = ava::session::now_timestamp(),
                                                 .data_json = reasoning_change_data_json(model, selection)});
}

ava::provider::ProviderReasoningOptions provider_reasoning_options(const RuntimeReasoningSelection& selection) {
  return ava::provider::ProviderReasoningOptions{
      .type = selection.level, .budget_tokens = selection.budget_tokens, .display = selection.display};
}

bool same_reasoning_selection(const std::optional<RuntimeReasoningSelection>& left,
                              const std::optional<RuntimeReasoningSelection>& right) {
  if (!left || !right) return !left && !right;
  return left->level == right->level && left->budget_tokens == right->budget_tokens && left->display == right->display;
}

ava::core::VoidResult validate_reasoning_selection(const ava::config::ModelInfo& model,
                                                   const RuntimeReasoningSelection& selection) {
  const auto level = trim(selection.level);
  if (level.empty()) {
    return std::unexpected(ava::core::Error(ava::core::ErrorCategory::InvalidArgument, "reasoning level is required"));
  }
  if (!model.supports_reasoning.value_or(false)) {
    auto error =
        ava::core::Error(ava::core::ErrorCategory::InvalidArgument, "current model does not declare reasoning support");
    error.with_context("provider", model.provider_id);
    error.with_context("model", model.model_id);
    return std::unexpected(std::move(error));
  }
  if (model.reasoning_levels.empty()) {
    auto error = ava::core::Error(ava::core::ErrorCategory::InvalidArgument,
                                  "current model does not declare supported reasoning levels");
    error.with_context("provider", model.provider_id);
    error.with_context("model", model.model_id);
    return std::unexpected(std::move(error));
  }
  if (!model.reasoning_levels.empty() && !has_reasoning_level(model, level)) {
    auto error = ava::core::Error(ava::core::ErrorCategory::InvalidArgument,
                                  "reasoning level is not supported by the current model");
    error.with_context("provider", model.provider_id);
    error.with_context("model", model.model_id);
    error.with_context("level", std::string(level));
    return std::unexpected(std::move(error));
  }
  if (selection.budget_tokens && *selection.budget_tokens <= 0) {
    return std::unexpected(
        ava::core::Error(ava::core::ErrorCategory::InvalidArgument, "reasoning budget must be positive"));
  }
  if (auto valid = ava::config::validate_reasoning_request(model, level, selection.budget_tokens, selection.display);
      !valid) {
    return std::unexpected(std::move(valid.error()));
  }
  return {};
}

std::optional<bool> bool_json_field(std::string_view object, std::string_view key) {
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

ava::config::ModelInfo fallback_persisted_model(
    std::string provider_id, std::string model_id, std::string display_name, std::string family, std::string api_family,
    std::optional<long long> context_window_tokens, std::optional<long long> max_output_tokens,
    std::optional<bool> supports_tools, std::optional<bool> supports_streaming, std::optional<bool> supports_reasoning,
    std::optional<bool> reports_usage, std::vector<std::string> input_modalities,
    std::vector<std::string> output_modalities, std::vector<std::string> reasoning_levels,
    std::vector<std::string> compatibility_quirks, std::string reasoning_format) {
  if (display_name.empty()) display_name = model_id;
  if (family.empty()) family = model_id;
  if (compatibility_quirks.empty()) compatibility_quirks = {"persisted_unknown_model"};
  return ava::config::ModelInfo{.provider_id = std::move(provider_id),
                                .model_id = std::move(model_id),
                                .display_name = std::move(display_name),
                                .family = std::move(family),
                                .context_window_tokens = context_window_tokens,
                                .max_output_tokens = max_output_tokens,
                                .pricing = std::nullopt,
                                .api_family = std::move(api_family),
                                .input_modalities = std::move(input_modalities),
                                .supports_tools = supports_tools,
                                .supports_streaming = supports_streaming,
                                .supports_reasoning = supports_reasoning,
                                .reports_usage = reports_usage,
                                .reasoning_levels = std::move(reasoning_levels),
                                .compatibility_quirks = std::move(compatibility_quirks),
                                .output_modalities = std::move(output_modalities),
                                .reasoning_format = std::move(reasoning_format)};
}

std::optional<ava::config::ModelInfo> latest_persisted_model(const ava::config::ModelRegistry& registry,
                                                             const std::vector<ava::session::SessionEntry>& entries) {
  std::optional<std::string> provider_id;
  std::optional<std::string> model_id;
  std::string display_name;
  std::string family;
  std::string api_family;
  std::optional<long long> context_window_tokens;
  std::optional<long long> max_output_tokens;
  std::optional<bool> supports_tools;
  std::optional<bool> supports_streaming;
  std::optional<bool> supports_reasoning;
  std::optional<bool> reports_usage;
  std::vector<std::string> input_modalities;
  std::vector<std::string> output_modalities;
  std::vector<std::string> reasoning_levels;
  std::vector<std::string> compatibility_quirks;
  std::string reasoning_format;
  for (const auto& entry : entries) {
    if (entry.type != ava::session::EntryType::SessionStart && entry.type != ava::session::EntryType::ModelChange) {
      continue;
    }
    auto provider = ava::core::json::string_field(entry.data_json, "provider");
    auto model = ava::core::json::string_field(entry.data_json, "model");
    if (!provider || !model || provider->empty() || model->empty()) continue;
    provider_id = std::move(*provider);
    model_id = std::move(*model);
    display_name = ava::core::json::string_field(entry.data_json, "display_name").value_or("");
    family = ava::core::json::string_field(entry.data_json, "family").value_or("");
    api_family = ava::core::json::string_field(entry.data_json, "api_family").value_or("");
    context_window_tokens = ava::core::json::integer_field(entry.data_json, "context_window_tokens");
    max_output_tokens = ava::core::json::integer_field(entry.data_json, "max_output_tokens");
    supports_tools = bool_json_field(entry.data_json, "supports_tools");
    supports_streaming = bool_json_field(entry.data_json, "supports_streaming");
    supports_reasoning = bool_json_field(entry.data_json, "supports_reasoning");
    reports_usage = bool_json_field(entry.data_json, "reports_usage");
    input_modalities = string_array_field(entry.data_json, "input_modalities");
    output_modalities = string_array_field(entry.data_json, "output_modalities");
    reasoning_levels = string_array_field(entry.data_json, "reasoning_levels");
    compatibility_quirks = string_array_field(entry.data_json, "compatibility_quirks");
    reasoning_format = ava::core::json::string_field(entry.data_json, "reasoning_format").value_or("");
  }
  if (!provider_id || !model_id) return std::nullopt;
  if (auto model = ava::config::find_model(registry, *provider_id, *model_id)) return model;
  return fallback_persisted_model(
      std::move(*provider_id), std::move(*model_id), std::move(display_name), std::move(family), std::move(api_family),
      context_window_tokens, max_output_tokens, supports_tools, supports_streaming, supports_reasoning, reports_usage,
      std::move(input_modalities), std::move(output_modalities), std::move(reasoning_levels),
      std::move(compatibility_quirks), std::move(reasoning_format));
}

std::optional<RuntimeReasoningSelection> latest_persisted_reasoning(
    const std::vector<ava::session::SessionEntry>& entries, const ava::config::ModelInfo& model) {
  std::optional<RuntimeReasoningSelection> latest;
  bool saw_change = false;
  for (const auto& entry : entries) {
    if (entry.type == ava::session::EntryType::SessionStart || entry.type == ava::session::EntryType::ModelChange) {
      latest = std::nullopt;
      continue;
    }
    if (entry.type != ava::session::EntryType::ReasoningChange) continue;
    auto provider = ava::core::json::string_field(entry.data_json, "provider");
    auto model_id = ava::core::json::string_field(entry.data_json, "model");
    if (!provider || !model_id || *provider != model.provider_id || *model_id != model.model_id) {
      saw_change = true;
      latest = std::nullopt;
      continue;
    }
    saw_change = true;
    if (bool_json_field(entry.data_json, "enabled") == false) {
      latest = std::nullopt;
      continue;
    }
    auto level = ava::core::json::string_field(entry.data_json, "level").value_or("");
    if (level.empty()) {
      latest = std::nullopt;
      continue;
    }
    latest =
        RuntimeReasoningSelection{.level = std::move(level),
                                  .budget_tokens = ava::core::json::integer_field(entry.data_json, "budget_tokens"),
                                  .display = ava::core::json::string_field(entry.data_json, "display").value_or("")};
  }
  if (!saw_change || !latest) return std::nullopt;
  if (auto valid = validate_reasoning_selection(model, *latest); !valid) return std::nullopt;
  return latest;
}

bool model_accepts_reasoning_format(const ava::config::ModelInfo& model, std::string_view format) {
  return ava::config::provider_accepts_reasoning_format(model, format);
}

ava::core::Error incompatible_model_switch_error(const ava::config::ModelInfo& model, std::string_view reason,
                                                 const ava::session::SessionEntry& entry) {
  auto error = ava::core::Error(ava::core::ErrorCategory::InvalidArgument,
                                "model switch cannot safely replay current session history");
  error.with_context("provider", model.provider_id);
  error.with_context("model", model.model_id);
  error.with_context("reason", std::string(reason));
  error.with_context("entry_id", entry.id);
  error.with_context("entry_type", ava::session::to_string(entry.type));
  return error;
}

ava::core::VoidResult validate_model_switch_history(const RuntimeSession& session,
                                                    const ava::config::ModelInfo& target) {
  auto entries = session.store.load();
  if (!entries) return std::unexpected(std::move(entries.error()));

  auto replay_start = entries->begin();
  for (auto it = entries->begin(); it != entries->end(); ++it) {
    if (it->type == ava::session::EntryType::Compaction) replay_start = std::next(it);
  }

  for (auto it = replay_start; it != entries->end(); ++it) {
    const auto& entry = *it;
    if ((entry.type == ava::session::EntryType::ToolCall || entry.type == ava::session::EntryType::ToolResult) &&
        !target.supports_tools.value_or(false)) {
      return std::unexpected(incompatible_model_switch_error(
          target, "target model does not declare tool support required by existing tool history", entry));
    }

    if (entry.type != ava::session::EntryType::ReasoningBlock) continue;
    const auto format = ava::core::json::string_field(entry.data_json, "format").value_or("");
    if (model_accepts_reasoning_format(target, format)) continue;

    auto error = incompatible_model_switch_error(
        target,
        format.empty() ? "target model cannot safely replay provider-native reasoning history without a format"
                       : "target model cannot replay provider-native reasoning format",
        entry);
    if (!format.empty()) error.with_context("reasoning_format", format);
    return std::unexpected(std::move(error));
  }

  return {};
}

RuntimeEvent base_event(const RuntimeSession& session, RuntimeEventType type) {
  RuntimeEvent event;
  event.type = type;
  event.timestamp = ava::session::now_timestamp();
  event.session_id = session.store.session_id();
  event.mode = session.mode;
  event.provider_id = session.model.provider_id;
  event.model_id = session.model.model_id;
  return event;
}

RuntimeEvent base_event_locked(const RuntimeSession& session, RuntimeEventType type, std::mutex* mutex) {
  if (!mutex) return base_event(session, type);
  std::lock_guard lock(*mutex);
  return base_event(session, type);
}

ava::core::Result<RuntimePromptState> load_runtime_prompt_state(const ava::config::XdgPaths& paths,
                                                                const ava::config::ModelInfo& model,
                                                                ava::agent::Mode mode,
                                                                const std::filesystem::path& workspace_dir,
                                                                const std::filesystem::path& current_dir) {
  auto prompt = ava::config::select_prompt(paths, model, mode);
  if (!prompt) return std::unexpected(prompt.error());

  auto loaded_context = ava::context::load_context_files(ava::context::ContextLoadOptions{
      .workspace_root = workspace_dir,
      .current_dir = current_dir,
      .global_agents_file = paths.global_agents_file,
  });
  if (!loaded_context) return std::unexpected(loaded_context.error());

  std::vector<ContextSourceMetadata> context_sources;
  context_sources.reserve(loaded_context->size());
  for (const auto& file : *loaded_context) {
    context_sources.push_back(
        ContextSourceMetadata{.path = file.path, .source_type = file.source_type, .byte_count = file.byte_count});
  }

  auto system_prompt = prompt->text + ava::context::format_context_for_prompt(*loaded_context);
  return RuntimePromptState{.mode = mode,
                            .prompt = std::move(*prompt),
                            .context_sources = std::move(context_sources),
                            .system_prompt = std::move(system_prompt)};
}

}  // namespace

bool same_session_snapshot(const std::vector<ava::session::SessionEntry>& expected,
                           const std::vector<ava::session::SessionEntry>& actual) {
  if (expected.size() != actual.size()) return false;
  for (std::size_t index = 0; index < expected.size(); ++index) {
    if (expected[index].id != actual[index].id || expected[index].parent_id != actual[index].parent_id ||
        expected[index].type != actual[index].type || expected[index].timestamp != actual[index].timestamp ||
        expected[index].data_json != actual[index].data_json) {
      return false;
    }
  }
  return true;
}

ava::core::Error stale_compaction_snapshot_error(std::string_view trigger, std::size_t snapshot_entries,
                                                 std::size_t current_entries) {
  auto error =
      ava::core::Error(ava::core::ErrorCategory::Session, "session changed during context compaction after retry");
  error.with_context("trigger", std::string(trigger));
  error.with_context("snapshot_entries", std::to_string(snapshot_entries));
  error.with_context("current_entries", std::to_string(current_entries));
  return error;
}

ava::core::Result<RuntimeSession> open_runtime_session(const RuntimeOpenOptions& options) {
  if (options.requested_session_id && options.continue_last_session) {
    return std::unexpected(ava::core::Error(ava::core::ErrorCategory::InvalidArgument,
                                            "use either requested session id or continue, not both"));
  }

  auto cwd = current_path_result();
  if (!cwd) return std::unexpected(std::move(cwd.error()));
  const auto workspace_dir = options.workspace_dir.empty() ? *cwd : options.workspace_dir;
  const auto current_dir = options.current_dir.empty() ? workspace_dir : options.current_dir;

  auto registry = ava::config::load_model_registry(options.paths);
  if (!registry) return std::unexpected(registry.error());
  auto model = ava::config::select_default_model(*registry);

  bool created = true;
  ava::core::Result<ava::session::SessionStore> store =
      std::unexpected(ava::core::Error(ava::core::ErrorCategory::Unknown, "session was not initialized"));
  if (options.requested_session_id) {
    auto resolved = resolve_session_id(workspace_dir, options.paths.sessions_dir, *options.requested_session_id);
    if (!resolved) return std::unexpected(resolved.error());
    store = ava::session::SessionStore::open(workspace_dir, *resolved, options.paths.sessions_dir);
    created = false;
  } else if (options.continue_last_session) {
    auto sessions = ava::session::SessionStore::list_sessions(workspace_dir, options.paths.sessions_dir);
    if (!sessions) return std::unexpected(sessions.error());
    if (!sessions->empty()) {
      store = ava::session::SessionStore::open(workspace_dir, sessions->front().session_id, options.paths.sessions_dir);
      created = false;
    } else {
      store = ava::session::SessionStore::create(workspace_dir, options.paths.sessions_dir);
    }
  } else {
    store = ava::session::SessionStore::create(workspace_dir, options.paths.sessions_dir);
  }
  if (!store) return std::unexpected(store.error());

  std::optional<std::vector<ava::session::SessionEntry>> loaded_entries;
  if (!created) {
    auto entries = store->load();
    if (!entries) return std::unexpected(std::move(entries.error()));
    if (auto persisted_model = latest_persisted_model(*registry, *entries)) model = std::move(*persisted_model);
    loaded_entries = std::move(*entries);
  }

  std::optional<RuntimeReasoningSelection> reasoning;
  if (loaded_entries) reasoning = latest_persisted_reasoning(*loaded_entries, model);

  auto prompt_state = load_runtime_prompt_state(options.paths, model, options.mode, workspace_dir, current_dir);
  if (!prompt_state) return std::unexpected(prompt_state.error());

  if (created) {
    auto appended =
        append_session_start(*store, options.mode, model, prompt_state->prompt, prompt_state->context_sources.size());
    if (!appended) return std::unexpected(appended.error());
  }

  return RuntimeSession{.store = std::move(*store),
                        .mode = options.mode,
                        .model = std::move(model),
                        .prompt = std::move(prompt_state->prompt),
                        .paths = options.paths,
                        .workspace_dir = workspace_dir,
                        .current_dir = current_dir,
                        .context_sources = std::move(prompt_state->context_sources),
                        .system_prompt = std::move(prompt_state->system_prompt),
                        .reasoning = std::move(reasoning),
                        .created = created};
}

ava::core::Result<ava::config::ModelInfo> resolve_runtime_model(const ava::config::XdgPaths& paths,
                                                                std::string_view provider_id,
                                                                std::string_view model_id) {
  provider_id = trim(provider_id);
  model_id = trim(model_id);
  if (provider_id.empty() || model_id.empty()) {
    return std::unexpected(
        ava::core::Error(ava::core::ErrorCategory::InvalidArgument, "provider and model are required"));
  }

  const auto providers = ava::provider::builtin_provider_registry();
  if (!providers.contains(provider_id)) {
    auto error = ava::core::Error(ava::core::ErrorCategory::NotFound, "provider is not registered");
    error.with_context("provider", std::string(provider_id));
    return std::unexpected(std::move(error));
  }

  auto registry = ava::config::load_model_registry(paths);
  if (!registry) return std::unexpected(std::move(registry.error()));
  auto model = ava::config::find_model(*registry, provider_id, model_id);
  if (!model) {
    auto error = ava::core::Error(ava::core::ErrorCategory::NotFound, "model is not configured");
    error.with_context("provider", std::string(provider_id));
    error.with_context("model", std::string(model_id));
    return std::unexpected(std::move(error));
  }
  return *model;
}

ava::core::Result<bool> switch_runtime_model(RuntimeSession& session, ava::config::ModelInfo model) {
  if (session.model.provider_id == model.provider_id && session.model.model_id == model.model_id) return false;

  auto compatible = validate_model_switch_history(session, model);
  if (!compatible) return std::unexpected(std::move(compatible.error()));

  auto prompt_state =
      load_runtime_prompt_state(session.paths, model, session.mode, session.workspace_dir, session.current_dir);
  if (!prompt_state) return std::unexpected(std::move(prompt_state.error()));

  const auto previous = session.model;
  auto appended = append_model_change(session.store, previous, model);
  if (!appended) return std::unexpected(std::move(appended.error()));

  session.model = std::move(model);
  session.mode = prompt_state->mode;
  session.prompt = std::move(prompt_state->prompt);
  session.context_sources = std::move(prompt_state->context_sources);
  session.system_prompt = std::move(prompt_state->system_prompt);
  session.reasoning = std::nullopt;
  return true;
}

ava::core::Result<bool> set_runtime_reasoning(RuntimeSession& session,
                                              std::optional<RuntimeReasoningSelection> selection) {
  if (selection) {
    selection->level = trimmed_copy(selection->level);
    selection->display = trimmed_copy(selection->display);
    if (auto valid = validate_reasoning_selection(session.model, *selection); !valid) {
      return std::unexpected(std::move(valid.error()));
    }
  }
  if (same_reasoning_selection(session.reasoning, selection)) return false;

  auto appended = append_reasoning_change(session.store, session.model, selection);
  if (!appended) return std::unexpected(std::move(appended.error()));
  session.reasoning = std::move(selection);
  return true;
}

ava::core::Result<RuntimePromptState> select_runtime_prompt_state(const RuntimeSession& session,
                                                                  ava::agent::Mode mode) {
  return load_runtime_prompt_state(session.paths, session.model, mode, session.workspace_dir, session.current_dir);
}

void apply_runtime_prompt_state(RuntimeSession& session, RuntimePromptState prompt_state) {
  session.mode = prompt_state.mode;
  session.prompt = std::move(prompt_state.prompt);
  session.context_sources = std::move(prompt_state.context_sources);
  session.system_prompt = std::move(prompt_state.system_prompt);
}

std::string build_compaction_summary_prompt(const std::vector<ava::session::SessionEntry>& entries,
                                            const ava::session::CompactionConfig& config, std::string_view instructions,
                                            std::size_t estimated_tokens) {
  std::string prompt;
  prompt += "Generate a provider-backed AVA /compact summary for the session below.\n";
  prompt += "Return only Markdown with exactly these top-level sections, in this order:\n";
  prompt +=
      "# Goal\n# Constraints / Preferences\n# Decisions\n# Files Read or Modified\n# Unresolved Tasks\n# Next "
      "Steps\n\n";
  prompt += "Rules:\n";
  prompt += "- Be faithful to the session entries; do not invent facts.\n";
  prompt += "- Prefer concise bullets that preserve information needed to continue the work.\n";
  prompt += "- Include files read or modified when visible in tool calls/results or messages.\n";
  prompt += "- If a section has no known facts, write \"None noted.\"\n";
  prompt += "- Keep the complete response under " + std::to_string(config.max_summary_bytes) + " bytes.\n\n";
  prompt += "User compaction instructions:\n";
  prompt += instructions.empty() ? "(none)\n\n" : std::string(instructions) + "\n\n";
  prompt += "Compaction metadata:\n";
  prompt += "- estimated_tokens: " + std::to_string(estimated_tokens) + "\n";
  prompt += "- threshold_tokens: " + std::to_string(config.auto_threshold_tokens) + "\n";
  prompt += "- keep_recent_tokens: " + std::to_string(config.keep_recent_tokens) + "\n";
  prompt += "- keep_recent_messages: " + std::to_string(config.keep_recent_messages) + "\n";
  prompt += "- summary_model: " + config.model_id + "\n\n";
  prompt += "Session entries in chronological order:\n";
  std::size_t visible_index = 0;
  for (std::size_t index = 0; index < entries.size(); ++index) {
    const auto& entry = entries[index];
    if (ava::session::is_internal_replay_user_message(entry)) continue;
    ++visible_index;
    prompt += "\n## Entry " + std::to_string(visible_index) + "\n";
    prompt += "type: " + ava::session::to_string(entry.type) + "\n";
    if (!entry.timestamp.empty()) prompt += "timestamp: " + entry.timestamp + "\n";
    prompt += "data_json:\n";
    prompt += compaction_entry_data(entry);
    prompt += "\n";
  }
  return prompt;
}

ava::core::Result<std::string> generate_compaction_summary(
    const RuntimeSession& session, const std::vector<ava::session::SessionEntry>& entries,
    const ava::session::CompactionConfig& config, std::string_view instructions, std::size_t estimated_tokens,
    const ava::provider::Provider& provider, ava::provider::Transport& transport, const RuntimeRunOptions& options) {
  if (options.access_token.empty()) {
    return std::unexpected(
        ava::core::Error(ava::core::ErrorCategory::PermissionDenied, "compaction requires provider access token"));
  }

  constexpr std::string_view system_prompt =
      "You are AVA's deterministic session compaction summarizer. Create faithful continuation context from the "
      "provided session record. Return only the requested Markdown summary; do not include prefaces or code fences.";
  const auto prompt = build_compaction_summary_prompt(entries, config, instructions, estimated_tokens);
  const ava::provider::ProviderRequest provider_request{
      .provider_id = session.model.provider_id,
      .model_id = config.model_id,
      .system_prompt = std::string(system_prompt),
      .messages = {ava::provider::ChatMessage{.role = "user", .content = prompt}},
      .tools_json = {},
      .stream = options.openai_oauth && session.model.supports_streaming.value_or(true),
      .max_output_tokens = session.model.max_output_tokens};
  const ava::provider::ProviderAuthContext auth_context{
      .access_token = options.access_token,
      .credential_type =
          options.openai_oauth && options.credential_type == "bearer" ? "oauth" : options.credential_type,
      .account_id = options.openai_account_id};
  auto request = provider.build_request(provider_request, auth_context);
  if (!request) return std::unexpected(std::move(request.error()));

  auto response = transport.send(*request);
  if (!response) return std::unexpected(std::move(response.error()));
  auto summary = parse_compaction_response_text(provider, *response, provider_request.stream);
  if (!summary) return std::unexpected(std::move(summary.error()));
  *summary = trimmed_copy(*summary);
  if (summary->empty()) {
    return std::unexpected(ava::core::Error(ava::core::ErrorCategory::Provider,
                                            "compaction summary generation returned an empty summary"));
  }
  if (summary->size() > config.max_summary_bytes) {
    auto error =
        ava::core::Error(ava::core::ErrorCategory::InvalidArgument, "generated compaction summary is too large");
    error.with_context("max_summary_bytes", std::to_string(config.max_summary_bytes));
    error.with_context("summary_bytes", std::to_string(summary->size()));
    return std::unexpected(std::move(error));
  }
  return *summary;
}

ava::core::Result<bool> compact_runtime_context(RuntimeSession& session, ava::session::SessionStore& store,
                                                std::string_view trigger, const ava::provider::Provider& provider,
                                                ava::provider::Transport& transport, const RuntimeRunOptions& options,
                                                const std::vector<std::string>& replayed_user_messages) {
  if (options.access_token.empty()) {
    return std::unexpected(
        ava::core::Error(ava::core::ErrorCategory::PermissionDenied, "compaction requires provider access token"));
  }

  auto config = ava::session::load_compaction_config(session.paths);
  if (!config) return std::unexpected(std::move(config.error()));

  constexpr std::size_t max_compaction_attempts = 2;
  const auto trigger_text = std::string(trigger);
  std::size_t last_snapshot_entries = 0;
  std::size_t last_current_entries = 0;
  for (std::size_t attempt = 0; attempt < max_compaction_attempts; ++attempt) {
    if (options.cancel_requested && options.cancel_requested()) {
      return std::unexpected(ava::core::Error(ava::core::ErrorCategory::Unknown, "agent loop canceled"));
    }

    ava::core::Result<std::vector<ava::session::SessionEntry>> entries =
        std::unexpected(ava::core::Error(ava::core::ErrorCategory::Unknown, "session entries were not loaded"));
    if (options.session_mutex) {
      std::lock_guard lock(*options.session_mutex);
      entries = store.load();
    } else {
      entries = store.load();
    }
    if (!entries) return std::unexpected(std::move(entries.error()));

    const auto threshold = ava::session::effective_auto_threshold_tokens(*config, session.model.context_window_tokens);
    std::size_t estimated_tokens = ava::session::estimate_session_tokens(*entries);
    std::size_t threshold_tokens = threshold;
    if (trigger == "auto") {
      const auto decision = ava::session::should_auto_compact(*entries, *config, session.model.context_window_tokens);
      if (!decision.should_compact) return false;
      estimated_tokens = decision.estimated_tokens;
      threshold_tokens = decision.threshold_tokens;
    }

    auto summary =
        generate_compaction_summary(session, *entries, *config, "", estimated_tokens, provider, transport, options);
    if (!summary) return std::unexpected(std::move(summary.error()));
    auto recent_context = build_recent_context_tail(*entries, config->keep_recent_messages, config->keep_recent_tokens,
                                                    replayed_user_messages);
    if (!recent_context) return std::unexpected(std::move(recent_context.error()));
    if (options.cancel_requested && options.cancel_requested()) {
      return std::unexpected(ava::core::Error(ava::core::ErrorCategory::Unknown, "agent loop canceled"));
    }

    bool snapshot_stale = false;
    auto validate_and_append = [&]() -> ava::core::Result<bool> {
      auto current_entries = store.load();
      if (!current_entries) return std::unexpected(std::move(current_entries.error()));
      if (!same_session_snapshot(*entries, *current_entries)) {
        snapshot_stale = true;
        last_snapshot_entries = entries->size();
        last_current_entries = current_entries->size();
        return false;
      }
      auto appended = ava::session::append_manual_compaction(
          store, ava::session::ManualCompactionRequest{.summary = *summary,
                                                       .instructions = "",
                                                       .config = *config,
                                                       .estimated_tokens = estimated_tokens,
                                                       .threshold_tokens = threshold_tokens,
                                                       .trigger = trigger_text,
                                                       .recent_context = *recent_context});
      if (!appended) return std::unexpected(std::move(appended.error()));
      return true;
    };
    ava::core::Result<bool> appended = false;
    if (options.session_mutex) {
      std::lock_guard lock(*options.session_mutex);
      appended = validate_and_append();
    } else {
      appended = validate_and_append();
    }
    if (!appended) return std::unexpected(std::move(appended.error()));
    if (*appended) return true;
    if (!snapshot_stale) return false;
  }
  return std::unexpected(stale_compaction_snapshot_error(trigger_text, last_snapshot_entries, last_current_entries));
}

ava::core::Result<ava::agent::AgentLoopResult> run_prompt(RuntimeSession& session, const std::string& user_message,
                                                          const ava::provider::Provider& provider,
                                                          ava::provider::Transport& transport,
                                                          const RuntimeRunOptions& options) {
  auto event_sink = make_plugin_event_observer_sink(
      plugin_event_observer_options(session, options.permission_resolver, options.session_mutex), options.event_sink);
  auto session_event = base_event_locked(session, RuntimeEventType::SessionStart, options.session_mutex);
  if (auto emitted = emit_event(event_sink, session_event); !emitted) {
    return std::unexpected(std::move(emitted.error()));
  }

  auto user_event = base_event_locked(session, RuntimeEventType::UserMessage, options.session_mutex);
  user_event.text = user_message;
  if (auto emitted = emit_event(event_sink, user_event); !emitted) {
    return std::unexpected(std::move(emitted.error()));
  }

  std::optional<ava::core::Error> sink_error;
  ava::agent::AgentLoop loop(ava::agent::AgentLoopOptions{
      .workspace_dir = session.workspace_dir,
      .mode = session.mode,
      .provider_id = session.model.provider_id,
      .model_id = session.model.model_id,
      .system_prompt = session.system_prompt,
      .access_token = options.access_token,
      .credential_type =
          options.openai_oauth && options.credential_type == "bearer" ? "oauth" : options.credential_type,
      .openai_oauth = options.openai_oauth,
      .openai_account_id = options.openai_account_id,
      .stream = options.stream,
      .model_supports_tools = session.model.supports_tools.value_or(true),
      .model_supports_streaming = session.model.supports_streaming.value_or(true),
      .model_max_output_tokens = session.model.max_output_tokens,
      .reasoning = session.reasoning ? std::optional(provider_reasoning_options(*session.reasoning)) : std::nullopt,
      .on_tool_event =
          [&session, &options, &event_sink, &sink_error](const ava::agent::ToolTimelineEntry& entry) {
            if (sink_error) return;
            auto event = base_event_locked(session,
                                           entry.status == ava::agent::ToolTimelineStatus::Running
                                               ? RuntimeEventType::ToolStart
                                               : RuntimeEventType::ToolResult,
                                           options.session_mutex);
            event.call_id = entry.call_id;
            event.tool_name = entry.name;
            event.text =
                entry.status == ava::agent::ToolTimelineStatus::Running ? entry.argument_summary : entry.result_summary;
            event.status = ava::agent::to_string(entry.status);
            if (auto emitted = emit_event(event_sink, event); !emitted) {
              sink_error = std::move(emitted.error());
            }
          },
      .on_tool_progress = [&session, &options, &event_sink,
                           &sink_error](const ava::agent::ToolProgressEntry& entry) -> ava::core::VoidResult {
        if (sink_error) return std::unexpected(*sink_error);
        auto event = base_event_locked(session, RuntimeEventType::ToolProgress, options.session_mutex);
        event.call_id = entry.call_id;
        event.tool_name = entry.name;
        event.text = entry.text;
        event.status = entry.status;
        if (auto emitted = emit_event(event_sink, event); !emitted) {
          sink_error = std::move(emitted.error());
          return std::unexpected(*sink_error);
        }
        return {};
      },
      .on_stream_event = [&session, &options, &event_sink,
                          &sink_error](const ava::provider::StreamEvent& stream_event) -> ava::core::VoidResult {
        if (sink_error) return std::unexpected(*sink_error);
        auto event = base_event_locked(
            session,
            stream_event.type == ava::provider::StreamEventType::TextDelta        ? RuntimeEventType::MessageUpdate
            : stream_event.type == ava::provider::StreamEventType::ReasoningStart ? RuntimeEventType::ReasoningStart
            : stream_event.type == ava::provider::StreamEventType::ReasoningDelta ? RuntimeEventType::ReasoningDelta
            : stream_event.type == ava::provider::StreamEventType::ReasoningEnd   ? RuntimeEventType::ReasoningEnd
            : stream_event.type == ava::provider::StreamEventType::Done           ? RuntimeEventType::MessageEnd
                                                                                  : RuntimeEventType::ProviderEvent,
            options.session_mutex);
        event.text = stream_event.text;
        event.call_id = stream_event.tool_call_id;
        event.tool_name = stream_event.tool_name;
        event.status = ava::provider::to_string(stream_event.type);
        event.error_message = stream_event.error_message;
        event.stop_reason = stream_event.stop_reason;
        event.reasoning_format = stream_event.reasoning_format;
        event.reasoning_redacted = stream_event.redacted;
        event.reasoning_signature_present =
            stream_event.reasoning_signature_present || !stream_event.reasoning_signature.empty();
        if (auto emitted = emit_event(event_sink, event); !emitted) {
          sink_error = std::move(emitted.error());
          return std::unexpected(*sink_error);
        }
        return {};
      },
      .permission_resolver = options.permission_resolver,
      .question_resolver = options.question_resolver,
      .cancel_requested =
          [&options, &sink_error] {
            return sink_error.has_value() || (options.cancel_requested && options.cancel_requested());
          },
      .take_steering_messages = options.take_steering_messages,
      .compact_context = options.access_token.empty()
                             ? decltype(ava::agent::AgentLoopOptions{}.compact_context){}
                             : [&](ava::session::SessionStore& store, std::string_view trigger,
                                   const std::vector<std::string>& replayed_user_messages) -> ava::core::Result<bool> {
        return compact_runtime_context(session, store, trigger, provider, transport, options, replayed_user_messages);
      },
      .session_mutex = options.session_mutex,
      .model_pricing = session.model.pricing});

  auto result = loop.run_turn(user_message, session.store, provider, transport);
  if (sink_error) return std::unexpected(std::move(*sink_error));
  if (!result) {
    auto event = base_event_locked(session, RuntimeEventType::Error, options.session_mutex);
    event.error_category = ava::core::to_string(result.error().category());
    event.error_message = result.error().message();
    event.error_details = result.error().format();
    static_cast<void>(emit_event(event_sink, event));
    return std::unexpected(result.error());
  }

  auto assistant_event = base_event_locked(session, RuntimeEventType::AssistantMessage, options.session_mutex);
  assistant_event.text = result->final_text;
  if (auto emitted = emit_event(event_sink, assistant_event); !emitted) {
    return std::unexpected(std::move(emitted.error()));
  }

  auto done_event = base_event_locked(session, RuntimeEventType::Done, options.session_mutex);
  done_event.stop_reason = result->stop_reason;
  done_event.provider_iterations = result->provider_iterations;
  done_event.tool_calls = result->tool_calls;
  if (auto emitted = emit_event(event_sink, done_event); !emitted) {
    return std::unexpected(std::move(emitted.error()));
  }

  return result;
}

}  // namespace ava::app
