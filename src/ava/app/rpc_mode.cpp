#include "ava/app/rpc_mode.h"

#include <algorithm>
#include <atomic>
#include <cctype>
#include <condition_variable>
#include <deque>
#include <functional>
#include <iomanip>
#include <istream>
#include <map>
#include <memory>
#include <mutex>
#include <ostream>
#include <sstream>
#include <string_view>
#include <thread>
#include <utility>

#include "ava/app/commands.h"
#include "ava/config/auth.h"
#include "ava/config/openai_oauth.h"
#include "ava/core/ids.h"
#include "ava/core/json.h"
#include "ava/provider/curl_transport.h"
#include "ava/provider/registry.h"
#include "ava/session/session_store.h"
#include "ava/session/stats.h"

namespace ava::app {
namespace {

constexpr std::size_t kMaxRpcLineBytes = 1024 * 1024;
constexpr std::size_t kMaxRpcMessagesResponseBytes = 1024 * 1024;
constexpr std::size_t kMaxRpcMessagesEntries = 1000;
constexpr std::size_t kMaxRpcQueuedMessages = 64;
constexpr std::size_t kMaxRpcQueuedMessageBytes = 64 * 1024;
constexpr std::size_t kMaxRpcQueueEventMessageBytes = 512;
constexpr std::size_t kMaxRpcIdentifierBytes = 256;
constexpr long long kRpcProtocolVersion = 1;

std::string_view trim(std::string_view value) {
  while (!value.empty() && std::isspace(static_cast<unsigned char>(value.front())) != 0) value.remove_prefix(1);
  while (!value.empty() && std::isspace(static_cast<unsigned char>(value.back())) != 0) value.remove_suffix(1);
  return value;
}

bool is_json_object_line(std::string_view line) {
  line = trim(line);
  if (line.size() < 2 || line.front() != '{') return false;
  bool in_string = false;
  bool escaped = false;
  int depth = 0;
  std::size_t object_end = std::string_view::npos;
  for (std::size_t index = 0; index < line.size(); ++index) {
    const char ch = line[index];
    if (escaped) {
      escaped = false;
      continue;
    }
    if (ch == '\\' && in_string) {
      escaped = true;
      continue;
    }
    if (ch == '"') {
      in_string = !in_string;
      continue;
    }
    if (in_string) continue;
    if (ch == '{') {
      ++depth;
    } else if (ch == '}') {
      --depth;
      if (depth == 0) {
        object_end = index;
        break;
      }
      if (depth < 0) return false;
    }
  }
  if (in_string || depth != 0 || object_end == std::string_view::npos) return false;
  return trim(line.substr(object_end + 1)).empty();
}

std::string string_field_json(std::string_view key, std::string_view value) {
  return "\"" + std::string(key) + "\":\"" + ava::core::json::escape(value) + "\"";
}

std::string bool_field_json(std::string_view key, bool value) {
  return "\"" + std::string(key) + "\":" + (value ? "true" : "false");
}

std::string number_field_json(std::string_view key, std::size_t value) {
  return "\"" + std::string(key) + "\":" + std::to_string(value);
}

std::string integer_field_json(std::string_view key, long long value) {
  return "\"" + std::string(key) + "\":" + std::to_string(value);
}

std::string decimal_field_json(std::string_view key, long double value) {
  std::ostringstream out;
  out << std::setprecision(12) << value;
  return "\"" + std::string(key) + "\":" + out.str();
}

std::string output_array_json(const std::vector<std::string>& output) {
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

std::string model_key(std::string_view provider_id, std::string_view model_id) {
  return std::string(provider_id) + "\n" + std::string(model_id);
}

bool has_model_key(const std::vector<std::string>& keys, std::string_view key) {
  for (const auto& existing : keys) {
    if (existing == key) return true;
  }
  return false;
}

std::vector<ava::config::ModelInfo> effective_models(const ava::config::ModelRegistry& registry) {
  std::vector<ava::config::ModelInfo> models;
  std::vector<std::string> seen;
  for (auto model = registry.models.rbegin(); model != registry.models.rend(); ++model) {
    const auto key = model_key(model->provider_id, model->model_id);
    if (has_model_key(seen, key)) continue;
    seen.push_back(key);
    models.push_back(*model);
  }
  std::reverse(models.begin(), models.end());
  return models;
}

void append_optional_bool(std::string& json, std::string_view key, const std::optional<bool>& value) {
  if (!value) return;
  json += ',';
  json += bool_field_json(key, *value);
}

void append_optional_integer(std::string& json, std::string_view key, const std::optional<long long>& value) {
  if (!value) return;
  json += ',';
  json += integer_field_json(key, *value);
}

std::string model_info_json(const ava::config::ModelInfo& model, const RuntimeSession& session, bool configured) {
  const bool registered = ava::provider::builtin_provider_registry().contains(model.provider_id);
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
  json += ",\"reasoning_levels\":";
  json += string_array_json(model.reasoning_levels);
  json += ",\"compatibility_quirks\":";
  json += string_array_json(model.compatibility_quirks);
  json += ',';
  json += bool_field_json("selected", session.model.provider_id == model.provider_id &&
                                          session.model.model_id == model.model_id);
  json += '}';
  return json;
}

ava::core::Error invalid_rpc(std::string message);

std::string joined_output(const std::vector<std::string>& output) {
  std::string text;
  for (std::size_t index = 0; index < output.size(); ++index) {
    if (index > 0) text += '\n';
    text += output[index];
  }
  return text;
}

std::string context_sources_json(const RuntimeSession& session) {
  std::string json = "[";
  for (std::size_t index = 0; index < session.context_sources.size(); ++index) {
    const auto& source = session.context_sources[index];
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

std::string state_result_json(const RuntimeSession& session, bool cancel_requested) {
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
  json += number_field_json("context_source_count", session.context_sources.size());
  json += ",\"context_sources\":";
  json += context_sources_json(session);
  json += '}';
  return json;
}

ava::core::VoidResult validate_protocol_version(const RpcCommand& command) {
  if (!command.protocol_version) return {};
  if (*command.protocol_version == kRpcProtocolVersion) return {};

  auto error = invalid_rpc("unsupported RPC protocol version");
  error.with_context("protocol_version", std::to_string(*command.protocol_version));
  error.with_context("supported_protocol_version", std::to_string(kRpcProtocolVersion));
  return std::unexpected(std::move(error));
}

std::string rpc_protocol_result_json() {
  return "{\"protocol_version\":" + std::to_string(kRpcProtocolVersion) + ",\"supported_protocol_versions\":[" +
         std::to_string(kRpcProtocolVersion) + "]}";
}

ava::core::Result<std::string> list_sessions_result_json(const RuntimeSession& session) {
  auto sessions = ava::session::SessionStore::list_sessions(session.workspace_dir, session.paths.sessions_dir);
  if (!sessions) return std::unexpected(sessions.error());
  std::string json = "{\"sessions\":[";
  for (std::size_t index = 0; index < sessions->size(); ++index) {
    const auto& summary = (*sessions)[index];
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

ava::core::Result<std::string> list_models_result_json(const RuntimeSession& session) {
  auto registry = ava::config::load_model_registry(session.paths);
  if (!registry) return std::unexpected(std::move(registry.error()));

  auto models = effective_models(*registry);
  bool current_in_catalog = false;
  for (const auto& model : models) {
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

std::string command_result_json(const CommandResult& result) {
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

std::string session_entry_json(const ava::session::SessionEntry& entry) {
  std::string json = "{";
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

std::string capped_session_entry_json(const ava::session::SessionEntry& entry) {
  auto json = session_entry_json(entry);
  if (json.size() <= 8192) return json;
  std::string capped = "{";
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

ava::core::Result<std::string> messages_result_json(const RuntimeSession& session) {
  auto entries = session.store.load();
  if (!entries) return std::unexpected(entries.error());

  std::string json = "{";
  json += string_field_json("session_id", session.store.session_id());
  json += ",\"messages\":[";
  bool first = true;
  bool truncated = false;
  std::size_t included = 0;
  for (const auto& entry : *entries) {
    if (ava::session::is_internal_replay_user_message(entry)) continue;
    if (entry.type != ava::session::EntryType::UserMessage && entry.type != ava::session::EntryType::AssistantMessage &&
        entry.type != ava::session::EntryType::ToolCall && entry.type != ava::session::EntryType::ToolResult) {
      continue;
    }
    if (included >= kMaxRpcMessagesEntries) {
      truncated = true;
      break;
    }
    auto entry_json = capped_session_entry_json(entry);
    const std::size_t projected_size = json.size() + entry_json.size() + 16;
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

ava::core::Result<std::string> session_stats_result_json(const RuntimeSession& session) {
  auto entries = session.store.load();
  if (!entries) return std::unexpected(entries.error());
  const auto stats = ava::session::compute_session_stats(*entries);

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
  json += number_field_json("compaction", stats.counts.compaction);
  json += ',';
  json += number_field_json("error", stats.counts.error);
  json += ',';
  json += number_field_json("cancel", stats.counts.cancel);
  json += "}}";
  return json;
}

ava::core::Result<RuntimeSession> create_new_session(const RuntimeSession& current,
                                                     const RuntimeOpenOptions& base_options) {
  RuntimeOpenOptions options = base_options;
  options.workspace_dir = current.workspace_dir;
  options.current_dir = current.current_dir;
  options.mode = current.mode;
  options.paths = current.paths;
  options.requested_session_id = std::nullopt;
  options.continue_last_session = false;
  return open_runtime_session(options);
}

struct RpcOutput {
  explicit RpcOutput(std::ostream& output) : out(output) {}

  std::ostream& out;
  std::mutex mutex;
  std::function<void()> on_write_failure;
};

ava::core::VoidResult write_record(RpcOutput& output, std::string_view record);

struct QueuedRpcMessage {
  std::string request_id;
  std::string correlation_id;
  std::string message;
};

struct RpcRunState {
  std::mutex mutex;
  std::atomic_bool cancel_requested = false;
  bool active_run = false;
  bool input_closed = false;
  std::string active_request_id;
  std::deque<QueuedRpcMessage> steering_messages;
  std::deque<QueuedRpcMessage> follow_up_messages;
  std::optional<ava::core::Error> async_error;
};

struct PendingPermissionRequest {
  bool resolved = false;
  std::string correlation_id;
  std::optional<ava::permissions::PermissionResolution> resolution;
  std::optional<ava::core::Error> error;
};

struct PendingQuestionRequest {
  bool resolved = false;
  std::string correlation_id;
  bool allow_custom = false;
  std::vector<ava::agent::QuestionOption> options;
  std::optional<ava::agent::QuestionAnswer> answer;
  std::optional<ava::core::Error> error;
};

struct PendingResolverState {
  std::mutex mutex;
  std::condition_variable cv;
  std::map<std::string, std::shared_ptr<PendingPermissionRequest>> permission_requests;
  std::map<std::string, std::shared_ptr<PendingQuestionRequest>> question_requests;
};

ava::core::Error canceled_error() { return ava::core::Error(ava::core::ErrorCategory::Unknown, "agent loop canceled"); }

ava::core::Error skipped_follow_up_error(std::string_view reason) {
  auto error = ava::core::Error(ava::core::ErrorCategory::Unknown, "queued follow_up skipped");
  error.with_context("reason", std::string(reason));
  return error;
}

ava::core::Error no_pending_request_error(std::string_view request_id) {
  auto error = invalid_rpc("RPC resolver reply has no matching pending request");
  error.with_context("request_id", std::string(request_id));
  return error;
}

bool cancel_requested(RpcRunState& state) {
  std::lock_guard lock(state.mutex);
  return state.cancel_requested.load(std::memory_order_relaxed);
}

bool active_run(RpcRunState& state) {
  std::lock_guard lock(state.mutex);
  return state.active_run;
}

bool input_closed(RpcRunState& state) {
  std::lock_guard lock(state.mutex);
  return state.input_closed;
}

void set_active_run(RpcRunState& state, bool active, std::string request_id = {}) {
  std::lock_guard lock(state.mutex);
  state.active_run = active;
  state.active_request_id = active ? std::move(request_id) : std::string{};
}

void set_active_request_id(RpcRunState& state, std::string request_id) {
  std::lock_guard lock(state.mutex);
  state.active_run = true;
  state.active_request_id = std::move(request_id);
}

ava::core::Error requires_active_prompt_error(std::string_view command_type) {
  auto error = invalid_rpc("RPC command requires an active prompt");
  error.with_context("type", std::string(command_type));
  return error;
}

ava::core::Error input_closed_error(std::string_view command_type) {
  auto error = invalid_rpc("RPC input is closed");
  error.with_context("type", std::string(command_type));
  return error;
}

ava::core::Error queue_limit_error(std::string_view command_type) {
  auto error = invalid_rpc("RPC queued message limit exceeded");
  error.with_context("type", std::string(command_type));
  error.with_context("max_entries", std::to_string(kMaxRpcQueuedMessages));
  error.with_context("max_message_bytes", std::to_string(kMaxRpcQueuedMessageBytes));
  return error;
}

std::size_t queued_message_bytes(const std::deque<QueuedRpcMessage>& queue) {
  std::size_t bytes = 0;
  for (const auto& queued : queue) bytes += queued.message.size();
  return bytes;
}

std::string_view utf8_prefix_within(std::string_view value, std::size_t max_bytes) {
  if (value.size() <= max_bytes) return value;

  std::size_t index = 0;
  std::size_t end = 0;
  while (index < value.size() && index < max_bytes) {
    const auto byte = static_cast<unsigned char>(value[index]);
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
      const auto continuation = static_cast<unsigned char>(value[index + offset]);
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

ava::core::Result<QueuedRpcMessage> queue_rpc_message(std::deque<QueuedRpcMessage>& queue, RpcRunState& state,
                                                      std::string command_type, std::string request_id,
                                                      std::string message) {
  std::lock_guard lock(state.mutex);
  if (state.input_closed) return std::unexpected(input_closed_error(command_type));
  if (state.cancel_requested.load(std::memory_order_relaxed)) return std::unexpected(canceled_error());
  if (!state.active_run || state.active_request_id.empty()) {
    return std::unexpected(requires_active_prompt_error(command_type));
  }
  if (queue.size() >= kMaxRpcQueuedMessages || message.size() > kMaxRpcQueuedMessageBytes ||
      queued_message_bytes(queue) + message.size() > kMaxRpcQueuedMessageBytes) {
    return std::unexpected(queue_limit_error(command_type));
  }
  QueuedRpcMessage queued{
      .request_id = std::move(request_id), .correlation_id = state.active_request_id, .message = std::move(message)};
  queue.push_back(queued);
  return queued;
}

std::vector<QueuedRpcMessage> take_queued_steering_messages(RpcRunState& state, std::string_view correlation_id) {
  std::lock_guard lock(state.mutex);
  std::vector<QueuedRpcMessage> queued;
  std::deque<QueuedRpcMessage> remaining;
  while (!state.steering_messages.empty()) {
    if (state.steering_messages.front().correlation_id == correlation_id) {
      queued.push_back(std::move(state.steering_messages.front()));
    } else {
      remaining.push_back(std::move(state.steering_messages.front()));
    }
    state.steering_messages.pop_front();
  }
  state.steering_messages = std::move(remaining);
  return queued;
}

std::optional<QueuedRpcMessage> take_next_follow_up_message(RpcRunState& state) {
  std::lock_guard lock(state.mutex);
  if (state.cancel_requested.load(std::memory_order_relaxed) || state.input_closed) return std::nullopt;
  if (state.follow_up_messages.empty()) return std::nullopt;
  auto queued = std::move(state.follow_up_messages.front());
  state.follow_up_messages.pop_front();
  return queued;
}

std::vector<QueuedRpcMessage> clear_queued_steering_messages(RpcRunState& state) {
  std::lock_guard lock(state.mutex);
  std::vector<QueuedRpcMessage> cleared;
  cleared.reserve(state.steering_messages.size());
  while (!state.steering_messages.empty()) {
    cleared.push_back(std::move(state.steering_messages.front()));
    state.steering_messages.pop_front();
  }
  return cleared;
}

struct ClearedRpcQueues {
  std::vector<QueuedRpcMessage> steering_messages;
  std::vector<QueuedRpcMessage> follow_up_messages;
};

ClearedRpcQueues deactivate_and_clear_queued_messages(RpcRunState& state) {
  std::lock_guard lock(state.mutex);
  state.active_run = false;
  state.active_request_id.clear();
  ClearedRpcQueues cleared;
  cleared.steering_messages.reserve(state.steering_messages.size());
  while (!state.steering_messages.empty()) {
    cleared.steering_messages.push_back(std::move(state.steering_messages.front()));
    state.steering_messages.pop_front();
  }
  cleared.follow_up_messages.reserve(state.follow_up_messages.size());
  while (!state.follow_up_messages.empty()) {
    cleared.follow_up_messages.push_back(std::move(state.follow_up_messages.front()));
    state.follow_up_messages.pop_front();
  }
  return cleared;
}

void record_async_error(RpcRunState& state, ava::core::Error error) {
  std::lock_guard lock(state.mutex);
  if (!state.async_error) state.async_error = std::move(error);
}

std::optional<ava::core::Error> take_async_error(RpcRunState& state) {
  std::lock_guard lock(state.mutex);
  auto error = std::move(state.async_error);
  state.async_error.reset();
  return error;
}

ava::core::Result<std::optional<long long>> exact_optional_integer_field(std::string_view object,
                                                                         std::string_view key) {
  const auto start = ava::core::json::field_value_start(object, key);
  if (!start) return std::optional<long long>{};
  std::size_t end = *start;
  if (end < object.size() && object[end] == '-') ++end;
  const auto digits_start = end;
  while (end < object.size() && std::isdigit(static_cast<unsigned char>(object[end])) != 0) ++end;
  if (end == digits_start) return std::unexpected(invalid_rpc("RPC protocol_version must be an integer"));
  const bool negative = object[*start] == '-';
  const auto unsigned_start = negative ? *start + 1 : *start;
  if (end - unsigned_start > 1 && object[unsigned_start] == '0') {
    return std::unexpected(invalid_rpc("RPC protocol_version must be an integer"));
  }
  while (end < object.size() && std::isspace(static_cast<unsigned char>(object[end])) != 0) ++end;
  if (end < object.size() && object[end] != ',' && object[end] != '}') {
    return std::unexpected(invalid_rpc("RPC protocol_version must be an integer"));
  }
  try {
    return std::optional<long long>{std::stoll(std::string(object.substr(*start, end - *start)))};
  } catch (...) {
    return std::unexpected(invalid_rpc("RPC protocol_version is out of range"));
  }
}

ava::core::VoidResult validate_rpc_identifier(std::string_view value, std::string_view field_name) {
  if (value.size() <= kMaxRpcIdentifierBytes) return {};
  auto error = invalid_rpc("RPC identifier is too long");
  error.with_context("field", std::string(field_name));
  error.with_context("max_bytes", std::to_string(kMaxRpcIdentifierBytes));
  return std::unexpected(std::move(error));
}

ava::core::VoidResult validate_optional_rpc_identifier(const std::optional<std::string>& value,
                                                       std::string_view field_name) {
  if (!value) return {};
  return validate_rpc_identifier(*value, field_name);
}

std::string parse_error_response_id(std::string_view line) {
  if (!is_json_object_line(line)) return "";
  auto id = ava::core::json::string_field(line, "id");
  if (!id || id->empty() || id->size() > kMaxRpcIdentifierBytes) return "";
  return *id;
}

std::string permission_request_payload_json(std::string_view resolver_request_id,
                                            const ava::permissions::PermissionPrompt& prompt) {
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
  json += '}';
  return json;
}

std::string question_options_json(const std::vector<ava::agent::QuestionOption>& options) {
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

std::string question_request_payload_json(std::string_view resolver_request_id,
                                          const ava::agent::QuestionPrompt& prompt) {
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
  json += '}';
  return json;
}

std::string session_id_snapshot(const RuntimeSession& session, std::mutex& session_mutex) {
  std::lock_guard lock(session_mutex);
  return session.store.session_id();
}

EventEnvelope resolver_event_envelope(std::string name, std::string request_id, std::string correlation_id,
                                      std::string session_id, std::string payload_json) {
  EventEnvelope envelope;
  envelope.schema_version = 1;
  envelope.event_id = ava::core::make_id("event");
  envelope.timestamp = ava::session::now_timestamp();
  envelope.session_id = std::move(session_id);
  envelope.request_id = std::move(request_id);
  envelope.correlation_id = std::move(correlation_id);
  envelope.name = std::move(name);
  envelope.payload_json = std::move(payload_json);
  return envelope;
}

std::string queued_message_payload_json(std::string_view message, std::string_view reason = {}) {
  const bool truncated = message.size() > kMaxRpcQueueEventMessageBytes;
  const auto event_message = truncated ? utf8_prefix_within(message, kMaxRpcQueueEventMessageBytes) : message;
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

ava::core::VoidResult write_queue_event(RpcOutput& output, const RuntimeSession& session, std::mutex& session_mutex,
                                        std::string_view name, const QueuedRpcMessage& queued,
                                        std::string_view reason = {}) {
  auto envelope = resolver_event_envelope(std::string(name), queued.request_id, queued.correlation_id,
                                          session_id_snapshot(session, session_mutex),
                                          queued_message_payload_json(queued.message, reason));
  return write_record(output, serialize_event_envelope_jsonl(envelope));
}

ava::core::VoidResult write_skipped_queue_events(RpcOutput& output, const RuntimeSession& session,
                                                 std::mutex& session_mutex, const ClearedRpcQueues& cleared,
                                                 std::string_view reason) {
  for (const auto& queued : cleared.steering_messages) {
    if (auto written = write_queue_event(output, session, session_mutex, "steer_skipped", queued, reason); !written) {
      return written;
    }
  }
  for (const auto& queued : cleared.follow_up_messages) {
    if (auto written = write_queue_event(output, session, session_mutex, "follow_up_skipped", queued, reason);
        !written) {
      return written;
    }
  }
  return {};
}

bool cancel_pending_resolvers(PendingResolverState& pending_state) {
  std::lock_guard lock(pending_state.mutex);
  const bool had_pending = !pending_state.permission_requests.empty() || !pending_state.question_requests.empty();
  for (auto& [request_id, request] : pending_state.permission_requests) {
    static_cast<void>(request_id);
    request->resolved = true;
    request->resolution = ava::permissions::PermissionResolution::Deny;
    request->error = canceled_error();
  }
  for (auto& [request_id, request] : pending_state.question_requests) {
    static_cast<void>(request_id);
    request->resolved = true;
    request->error = canceled_error();
  }
  pending_state.permission_requests.clear();
  pending_state.question_requests.clear();
  pending_state.cv.notify_all();
  return had_pending;
}

std::string next_resolver_request_id(std::string_view prefix) { return ava::core::make_id(prefix); }

ava::core::Error invalid_rpc(std::string message) {
  return ava::core::Error(ava::core::ErrorCategory::InvalidArgument, std::move(message));
}

ava::core::Result<bool> read_rpc_line_bounded(std::istream& in, std::string& line) {
  line.clear();
  bool oversized = false;
  while (true) {
    const auto next = in.get();
    if (next == std::istream::traits_type::eof()) {
      if (oversized) return std::unexpected(invalid_rpc("RPC request line is too large"));
      if (in.eof()) return !line.empty() || oversized;
      return std::unexpected(ava::core::Error(ava::core::ErrorCategory::Io, "failed to read RPC stdin"));
    }
    const char ch = static_cast<char>(next);
    if (ch == '\n') break;
    if (line.size() >= kMaxRpcLineBytes) {
      oversized = true;
      continue;
    }
    if (!oversized) line.push_back(ch);
  }
  if (oversized) return std::unexpected(invalid_rpc("RPC request line is too large"));
  return true;
}

ava::core::VoidResult write_record(RpcOutput& output, std::string_view record) {
  std::lock_guard lock(output.mutex);
  output.out << record;
  output.out.flush();
  if (!output.out) {
    if (output.on_write_failure) output.on_write_failure();
    return std::unexpected(ava::core::Error(ava::core::ErrorCategory::Io, "failed to write RPC JSONL record"));
  }
  return {};
}

void subscribe_event_envelope_writer(EventBus& bus, RpcOutput& output) {
  bus.subscribe([&output](const EventEnvelope& envelope) {
    return write_record(output, serialize_event_envelope_jsonl(envelope));
  });
}

EventEnvelopeContext rpc_event_context(std::string_view request_id) {
  const auto id = std::string(request_id);
  EventEnvelopeContext context;
  context.request_id = id;
  context.correlation_id = id;
  return context;
}

ava::core::VoidResult write_success(RpcOutput& output, std::string_view id, std::string_view result_json) {
  return write_record(output, serialize_rpc_success_jsonl(id, result_json));
}

ava::core::VoidResult write_error(RpcOutput& output, std::string_view id, const ava::core::Error& error) {
  return write_record(output, serialize_rpc_error_jsonl(id, error));
}

ava::core::Result<RuntimeRunOptions> ensure_prompt_runtime_options(const ava::config::XdgPaths& paths,
                                                                    std::string_view provider_id,
                                                                    RuntimeRunOptions options,
                                                                    ava::provider::Transport& auth_transport,
                                                                    std::string_view purpose) {
  if (!options.access_token.empty()) return options;

  auto credential = ava::config::provider_credential_for_request(paths, provider_id, auth_transport);
  if (!credential) return std::unexpected(credential.error());
  if (!*credential) {
    auto error = ava::core::Error(ava::core::ErrorCategory::InvalidArgument,
                                  "RPC " + std::string(purpose) + " requires auth for provider `" +
                                      std::string(provider_id) + "`");
    error.with_context("auth_file", paths.auth_file.string());
    return std::unexpected(std::move(error));
  }
  options.access_token = (*credential)->access_token;
  options.credential_type = (*credential)->credential_type;
  options.openai_oauth = (*credential)->provider_id == "openai" && (*credential)->credential_type == "oauth";
  options.openai_account_id = (*credential)->account_id;
  if (options.openai_oauth && options.openai_account_id.empty()) {
    options.openai_account_id = ava::config::openai_oauth_account_id_from_token((*credential)->access_token).value_or("");
  }
  return options;
}

ava::core::Result<RuntimeSession> open_requested_session(const RuntimeSession& current,
                                                         const RuntimeOpenOptions& base_options,
                                                         std::string_view requested_session_id) {
  RuntimeOpenOptions options = base_options;
  options.workspace_dir = current.workspace_dir;
  options.current_dir = current.current_dir;
  options.mode = current.mode;
  options.paths = current.paths;
  options.requested_session_id = std::string(requested_session_id);
  options.continue_last_session = false;
  return open_runtime_session(options);
}

ava::core::Result<ava::config::ModelInfo> resolve_requested_model(const RuntimeSession& session,
                                                                  const RpcCommand& command) {
  if (!command.model || command.model->empty()) return std::unexpected(invalid_rpc("set_model requires model"));
  if (command.provider && !command.provider->empty()) {
    return resolve_runtime_model(session.paths, *command.provider, *command.model);
  }

  auto current_provider_match = resolve_runtime_model(session.paths, session.model.provider_id, *command.model);
  if (current_provider_match) return current_provider_match;
  if (current_provider_match.error().category() != ava::core::ErrorCategory::NotFound) {
    return std::unexpected(std::move(current_provider_match.error()));
  }

  auto registry = ava::config::load_model_registry(session.paths);
  if (!registry) return std::unexpected(std::move(registry.error()));
  const auto providers = ava::provider::builtin_provider_registry();
  std::vector<ava::config::ModelInfo> matches;
  for (const auto& model : effective_models(*registry)) {
    if (model.model_id == *command.model && providers.contains(model.provider_id)) matches.push_back(model);
  }
  if (matches.empty()) {
    auto error = ava::core::Error(ava::core::ErrorCategory::NotFound, "model is not configured");
    error.with_context("model", *command.model);
    return std::unexpected(std::move(error));
  }
  if (matches.size() > 1) {
    auto error = invalid_rpc("model id is ambiguous; provider is required");
    error.with_context("model", *command.model);
    error.with_context("matches", std::to_string(matches.size()));
    return std::unexpected(std::move(error));
  }
  return matches.front();
}

ava::core::Result<ava::config::ModelInfo> next_runtime_model(const RuntimeSession& session) {
  auto registry = ava::config::load_model_registry(session.paths);
  if (!registry) return std::unexpected(std::move(registry.error()));
  const auto providers = ava::provider::builtin_provider_registry();
  std::vector<ava::config::ModelInfo> models;
  for (const auto& model : effective_models(*registry)) {
    if (providers.contains(model.provider_id)) models.push_back(model);
  }
  if (models.empty()) return std::unexpected(ava::core::Error(ava::core::ErrorCategory::NotFound,
                                                              "no registered provider models are configured"));

  std::size_t next_index = 0;
  for (std::size_t index = 0; index < models.size(); ++index) {
    if (models[index].provider_id == session.model.provider_id && models[index].model_id == session.model.model_id) {
      next_index = (index + 1) % models.size();
      break;
    }
  }
  return models[next_index];
}

struct ProviderHandle {
  const ava::provider::Provider* provider = nullptr;
  std::unique_ptr<ava::provider::Provider> owned;

  const ava::provider::Provider& get() const { return owned ? *owned : *provider; }
};

ava::core::Result<ProviderHandle> provider_for_session_model(const RuntimeSession& session,
                                                             std::string_view injected_provider_id,
                                                             const ava::provider::Provider& injected_provider) {
  if (session.model.provider_id == injected_provider_id) {
    return ProviderHandle{.provider = &injected_provider, .owned = nullptr};
  }
  auto registry = ava::provider::builtin_provider_registry();
  auto provider = registry.create(session.model.provider_id);
  if (!provider) return std::unexpected(std::move(provider.error()));
  return ProviderHandle{.provider = nullptr, .owned = std::move(*provider)};
}

ava::permissions::PermissionResolver make_rpc_permission_resolver(
    PendingResolverState& pending_state, RpcOutput& output, RpcRunState& run_state, const RuntimeSession& session,
    std::mutex& session_mutex, ava::permissions::PermissionResolver policy_resolver, std::string prompt_request_id) {
  return [&pending_state, &output, &run_state, &session, &session_mutex, policy_resolver = std::move(policy_resolver),
          prompt_request_id = std::move(prompt_request_id)](const ava::permissions::PermissionPrompt& prompt)
             -> ava::core::Result<ava::permissions::PermissionResolution> {
    if (cancel_requested(run_state)) return std::unexpected(canceled_error());
    if (input_closed(run_state)) return std::unexpected(canceled_error());

    if (policy_resolver) {
      auto policy_result = policy_resolver(prompt);
      if (!policy_result) return std::unexpected(std::move(policy_result.error()));
      if (*policy_result == ava::permissions::PermissionResolution::Allow) {
        return ava::permissions::PermissionResolution::Allow;
      }
    }

    auto pending = std::make_shared<PendingPermissionRequest>();
    pending->correlation_id = prompt_request_id;
    std::string request_id;
    {
      std::lock_guard lock(pending_state.mutex);
      request_id = next_resolver_request_id("permission");
      pending_state.permission_requests[request_id] = pending;
    }

    if (cancel_requested(run_state) || input_closed(run_state)) {
      std::lock_guard lock(pending_state.mutex);
      pending_state.permission_requests.erase(request_id);
      return std::unexpected(canceled_error());
    }

    auto envelope = resolver_event_envelope("permission_requested", prompt_request_id, prompt_request_id,
                                            session_id_snapshot(session, session_mutex),
                                            permission_request_payload_json(request_id, prompt));
    if (auto written = write_record(output, serialize_event_envelope_jsonl(envelope)); !written) {
      std::lock_guard lock(pending_state.mutex);
      pending_state.permission_requests.erase(request_id);
      return std::unexpected(std::move(written.error()));
    }

    std::unique_lock lock(pending_state.mutex);
    pending_state.cv.wait(lock, [&pending] { return pending->resolved; });
    if (pending->error) return std::unexpected(*pending->error);
    if (!pending->resolution) return std::unexpected(canceled_error());
    return *pending->resolution;
  };
}

ava::agent::QuestionResolver make_rpc_question_resolver(PendingResolverState& pending_state, RpcOutput& output,
                                                        RpcRunState& run_state, const RuntimeSession& session,
                                                        std::mutex& session_mutex, std::string prompt_request_id) {
  return
      [&pending_state, &output, &run_state, &session, &session_mutex, prompt_request_id = std::move(prompt_request_id)](
          const ava::agent::QuestionPrompt& prompt) -> ava::core::Result<ava::agent::QuestionAnswer> {
        if (cancel_requested(run_state)) return std::unexpected(canceled_error());
        if (input_closed(run_state)) return std::unexpected(canceled_error());
        if (prompt.multiple) {
          return std::unexpected(invalid_rpc("RPC question resolver does not support multiple selections yet"));
        }

        auto pending = std::make_shared<PendingQuestionRequest>();
        pending->correlation_id = prompt_request_id;
        pending->allow_custom = prompt.allow_custom;
        pending->options = prompt.options;
        std::string request_id;
        {
          std::lock_guard lock(pending_state.mutex);
          request_id = next_resolver_request_id("question");
          pending_state.question_requests[request_id] = pending;
        }

        if (cancel_requested(run_state) || input_closed(run_state)) {
          std::lock_guard lock(pending_state.mutex);
          pending_state.question_requests.erase(request_id);
          return std::unexpected(canceled_error());
        }

        auto envelope = resolver_event_envelope("question_requested", prompt_request_id, prompt_request_id,
                                                session_id_snapshot(session, session_mutex),
                                                question_request_payload_json(request_id, prompt));
        if (auto written = write_record(output, serialize_event_envelope_jsonl(envelope)); !written) {
          std::lock_guard lock(pending_state.mutex);
          pending_state.question_requests.erase(request_id);
          return std::unexpected(std::move(written.error()));
        }

        std::unique_lock lock(pending_state.mutex);
        pending_state.cv.wait(lock, [&pending] { return pending->resolved; });
        if (pending->error) return std::unexpected(*pending->error);
        if (!pending->answer) return std::unexpected(canceled_error());
        return *pending->answer;
      };
}

ava::core::VoidResult resolve_permission_reply(PendingResolverState& pending_state, std::string_view request_id,
                                               std::string_view correlation_id, std::string_view decision) {
  ava::permissions::PermissionResolution resolution = ava::permissions::PermissionResolution::Deny;
  if (decision == "allow") {
    resolution = ava::permissions::PermissionResolution::Allow;
  } else if (decision == "deny") {
    resolution = ava::permissions::PermissionResolution::Deny;
  } else {
    auto error = invalid_rpc("permission_reply decision must be allow or deny");
    error.with_context("decision", std::string(decision));
    return std::unexpected(std::move(error));
  }

  std::shared_ptr<PendingPermissionRequest> pending;
  {
    std::lock_guard lock(pending_state.mutex);
    auto found = pending_state.permission_requests.find(std::string(request_id));
    if (found == pending_state.permission_requests.end()) {
      return std::unexpected(no_pending_request_error(request_id));
    }
    pending = found->second;
    if (pending->correlation_id != correlation_id) {
      auto error = invalid_rpc("permission_reply correlation_id does not match pending request");
      error.with_context("request_id", std::string(request_id));
      return std::unexpected(std::move(error));
    }
    pending_state.permission_requests.erase(found);
    pending->resolved = true;
    pending->resolution = resolution;
  }
  pending_state.cv.notify_all();
  return {};
}

ava::core::VoidResult resolve_question_reply(PendingResolverState& pending_state, std::string_view request_id,
                                             std::string_view correlation_id, const std::optional<std::string>& answer,
                                             const std::optional<std::string>& selected) {
  if (answer && selected) return std::unexpected(invalid_rpc("question_reply requires answer or selected, not both"));

  std::shared_ptr<PendingQuestionRequest> pending;
  ava::agent::QuestionAnswer parsed;
  {
    std::lock_guard lock(pending_state.mutex);
    auto found = pending_state.question_requests.find(std::string(request_id));
    if (found == pending_state.question_requests.end()) {
      return std::unexpected(no_pending_request_error(request_id));
    }
    pending = found->second;
    if (pending->correlation_id != correlation_id) {
      auto error = invalid_rpc("question_reply correlation_id does not match pending request");
      error.with_context("request_id", std::string(request_id));
      return std::unexpected(std::move(error));
    }

    if (answer) {
      if (!pending->allow_custom) {
        return std::unexpected(invalid_rpc("question_reply answer is not allowed for this request"));
      }
      parsed.custom_text = *answer;
    } else if (selected) {
      bool valid_option = false;
      for (const auto& option : pending->options) valid_option = valid_option || option.value == *selected;
      if (!valid_option) {
        return std::unexpected(invalid_rpc("question_reply selected option is not valid for this request"));
      }
      parsed.selected_options.push_back(*selected);
    } else {
      return std::unexpected(invalid_rpc("question_reply requires answer or selected"));
    }

    pending_state.question_requests.erase(found);
    pending->resolved = true;
    pending->answer = std::move(parsed);
  }
  pending_state.cv.notify_all();
  return {};
}

ava::core::Error active_run_reject_error(std::string_view command_type) {
  auto error = invalid_rpc("RPC command is unavailable while a prompt is active");
  error.with_context("type", std::string(command_type));
  return error;
}

std::string prompt_result_json(const RuntimeSession& session, std::mutex& session_mutex,
                               const ava::agent::AgentLoopResult& result) {
  std::string json = "{";
  json += string_field_json("session_id", session_id_snapshot(session, session_mutex));
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

}  // namespace

ava::core::Result<RpcCommand> parse_rpc_command_line(std::string_view line) {
  if (!line.empty() && line.back() == '\r') line.remove_suffix(1);
  if (line.size() > kMaxRpcLineBytes) return std::unexpected(invalid_rpc("RPC request line is too large"));
  if (!is_json_object_line(line)) return std::unexpected(invalid_rpc("malformed RPC JSON object"));

  auto id = ava::core::json::string_field(line, "id");
  if (!id || id->empty()) return std::unexpected(invalid_rpc("RPC request requires a non-empty string id"));
  if (auto valid = validate_rpc_identifier(*id, "id"); !valid) return std::unexpected(std::move(valid.error()));
  auto type = ava::core::json::string_field(line, "type");
  if (!type || type->empty()) return std::unexpected(invalid_rpc("RPC request requires a non-empty string type"));
  auto protocol_version = exact_optional_integer_field(line, "protocol_version");
  if (!protocol_version) return std::unexpected(std::move(protocol_version.error()));
  auto request_id = ava::core::json::string_field(line, "request_id");
  if (auto valid = validate_optional_rpc_identifier(request_id, "request_id"); !valid) {
    return std::unexpected(std::move(valid.error()));
  }
  auto correlation_id = ava::core::json::string_field(line, "correlation_id");
  if (auto valid = validate_optional_rpc_identifier(correlation_id, "correlation_id"); !valid) {
    return std::unexpected(std::move(valid.error()));
  }
  auto provider = ava::core::json::string_field(line, "provider");
  if (auto valid = validate_optional_rpc_identifier(provider, "provider"); !valid) {
    return std::unexpected(std::move(valid.error()));
  }
  auto model = ava::core::json::string_field(line, "model");
  if (auto valid = validate_optional_rpc_identifier(model, "model"); !valid) {
    return std::unexpected(std::move(valid.error()));
  }

  return RpcCommand{.id = std::move(*id),
                    .type = std::move(*type),
                    .protocol_version = std::move(*protocol_version),
                    .message = ava::core::json::string_field(line, "message"),
                    .session_id = ava::core::json::string_field(line, "session_id"),
                    .provider = std::move(provider),
                    .model = std::move(model),
                    .instructions = ava::core::json::string_field(line, "instructions"),
                    .request_id = std::move(request_id),
                    .correlation_id = std::move(correlation_id),
                    .decision = ava::core::json::string_field(line, "decision"),
                    .answer = ava::core::json::string_field(line, "answer"),
                    .selected = ava::core::json::string_field(line, "selected")};
}

std::string serialize_rpc_success_jsonl(std::string_view id, std::string_view result_json) {
  std::string json = "{\"id\":\"" + ava::core::json::escape(id) + "\",\"type\":\"response\",\"success\":true";
  if (!result_json.empty()) {
    json += ",\"result\":";
    json += result_json;
  }
  json += "}\n";
  return json;
}

std::string serialize_rpc_error_jsonl(std::string_view id, const ava::core::Error& error) {
  std::string json =
      "{\"id\":\"" + ava::core::json::escape(id) + "\",\"type\":\"response\",\"success\":false,\"error\":{";
  json += string_field_json("category", ava::core::to_string(error.category()));
  json += ',';
  json += string_field_json("message", error.message());
  json += ',';
  json += string_field_json("details", error.format());
  json += "}}\n";
  return json;
}

ava::core::VoidResult run_rpc_loop(RuntimeSession& session, const RuntimeOpenOptions& open_options,
                                    const ava::provider::Provider& provider, ava::provider::Transport& transport,
                                    ava::provider::Transport& auth_transport, RuntimeRunOptions runtime_options,
                                    std::istream& in, std::ostream& out) {
  RpcOutput output(out);
  RpcRunState run_state;
  PendingResolverState pending_state;
  std::mutex session_mutex;
  std::optional<std::jthread> prompt_worker;
  if (!runtime_options.permission_resolver) {
    runtime_options.permission_resolver = build_headless_permission_resolver(HeadlessPermissionPolicyOptions{});
  }
  const std::string injected_provider_id = session.model.provider_id;

  auto reap_finished_prompt = [&] {
    if (prompt_worker && !active_run(run_state)) {
      prompt_worker.reset();
    }
  };

  auto write_state_response = [&](std::string_view id) -> ava::core::VoidResult {
    const bool canceled = cancel_requested(run_state);
    std::lock_guard lock(session_mutex);
    return write_success(output, id, state_result_json(session, canceled));
  };

  auto write_follow_up_errors = [&](const std::vector<QueuedRpcMessage>& follow_ups,
                                    std::string_view reason) -> ava::core::VoidResult {
    for (const auto& queued : follow_ups) {
      const auto error = reason == "canceled" ? canceled_error() : skipped_follow_up_error(reason);
      if (auto written = write_error(output, queued.request_id, error); !written) return written;
    }
    return {};
  };

  auto cancel_and_clear_queued = [&] {
    std::lock_guard lock(run_state.mutex);
    run_state.input_closed = true;
    run_state.cancel_requested.store(true, std::memory_order_relaxed);
    ClearedRpcQueues cleared;
    cleared.steering_messages.reserve(run_state.steering_messages.size());
    while (!run_state.steering_messages.empty()) {
      cleared.steering_messages.push_back(std::move(run_state.steering_messages.front()));
      run_state.steering_messages.pop_front();
    }
    cleared.follow_up_messages.reserve(run_state.follow_up_messages.size());
    while (!run_state.follow_up_messages.empty()) {
      cleared.follow_up_messages.push_back(std::move(run_state.follow_up_messages.front()));
      run_state.follow_up_messages.pop_front();
    }
    return cleared;
  };

  output.on_write_failure = [&] {
    static_cast<void>(cancel_and_clear_queued());
    cancel_pending_resolvers(pending_state);
  };

  std::string line;
  while (true) {
    auto read_line = read_rpc_line_bounded(in, line);
    if (!read_line) {
      if (read_line.error().category() == ava::core::ErrorCategory::InvalidArgument) {
        if (auto written = write_error(output, "", read_line.error()); !written) return written;
        continue;
      }
      return std::unexpected(std::move(read_line.error()));
    }
    if (!*read_line) break;
    reap_finished_prompt();
    if (auto async_error = take_async_error(run_state)) return std::unexpected(std::move(*async_error));
    auto command = parse_rpc_command_line(line);
    if (!command) {
      if (auto written = write_error(output, parse_error_response_id(line), command.error()); !written) return written;
      continue;
    }
    if (auto valid_version = validate_protocol_version(*command); !valid_version) {
      if (auto written = write_error(output, command->id, valid_version.error()); !written) return written;
      continue;
    }

    if (command->type == "get_protocol") {
      if (auto written = write_success(output, command->id, rpc_protocol_result_json()); !written) return written;
      continue;
    }

    if (command->type == "get_state") {
      if (auto written = write_state_response(command->id); !written) return written;
      continue;
    }

    if (command->type == "list_sessions") {
      std::lock_guard lock(session_mutex);
      auto sessions_json = list_sessions_result_json(session);
      if (!sessions_json) {
        if (auto written = write_error(output, command->id, sessions_json.error()); !written) return written;
        continue;
      }
      if (auto written = write_success(output, command->id, *sessions_json); !written) return written;
      continue;
    }

    if (command->type == "list_models") {
      std::lock_guard lock(session_mutex);
      auto models_json = list_models_result_json(session);
      if (!models_json) {
        if (auto written = write_error(output, command->id, models_json.error()); !written) return written;
        continue;
      }
      if (auto written = write_success(output, command->id, *models_json); !written) return written;
      continue;
    }

    if (command->type == "get_messages") {
      if (active_run(run_state)) {
        if (auto written = write_error(output, command->id, active_run_reject_error(command->type)); !written) {
          return written;
        }
        continue;
      }
      std::lock_guard lock(session_mutex);
      auto messages_json = messages_result_json(session);
      if (!messages_json) {
        if (auto written = write_error(output, command->id, messages_json.error()); !written) return written;
        continue;
      }
      if (auto written = write_success(output, command->id, *messages_json); !written) return written;
      continue;
    }

    if (command->type == "get_session_stats") {
      if (active_run(run_state)) {
        if (auto written = write_error(output, command->id, active_run_reject_error(command->type)); !written) {
          return written;
        }
        continue;
      }
      std::lock_guard lock(session_mutex);
      auto stats_json = session_stats_result_json(session);
      if (!stats_json) {
        if (auto written = write_error(output, command->id, stats_json.error()); !written) return written;
        continue;
      }
      if (auto written = write_success(output, command->id, *stats_json); !written) return written;
      continue;
    }

    if (command->type == "set_model" || command->type == "cycle_model") {
      if (active_run(run_state)) {
        if (auto written = write_error(output, command->id, active_run_reject_error(command->type)); !written) {
          return written;
        }
        continue;
      }
      std::lock_guard lock(session_mutex);
      ava::core::Result<ava::config::ModelInfo> selected =
          command->type == "set_model" ? resolve_requested_model(session, *command) : next_runtime_model(session);
      if (!selected) {
        if (auto written = write_error(output, command->id, selected.error()); !written) return written;
        continue;
      }
      auto switched = switch_runtime_model(session, std::move(*selected));
      if (!switched) {
        if (auto written = write_error(output, command->id, switched.error()); !written) return written;
        continue;
      }
      if (auto written = write_success(output, command->id, state_result_json(session, cancel_requested(run_state)));
          !written) {
        return written;
      }
      continue;
    }

    if (command->type == "new_session") {
      if (active_run(run_state)) {
        if (auto written = write_error(output, command->id, active_run_reject_error(command->type)); !written) {
          return written;
        }
        continue;
      }
      std::lock_guard lock(session_mutex);
      auto created = create_new_session(session, open_options);
      if (!created) {
        if (auto written = write_error(output, command->id, created.error()); !written) return written;
        continue;
      }
      session = std::move(*created);
      {
        std::lock_guard state_lock(run_state.mutex);
        run_state.cancel_requested.store(false, std::memory_order_relaxed);
      }
      if (auto written = write_success(output, command->id, state_result_json(session, false)); !written) {
        return written;
      }
      continue;
    }

    if (command->type == "open_session" || command->type == "switch_session") {
      if (!command->session_id || command->session_id->empty()) {
        if (auto written = write_error(output, command->id, invalid_rpc(command->type + " requires session_id"));
            !written) {
          return written;
        }
        continue;
      }
      if (active_run(run_state)) {
        if (auto written = write_error(output, command->id, active_run_reject_error(command->type)); !written) {
          return written;
        }
        continue;
      }
      std::lock_guard lock(session_mutex);
      auto opened = open_requested_session(session, open_options, *command->session_id);
      if (!opened) {
        if (auto written = write_error(output, command->id, opened.error()); !written) return written;
        continue;
      }
      session = std::move(*opened);
      {
        std::lock_guard state_lock(run_state.mutex);
        run_state.cancel_requested.store(false, std::memory_order_relaxed);
      }
      if (auto written = write_success(output, command->id, state_result_json(session, false)); !written) {
        return written;
      }
      continue;
    }

    if (command->type == "permission_reply") {
      if (!command->request_id || command->request_id->empty()) {
        if (auto written = write_error(output, command->id, invalid_rpc(command->type + " requires request_id"));
            !written) {
          return written;
        }
        continue;
      }
      if (!command->decision) {
        if (auto written = write_error(output, command->id, invalid_rpc("permission_reply requires decision"));
            !written) {
          return written;
        }
        continue;
      }
      if (!command->correlation_id || command->correlation_id->empty()) {
        if (auto written = write_error(output, command->id, invalid_rpc("permission_reply requires correlation_id"));
            !written) {
          return written;
        }
        continue;
      }
      auto resolved =
          resolve_permission_reply(pending_state, *command->request_id, *command->correlation_id, *command->decision);
      if (!resolved) {
        if (auto written = write_error(output, command->id, resolved.error()); !written) return written;
        continue;
      }
      if (auto written = write_success(output, command->id, "{}"); !written) return written;
      continue;
    }

    if (command->type == "question_reply") {
      if (!command->request_id || command->request_id->empty()) {
        if (auto written = write_error(output, command->id, invalid_rpc(command->type + " requires request_id"));
            !written) {
          return written;
        }
        continue;
      }
      if (!command->correlation_id || command->correlation_id->empty()) {
        if (auto written = write_error(output, command->id, invalid_rpc("question_reply requires correlation_id"));
            !written) {
          return written;
        }
        continue;
      }
      auto resolved = resolve_question_reply(pending_state, *command->request_id, *command->correlation_id,
                                             command->answer, command->selected);
      if (!resolved) {
        if (auto written = write_error(output, command->id, resolved.error()); !written) return written;
        continue;
      }
      if (auto written = write_success(output, command->id, "{}"); !written) return written;
      continue;
    }

    if (command->type == "steer") {
      if (!command->message) {
        if (auto written = write_error(output, command->id, invalid_rpc("steer requires message")); !written) {
          return written;
        }
        continue;
      }
      auto queued =
          queue_rpc_message(run_state.steering_messages, run_state, command->type, command->id, *command->message);
      if (!queued) {
        if (auto written = write_error(output, command->id, queued.error()); !written) return written;
        continue;
      }
      if (auto written = write_queue_event(output, session, session_mutex, "steer_queued", *queued); !written) {
        return written;
      }
      std::string json = "{";
      json += bool_field_json("queued", true);
      json += ',';
      json += string_field_json("correlation_id", queued->correlation_id);
      json += '}';
      if (auto written = write_success(output, command->id, json); !written) return written;
      continue;
    }

    if (command->type == "follow_up") {
      if (!command->message) {
        if (auto written = write_error(output, command->id, invalid_rpc("follow_up requires message")); !written) {
          return written;
        }
        continue;
      }
      auto queued =
          queue_rpc_message(run_state.follow_up_messages, run_state, command->type, command->id, *command->message);
      if (!queued) {
        if (auto written = write_error(output, command->id, queued.error()); !written) return written;
        continue;
      }
      if (auto written = write_queue_event(output, session, session_mutex, "follow_up_queued", *queued); !written) {
        return written;
      }
      continue;
    }

    if (command->type == "cancel") {
      bool was_active = false;
      ClearedRpcQueues cleared;
      {
        std::lock_guard lock(run_state.mutex);
        run_state.cancel_requested.store(true, std::memory_order_relaxed);
        was_active = run_state.active_run;
        cleared.steering_messages.reserve(run_state.steering_messages.size());
        while (!run_state.steering_messages.empty()) {
          cleared.steering_messages.push_back(std::move(run_state.steering_messages.front()));
          run_state.steering_messages.pop_front();
        }
        cleared.follow_up_messages.reserve(run_state.follow_up_messages.size());
        while (!run_state.follow_up_messages.empty()) {
          cleared.follow_up_messages.push_back(std::move(run_state.follow_up_messages.front()));
          run_state.follow_up_messages.pop_front();
        }
      }
      cancel_pending_resolvers(pending_state);
      if (auto written = write_skipped_queue_events(output, session, session_mutex, cleared, "canceled"); !written) {
        return written;
      }
      std::string json = "{";
      json += bool_field_json("cancel_requested", true);
      json += ',';
      json += bool_field_json("active_run", was_active);
      json += ',';
      json += number_field_json("cleared_steer", cleared.steering_messages.size());
      json += ',';
      json += number_field_json("cleared_follow_up", cleared.follow_up_messages.size());
      json += '}';
      if (auto written = write_success(output, command->id, json); !written) return written;
      if (auto written = write_follow_up_errors(cleared.follow_up_messages, "canceled"); !written) return written;
      continue;
    }

    if (command->type == "context" || command->type == "export" || command->type == "compact") {
      bool compact_active_run = false;
      {
        std::lock_guard lock(run_state.mutex);
        if (run_state.active_run) {
          if (auto written = write_error(output, command->id, active_run_reject_error(command->type)); !written) {
            return written;
          }
          continue;
        }
        if (command->type == "compact") {
          run_state.active_run = true;
          run_state.active_request_id = command->id;
          run_state.cancel_requested.store(false, std::memory_order_relaxed);
          compact_active_run = true;
        }
      }
      auto clear_compact_active_run = [&] {
        if (compact_active_run) {
          set_active_run(run_state, false);
          compact_active_run = false;
        }
      };
      auto write_command_error = [&](ava::core::Error error) -> ava::core::VoidResult {
        clear_compact_active_run();
        return write_error(output, command->id, std::move(error));
      };
      auto write_command_success = [&](std::string json) -> ava::core::VoidResult {
        clear_compact_active_run();
        return write_success(output, command->id, std::move(json));
      };
      std::string slash_command;
      if (command->type == "context") {
        slash_command = "/context";
      } else if (command->type == "export") {
        slash_command = "/export";
      } else {
        slash_command = "/compact";
        if (command->instructions) slash_command += " " + *command->instructions;
      }
      std::optional<RuntimeRunOptions> compact_runtime_options;
      std::optional<ProviderHandle> compact_provider;
      if (command->type == "compact") {
        ava::config::XdgPaths paths;
        std::string provider_id;
        {
          std::lock_guard lock(session_mutex);
          paths = session.paths;
          provider_id = session.model.provider_id;
          auto selected_provider = provider_for_session_model(session, injected_provider_id, provider);
          if (!selected_provider) {
            if (auto written = write_command_error(selected_provider.error()); !written) return written;
            continue;
          }
          compact_provider = std::move(*selected_provider);
        }
        auto ensured = ensure_prompt_runtime_options(paths, provider_id, runtime_options, auth_transport, "compact");
        if (!ensured) {
          if (auto written = write_command_error(ensured.error()); !written) return written;
          continue;
        }
        compact_runtime_options = std::move(*ensured);
      }
      EventBus event_bus;
      subscribe_event_envelope_writer(event_bus, output);
      std::unique_lock lock(session_mutex, std::defer_lock);
      if (command->type != "compact") lock.lock();
      auto summary_generator =
          command->type == "compact"
              ? CompactionSummaryGenerator([&](const std::vector<ava::session::SessionEntry>& entries,
                                               const ava::session::CompactionConfig& config,
                                               std::string_view instructions, std::size_t estimated_tokens) {
                  return generate_compaction_summary(session, entries, config, instructions, estimated_tokens,
                                                     compact_provider->get(), transport, *compact_runtime_options);
                })
              : CompactionSummaryGenerator{};
      auto result = run_command(session, CommandRequest{.command = std::move(slash_command),
                                                        .event_sink = make_runtime_event_bus_adapter(
                                                            event_bus, rpc_event_context(command->id)),
                                                         .permission_resolver = runtime_options.permission_resolver,
                                                         .compaction_summary_generator = std::move(summary_generator),
                                                         .session_mutex = &session_mutex,
                                                          .propagate_compaction_errors = command->type == "compact"});
      if (!result) {
        if (auto written = write_command_error(result.error()); !written) return written;
        continue;
      }
      if (auto written = write_command_success(command_result_json(*result)); !written) return written;
      continue;
    }

    if (command->type == "prompt") {
      if (!command->message) {
        if (auto written = write_error(output, command->id, invalid_rpc("prompt requires message")); !written) {
          return written;
        }
        continue;
      }

      if (active_run(run_state)) {
        if (auto written = write_error(output, command->id, active_run_reject_error(command->type)); !written) {
          return written;
        }
        continue;
      }

      reap_finished_prompt();
      ava::config::XdgPaths paths;
      {
        std::lock_guard lock(session_mutex);
        paths = session.paths;
      }
      set_active_run(run_state, true, command->id);
      auto prompt_base_options = runtime_options;
      const auto prompt_id = command->id;
      const auto prompt_message = *command->message;
      prompt_worker.emplace([&, prompt_id, prompt_message, prompt_base_options = std::move(prompt_base_options),
                             paths = std::move(paths)](std::stop_token stop_token) mutable {
        auto finish_with_queue_cleanup = [&](std::string_view reason) {
          auto cleared = deactivate_and_clear_queued_messages(run_state);
          if (auto written = write_skipped_queue_events(output, session, session_mutex, cleared, reason); !written) {
            record_async_error(run_state, std::move(written.error()));
          }
          if (auto written = write_follow_up_errors(cleared.follow_up_messages, reason); !written) {
            record_async_error(run_state, std::move(written.error()));
          }
        };

        std::string prompt_provider_id;
        {
          std::lock_guard lock(session_mutex);
          prompt_provider_id = session.model.provider_id;
        }
        auto prompt_options = ensure_prompt_runtime_options(paths, prompt_provider_id, std::move(prompt_base_options),
                                                           auth_transport, "prompt");
        if (!prompt_options) {
          if (auto written = write_error(output, prompt_id, prompt_options.error()); !written) {
            record_async_error(run_state, std::move(written.error()));
          }
          finish_with_queue_cleanup("prompt_start_failed");
          return;
        }
        auto policy_permission_resolver = prompt_options->permission_resolver;
        auto run_one_prompt = [&](const std::string& request_id, const std::string& message) -> ava::core::VoidResult {
          set_active_request_id(run_state, request_id);
          prompt_options->cancel_requested = [&run_state, stop_token] {
            return stop_token.stop_requested() || cancel_requested(run_state);
          };
          prompt_options->session_mutex = &session_mutex;
          prompt_options->permission_resolver = make_rpc_permission_resolver(
              pending_state, output, run_state, session, session_mutex, policy_permission_resolver, request_id);
          prompt_options->question_resolver =
              make_rpc_question_resolver(pending_state, output, run_state, session, session_mutex, request_id);
          prompt_options->take_steering_messages = [&, request_id]() -> ava::core::Result<std::vector<std::string>> {
            auto queued = take_queued_steering_messages(run_state, request_id);
            std::vector<std::string> messages;
            messages.reserve(queued.size());
            for (const auto& item : queued) {
              if (auto written = write_queue_event(output, session, session_mutex, "steer_applied", item); !written) {
                return std::unexpected(std::move(written.error()));
              }
              messages.push_back(item.message);
            }
            return messages;
          };

          EventBus event_bus;
          subscribe_event_envelope_writer(event_bus, output);
          prompt_options->event_sink = make_runtime_event_bus_adapter(event_bus, rpc_event_context(request_id));

          ProviderHandle prompt_provider;
          {
            std::lock_guard lock(session_mutex);
            auto selected_provider = provider_for_session_model(session, injected_provider_id, provider);
            if (!selected_provider) return std::unexpected(std::move(selected_provider.error()));
            prompt_provider = std::move(*selected_provider);
          }

          auto result = run_prompt(session, message, prompt_provider.get(), transport, *prompt_options);
          ClearedRpcQueues skipped_steering;
          skipped_steering.steering_messages = clear_queued_steering_messages(run_state);
          if (auto written = write_skipped_queue_events(output, session, session_mutex, skipped_steering,
                                                        "run_completed_before_safe_point");
              !written) {
            return std::unexpected(std::move(written.error()));
          }
          if (!result) {
            if (auto written = write_error(output, request_id, result.error()); !written) {
              return std::unexpected(std::move(written.error()));
            }
            return {};
          }
          if (auto written = write_success(output, request_id, prompt_result_json(session, session_mutex, *result));
              !written) {
            return std::unexpected(std::move(written.error()));
          }
          return {};
        };

        auto prompt_run = run_one_prompt(prompt_id, prompt_message);
        if (!prompt_run) {
          record_async_error(run_state, std::move(prompt_run.error()));
          finish_with_queue_cleanup("prompt_start_failed");
          return;
        }

        while (!cancel_requested(run_state)) {
          auto follow_up = take_next_follow_up_message(run_state);
          if (!follow_up) break;
          set_active_request_id(run_state, follow_up->request_id);
          auto started_event = *follow_up;
          started_event.correlation_id = follow_up->request_id;
          if (auto written = write_queue_event(output, session, session_mutex, "follow_up_started", started_event);
              !written) {
            record_async_error(run_state, std::move(written.error()));
            finish_with_queue_cleanup("prompt_start_failed");
            return;
          }
          auto follow_up_run = run_one_prompt(follow_up->request_id, follow_up->message);
          if (!follow_up_run) {
            record_async_error(run_state, std::move(follow_up_run.error()));
            finish_with_queue_cleanup("prompt_start_failed");
            return;
          }
        }

        const bool canceled = cancel_requested(run_state);
        auto cleared = deactivate_and_clear_queued_messages(run_state);
        if (auto written = write_skipped_queue_events(output, session, session_mutex, cleared,
                                                      canceled ? "canceled" : "run_completed_before_safe_point");
            !written) {
          record_async_error(run_state, std::move(written.error()));
        }
        if (auto written = write_follow_up_errors(cleared.follow_up_messages,
                                                  canceled ? "canceled" : "run_completed_before_safe_point");
            !written) {
          record_async_error(run_state, std::move(written.error()));
        }
      });
      continue;
    }

    auto error = invalid_rpc("unknown RPC command type");
    error.with_context("type", command->type);
    if (auto written = write_error(output, command->id, error); !written) return written;
  }

  if (prompt_worker) {
    auto cleared = cancel_and_clear_queued();
    cancel_pending_resolvers(pending_state);
    if (auto written = write_skipped_queue_events(output, session, session_mutex, cleared, "canceled"); !written) {
      return written;
    }
    if (auto written = write_follow_up_errors(cleared.follow_up_messages, "canceled"); !written) return written;
    prompt_worker.reset();
  }
  if (auto async_error = take_async_error(run_state)) return std::unexpected(std::move(*async_error));

  return {};
}

ava::core::VoidResult run_rpc_loop(RuntimeSession& session, const RuntimeOpenOptions& open_options,
                                   const ava::provider::Provider& provider, ava::provider::Transport& transport,
                                   RuntimeRunOptions runtime_options, std::istream& in, std::ostream& out) {
  return run_rpc_loop(session, open_options, provider, transport, transport, std::move(runtime_options), in, out);
}

int run_rpc_mode(const RpcModeOptions& options, std::istream& in, std::ostream& out, std::ostream& err) {
  auto session = open_runtime_session(options.open_options);
  if (!session) {
    err << session.error().format() << '\n';
    return 1;
  }

  RuntimeRunOptions runtime_options;
  runtime_options.permission_resolver = build_headless_permission_resolver(options.permission_policy);
  runtime_options.question_resolver = nullptr;

  auto registry = ava::provider::builtin_provider_registry();
  auto provider = registry.create(session->model.provider_id);
  if (!provider) {
    err << provider.error().format() << '\n';
    return 1;
  }
  ava::provider::CurlCliTransport transport;
  ava::provider::RetryTransport retry_transport(transport);
  auto result = run_rpc_loop(*session, options.open_options, **provider, retry_transport, transport,
                             std::move(runtime_options), in, out);
  if (!result) {
    err << result.error().format() << '\n';
    return 1;
  }
  return 0;
}

}  // namespace ava::app
