#include "ava/app/runtime.h"

#include <algorithm>
#include <cctype>
#include <iterator>
#include <utility>

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

std::string capped_entry_data(std::string_view data) {
  if (data.size() <= kMaxCompactionPromptEntryBytes) return std::string(data);
  return std::string(data.substr(0, kMaxCompactionPromptEntryBytes)) + "\n[entry data truncated from " +
         std::to_string(data.size()) + " bytes]";
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
    auto error = ava::core::Error(ava::core::ErrorCategory::Provider,
                                  "compaction summary request failed with status " +
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
  return std::unexpected(ava::core::Error(ava::core::ErrorCategory::Provider,
                                          "compaction summary response text is missing"));
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

std::string session_start_data_json(ava::agent::Mode mode, const ava::config::ModelInfo& model,
                                     const ava::config::PromptSelection& prompt, std::size_t context_source_count) {
  return "{\"mode\":\"" + ava::agent::to_string(mode) + "\",\"provider\":\"" +
         ava::core::json::escape(model.provider_id) + "\",\"model\":\"" + ava::core::json::escape(model.model_id) +
         "\",\"prompt_override\":" + (prompt.from_override ? std::string("true") : std::string("false")) +
         ",\"context_sources\":" + std::to_string(context_source_count) + '}';
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

std::string model_change_data_json(const ava::config::ModelInfo& previous, const ava::config::ModelInfo& current) {
  std::string json = "{";
  json += "\"previous_provider\":\"" + ava::core::json::escape(previous.provider_id) + "\"";
  json += ",\"previous_model\":\"" + ava::core::json::escape(previous.model_id) + "\"";
  json += ",\"provider\":\"" + ava::core::json::escape(current.provider_id) + "\"";
  json += ",\"model\":\"" + ava::core::json::escape(current.model_id) + "\"";
  json += ",\"display_name\":\"" + ava::core::json::escape(current.display_name) + "\"";
  json += ",\"family\":\"" + ava::core::json::escape(current.family) + "\"";
  json += ",\"api_family\":\"" + ava::core::json::escape(current.api_family) + "\"";
  json += optional_bool_json("supports_tools", current.supports_tools);
  json += optional_bool_json("supports_streaming", current.supports_streaming);
  json += optional_bool_json("supports_reasoning", current.supports_reasoning);
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

std::optional<bool> bool_json_field(std::string_view object, std::string_view key) {
  auto start = ava::core::json::field_value_start(object, key);
  if (!start) return std::nullopt;
  auto value = trim(object.substr(*start));
  if (value.starts_with("true") && (value.size() == 4 || value[4] == ',' || value[4] == '}')) return true;
  if (value.starts_with("false") && (value.size() == 5 || value[5] == ',' || value[5] == '}')) return false;
  return std::nullopt;
}

ava::config::ModelInfo fallback_persisted_model(std::string provider_id, std::string model_id,
                                                std::optional<bool> supports_tools,
                                                std::optional<bool> supports_streaming,
                                                std::optional<bool> supports_reasoning) {
  const auto display_name = model_id;
  const auto family = model_id;
  return ava::config::ModelInfo{.provider_id = std::move(provider_id),
                                .model_id = std::move(model_id),
                                .display_name = display_name,
                                .family = family,
                                .context_window_tokens = std::nullopt,
                                .max_output_tokens = std::nullopt,
                                .pricing = std::nullopt,
                                .api_family = {},
                                .input_modalities = {},
                                .supports_tools = supports_tools.value_or(false),
                                .supports_streaming = supports_streaming.value_or(false),
                                .supports_reasoning = supports_reasoning.value_or(false),
                                .reports_usage = std::nullopt,
                                .reasoning_levels = {},
                                .compatibility_quirks = {"persisted_unknown_model"}};
}

std::optional<ava::config::ModelInfo> latest_persisted_model(
    const ava::config::ModelRegistry& registry, const std::vector<ava::session::SessionEntry>& entries) {
  std::optional<std::string> provider_id;
  std::optional<std::string> model_id;
  std::optional<bool> supports_tools;
  std::optional<bool> supports_streaming;
  std::optional<bool> supports_reasoning;
  for (const auto& entry : entries) {
    if (entry.type != ava::session::EntryType::SessionStart && entry.type != ava::session::EntryType::ModelChange) {
      continue;
    }
    auto provider = ava::core::json::string_field(entry.data_json, "provider");
    auto model = ava::core::json::string_field(entry.data_json, "model");
    if (!provider || !model || provider->empty() || model->empty()) continue;
    provider_id = std::move(*provider);
    model_id = std::move(*model);
    supports_tools = bool_json_field(entry.data_json, "supports_tools");
    supports_streaming = bool_json_field(entry.data_json, "supports_streaming");
    supports_reasoning = bool_json_field(entry.data_json, "supports_reasoning");
  }
  if (!provider_id || !model_id) return std::nullopt;
  if (auto model = ava::config::find_model(registry, *provider_id, *model_id)) return model;
  return fallback_persisted_model(std::move(*provider_id), std::move(*model_id), supports_tools, supports_streaming,
                                  supports_reasoning);
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

  if (!created) {
    auto entries = store->load();
    if (!entries) return std::unexpected(std::move(entries.error()));
    if (auto persisted_model = latest_persisted_model(*registry, *entries)) model = std::move(*persisted_model);
  }

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

  auto prompt_state = load_runtime_prompt_state(session.paths, model, session.mode, session.workspace_dir,
                                               session.current_dir);
  if (!prompt_state) return std::unexpected(std::move(prompt_state.error()));

  const auto previous = session.model;
  auto appended = append_model_change(session.store, previous, model);
  if (!appended) return std::unexpected(std::move(appended.error()));

  session.model = std::move(model);
  session.mode = prompt_state->mode;
  session.prompt = std::move(prompt_state->prompt);
  session.context_sources = std::move(prompt_state->context_sources);
  session.system_prompt = std::move(prompt_state->system_prompt);
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
    prompt += capped_entry_data(entry.data_json);
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
      .credential_type = options.openai_oauth && options.credential_type == "bearer" ? "oauth" : options.credential_type,
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
  auto session_event = base_event_locked(session, RuntimeEventType::SessionStart, options.session_mutex);
  if (auto emitted = emit_event(options.event_sink, session_event); !emitted) {
    return std::unexpected(std::move(emitted.error()));
  }

  auto user_event = base_event_locked(session, RuntimeEventType::UserMessage, options.session_mutex);
  user_event.text = user_message;
  if (auto emitted = emit_event(options.event_sink, user_event); !emitted) {
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
      .credential_type = options.openai_oauth && options.credential_type == "bearer" ? "oauth" : options.credential_type,
      .openai_oauth = options.openai_oauth,
      .openai_account_id = options.openai_account_id,
      .stream = options.stream,
      .model_supports_tools = session.model.supports_tools.value_or(true),
      .model_supports_streaming = session.model.supports_streaming.value_or(true),
      .model_supports_reasoning = session.model.supports_reasoning.value_or(false),
      .model_max_output_tokens = session.model.max_output_tokens,
      .on_tool_event =
          [&session, &options, &sink_error](const ava::agent::ToolTimelineEntry& entry) {
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
            if (auto emitted = emit_event(options.event_sink, event); !emitted) {
              sink_error = std::move(emitted.error());
            }
          },
      .on_tool_progress = [&session, &options,
                           &sink_error](const ava::agent::ToolProgressEntry& entry) -> ava::core::VoidResult {
        if (sink_error) return std::unexpected(*sink_error);
        auto event = base_event_locked(session, RuntimeEventType::ToolProgress, options.session_mutex);
        event.call_id = entry.call_id;
        event.tool_name = entry.name;
        event.text = entry.text;
        event.status = entry.status;
        if (auto emitted = emit_event(options.event_sink, event); !emitted) {
          sink_error = std::move(emitted.error());
          return std::unexpected(*sink_error);
        }
        return {};
      },
      .on_stream_event = [&session, &options,
                          &sink_error](const ava::provider::StreamEvent& stream_event) -> ava::core::VoidResult {
        if (sink_error) return std::unexpected(*sink_error);
        auto event = base_event_locked(
            session,
            stream_event.type == ava::provider::StreamEventType::TextDelta ? RuntimeEventType::MessageUpdate
            : stream_event.type == ava::provider::StreamEventType::Done    ? RuntimeEventType::MessageEnd
                                                                           : RuntimeEventType::ProviderEvent,
            options.session_mutex);
        event.text = stream_event.text;
        event.call_id = stream_event.tool_call_id;
        event.tool_name = stream_event.tool_name;
        event.status = ava::provider::to_string(stream_event.type);
        event.error_message = stream_event.error_message;
        if (auto emitted = emit_event(options.event_sink, event); !emitted) {
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
    static_cast<void>(emit_event(options.event_sink, event));
    return std::unexpected(result.error());
  }

  auto assistant_event = base_event_locked(session, RuntimeEventType::AssistantMessage, options.session_mutex);
  assistant_event.text = result->final_text;
  if (auto emitted = emit_event(options.event_sink, assistant_event); !emitted) {
    return std::unexpected(std::move(emitted.error()));
  }

  auto done_event = base_event_locked(session, RuntimeEventType::Done, options.session_mutex);
  done_event.stop_reason = result->stop_reason;
  done_event.provider_iterations = result->provider_iterations;
  done_event.tool_calls = result->tool_calls;
  if (auto emitted = emit_event(options.event_sink, done_event); !emitted) {
    return std::unexpected(std::move(emitted.error()));
  }

  return result;
}

}  // namespace ava::app
