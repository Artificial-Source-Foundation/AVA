#include "sys.h"
#include "ava/app/runtime_compaction.h"

#include "ava/app/runtime_json.h"
#include "ava/app/runtime_retry.h"

#include "ava/agent/message_builder.h"

#include "ava/core/json.h"

#include "ava/session/validation.h"

#include <algorithm>
#include <optional>
#include <utility>

namespace ava::app {
namespace {

constexpr std::size_t kMaxCompactionPromptEntryBytes = 8192;

runtime::RuntimeEvent base_compaction_event_locked(runtime::RuntimeSession const& session, runtime::RuntimeRunOptions const& options,
                                          runtime::RuntimeEventType type)
{
  auto build = [&] {
    runtime::RuntimeEvent event;
    event.type = type;
    event.timestamp = ava::session::now_timestamp();
    event.session_id = session.store.session_id();
    event.mode = session.mode;
    event.provider_id = session.model.provider_id;
    event.model_id = session.model.model_id;
    return event;
  };
  if (!options.session_mutex) return build();
  std::lock_guard lock(*options.session_mutex);
  return build();
}

ava::core::VoidResult emit_compaction_event(runtime::RuntimeSession const& session, runtime::RuntimeRunOptions const& options,
                                            runtime::RuntimeEvent event)
{
  if (!options.event_sink) return {};
  if (event.timestamp.empty()) {
    event.timestamp = ava::session::now_timestamp();
  }
  if (event.session_id.empty()) {
    if (options.session_mutex) {
      std::lock_guard lock(*options.session_mutex);
      event.session_id = session.store.session_id();
    } else {
      event.session_id = session.store.session_id();
    }
  }
  return emit_event(options.event_sink, event);
}

ava::core::Error agent_loop_canceled_error()
{
  return ava::core::Error(ava::core::ErrorCategory::Unknown, "agent loop canceled");
}

std::string capped_entry_data(std::string_view data)
{
  if (data.size() <= kMaxCompactionPromptEntryBytes) return std::string(data);
  return std::string(data.substr(0, kMaxCompactionPromptEntryBytes)) + "\n[entry data truncated from " +
         std::to_string(data.size()) + " bytes]";
}

std::string sanitized_reasoning_data_for_compaction(ava::session::SessionEntry const& entry)
{
  std::string data = "{";
  bool first = true;
  auto append_string = [&](std::string_view key, std::optional<std::string> const& value) {
    if (!value || value->empty()) return;
    if (!first) data += ',';
    first = false;
    data += runtime::json_string_field(key, *value);
  };
  auto append_bool = [&](std::string_view key, bool value) {
    if (!first) data += ',';
    first = false;
    data += runtime::json_bool_field(key, value);
  };

  bool const redacted = runtime::bool_json_field(entry.data_json, "redacted").value_or(false);
  append_string("provider", ava::core::json::string_field(entry.data_json, "provider"));
  append_string("model", ava::core::json::string_field(entry.data_json, "model"));
  append_string("format", ava::core::json::string_field(entry.data_json, "format"));
  if (!redacted) append_string("text", ava::core::json::string_field(entry.data_json, "text"));
  append_bool("redacted", redacted);
  append_bool("signature_present", ava::core::json::string_field(entry.data_json, "signature").has_value());
  data += '}';
  return capped_entry_data(data);
}

std::string compaction_entry_data(ava::session::SessionEntry const& entry)
{
  if (entry.type == ava::session::EntryType::ReasoningBlock) return sanitized_reasoning_data_for_compaction(entry);
  if (entry.type == ava::session::EntryType::UserMessage || entry.type == ava::session::EntryType::AssistantMessage) {
    return capped_entry_data(
        ava::session::sanitized_message_data_json(entry.data_json, entry.type == ava::session::EntryType::UserMessage));
  }
  return capped_entry_data(entry.data_json);
}

bool is_utf8_continuation_byte(char value)
{
  return (static_cast<unsigned char>(value) & 0xC0U) == 0x80U;
}

std::size_t utf8_suffix_start(std::string_view text, std::size_t suffix_bytes)
{
  if (suffix_bytes >= text.size()) return 0;
  auto start = text.size() - suffix_bytes;
  while (start < text.size() && is_utf8_continuation_byte(text[start])) ++start;
  return start;
}

std::string truncate_recent_context_to_token_budget(std::string tail, std::size_t keep_recent_tokens)
{
  if (keep_recent_tokens == 0 || tail.empty()) return {};
  if (ava::session::estimate_tokens(tail) <= keep_recent_tokens) return tail;

  std::string const marker =
      "[AVA: recent context tail truncated to keep_recent_tokens=" + std::to_string(keep_recent_tokens) + "]\n";
  auto const max_bytes = keep_recent_tokens * 4;
  if (max_bytes <= marker.size()) return marker;
  auto const suffix_bytes = max_bytes - marker.size();
  if (tail.size() > suffix_bytes) {
    tail = tail.substr(utf8_suffix_start(tail, suffix_bytes));
  }
  return marker + tail;
}

void erase_replayed_active_user_messages(std::vector<ava::provider::ChatMessage>& messages,
                                         std::vector<std::string> const& replayed_user_messages)
{
  for (auto replay = replayed_user_messages.rbegin(); replay != replayed_user_messages.rend(); ++replay) {
    auto const match = std::ranges::find_if(messages.rbegin(), messages.rend(), [&](auto const& message) {
      return message.role == "user" && message.content == *replay;
    });
    if (match != messages.rend()) messages.erase(std::next(match).base());
  }
}

ava::core::Result<std::string> build_recent_context_tail(std::vector<ava::session::SessionEntry> const& entries,
                                                         std::size_t keep_recent_messages,
                                                         std::size_t keep_recent_tokens,
                                                         std::vector<std::string> const& replayed_user_messages)
{
  if (keep_recent_messages == 0 || keep_recent_tokens == 0) return std::string{};
  auto messages = ava::agent::build_provider_messages_from_entries(entries);
  if (!messages) return std::unexpected(std::move(messages.error()));
  if (!replayed_user_messages.empty()) {
    erase_replayed_active_user_messages(*messages, replayed_user_messages);
  }
  auto const count = std::min(keep_recent_messages, messages->size());
  auto const start = messages->size() - count;
  std::string tail;
  for (std::size_t index = start; index < messages->size(); ++index) {
    if (!tail.empty()) tail += "\n\n";
    tail += messages->at(index).role;
    tail += ":\n";
    tail += messages->at(index).content;
  }
  return truncate_recent_context_to_token_budget(std::move(tail), keep_recent_tokens);
}

ava::core::Result<std::string> parse_compaction_response_text(ava::provider::Provider const& provider,
                                                              ava::provider::HttpResponse const& response, bool stream)
{
  auto events = provider.parse_response(response, stream);
  if (!events) {
    auto error = ava::core::Error(ava::core::ErrorCategory::Provider, "compaction summary request failed with status " +
                                                                          std::to_string(response.status_code));
    error.with_context("status", std::to_string(response.status_code));
    error.with_context("provider_error", events.error().format());
    return std::unexpected(std::move(error));
  }
  std::string streamed_text;
  for (auto const& event : *events) {
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

}  // namespace

bool same_session_snapshot(std::vector<ava::session::SessionEntry> const& expected,
                           std::vector<ava::session::SessionEntry> const& actual)
{
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
                                                 std::size_t current_entries)
{
  auto error =
      ava::core::Error(ava::core::ErrorCategory::Session, "session changed during context compaction after retry");
  error.with_context("trigger", std::string(trigger));
  error.with_context("snapshot_entries", std::to_string(snapshot_entries));
  error.with_context("current_entries", std::to_string(current_entries));
  return error;
}

std::string build_compaction_summary_prompt(std::vector<ava::session::SessionEntry> const& entries,
                                            ava::session::CompactionConfig const& config, std::string_view instructions,
                                            std::size_t estimated_tokens)
{
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
    auto const& entry = entries[index];
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
    runtime::RuntimeSession const& session, std::vector<ava::session::SessionEntry> const& entries,
    ava::session::CompactionConfig const& config, std::string_view instructions, std::size_t estimated_tokens,
    ava::provider::Provider const& provider, ava::provider::Transport& transport, runtime::RuntimeRunOptions const& options)
{
  if (options.access_token.empty()) {
    return std::unexpected(
        ava::core::Error(ava::core::ErrorCategory::PermissionDenied, "compaction requires provider access token"));
  }

  auto summary_options = options;
  std::optional<ava::provider::RetryTransport> retry_transport;
  ava::provider::Transport* summary_transport = &transport;
  if (summary_options.enable_transport_retries) {
    retry_transport.emplace(transport, runtime::runtime_retry_options(session, summary_options));
    summary_transport = &*retry_transport;
    summary_options.enable_transport_retries = false;
  }

  constexpr std::string_view system_prompt =
      "You are AVA's deterministic session compaction summarizer. Create faithful continuation context from the "
      "provided session record. Return only the requested Markdown summary; do not include prefaces or code fences.";
  auto const prompt = build_compaction_summary_prompt(entries, config, instructions, estimated_tokens);
  ava::provider::ProviderRequest const provider_request{
      .provider_id = session.model.provider_id,
      .model_id = config.model_id,
      .system_prompt = std::string(system_prompt),
      .messages = {ava::provider::ChatMessage{.role = "user", .content = prompt}},
      .tools_json = {},
      .stream = summary_options.openai_oauth && session.model.supports_streaming.value_or(true),
      .max_output_tokens = session.model.max_output_tokens};
  ava::provider::ProviderAuthContext const auth_context{
      .access_token = summary_options.access_token,
      .credential_type = summary_options.openai_oauth && summary_options.credential_type == "bearer"
                             ? "oauth"
                             : summary_options.credential_type,
      .account_id = summary_options.openai_account_id};
  auto request = provider.build_request(provider_request, auth_context);
  if (!request) return std::unexpected(std::move(request.error()));

  auto response = summary_transport->send(*request, summary_options.cancel_requested);
  if (!response) {
    if (summary_options.cancel_requested && summary_options.cancel_requested()) {
      return std::unexpected(agent_loop_canceled_error());
    }
    return std::unexpected(std::move(response.error()));
  }
  if (summary_options.cancel_requested && summary_options.cancel_requested()) {
    return std::unexpected(agent_loop_canceled_error());
  }
  auto summary = parse_compaction_response_text(provider, *response, provider_request.stream);
  if (!summary) return std::unexpected(std::move(summary.error()));
  *summary = runtime::trimmed_copy(*summary);
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

}  // namespace ava::app

namespace ava::app::runtime {

ava::core::Result<bool> compact_runtime_context(runtime::RuntimeSession& session, ava::session::SessionStore& store,
                                                std::string_view trigger, ava::provider::Provider const& provider,
                                                ava::provider::Transport& transport, runtime::RuntimeRunOptions const& options,
                                                std::vector<std::string> const& replayed_user_messages)
{
  if (options.access_token.empty()) {
    return std::unexpected(
        ava::core::Error(ava::core::ErrorCategory::PermissionDenied, "compaction requires provider access token"));
  }

  auto config = ava::session::load_compaction_config(session.paths);
  if (!config) return std::unexpected(std::move(config.error()));

  constexpr std::size_t max_compaction_attempts = 2;
  auto const trigger_text = std::string(trigger);
  std::size_t last_snapshot_entries = 0;
  std::size_t last_current_entries = 0;
  bool context_retry_event_emitted = false;
  for (std::size_t attempt = 0; attempt < max_compaction_attempts; ++attempt) {
    if (options.cancel_requested && options.cancel_requested()) {
      return std::unexpected(agent_loop_canceled_error());
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

    auto const threshold = ava::session::effective_auto_threshold_tokens(*config, session.model.context_window_tokens);
    std::size_t estimated_tokens = ava::session::estimate_session_tokens(*entries);
    std::size_t threshold_tokens = threshold;
    if (trigger == "auto") {
      auto const decision = ava::session::should_auto_compact(*entries, *config, session.model.context_window_tokens);
      if (!decision.should_compact) return false;
      estimated_tokens = decision.estimated_tokens;
      threshold_tokens = decision.threshold_tokens;
    }

    if (trigger == "context_overflow" && !context_retry_event_emitted) {
      auto retry_event = base_compaction_event_locked(session, options, runtime::RuntimeEventType::Retry);
      retry_event.trigger = trigger_text;
      retry_event.reason = "context_overflow";
      retry_event.status = "started";
      retry_event.attempt = 1;
      retry_event.max_attempts = 1;
      retry_event.estimated_tokens = estimated_tokens;
      retry_event.threshold_tokens = threshold_tokens;
      if (auto emitted = emit_compaction_event(session, options, std::move(retry_event)); !emitted) {
        return std::unexpected(std::move(emitted.error()));
      }
      context_retry_event_emitted = true;
    }

    auto start_event = base_compaction_event_locked(session, options, runtime::RuntimeEventType::CompactionStart);
    start_event.trigger = trigger_text;
    start_event.status = "started";
    start_event.attempt = attempt + 1;
    start_event.max_attempts = max_compaction_attempts;
    start_event.estimated_tokens = estimated_tokens;
    start_event.threshold_tokens = threshold_tokens;
    if (auto emitted = emit_compaction_event(session, options, std::move(start_event)); !emitted) {
      return std::unexpected(std::move(emitted.error()));
    }

    auto summary =
        generate_compaction_summary(session, *entries, *config, "", estimated_tokens, provider, transport, options);
    if (!summary) return std::unexpected(std::move(summary.error()));
    auto recent_context = build_recent_context_tail(*entries, config->keep_recent_messages, config->keep_recent_tokens,
                                                    replayed_user_messages);
    if (!recent_context) return std::unexpected(std::move(recent_context.error()));
    if (options.cancel_requested && options.cancel_requested()) {
      return std::unexpected(agent_loop_canceled_error());
    }

    bool snapshot_stale = false;
    auto validate_and_append = [&]() -> ava::core::Result<bool> {
      auto current_entries = store.load();
      if (!current_entries) return std::unexpected(std::move(current_entries.error()));
      if (options.cancel_requested && options.cancel_requested()) {
        return std::unexpected(agent_loop_canceled_error());
      }
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
    if (*appended) {
      auto end_event = base_compaction_event_locked(session, options, runtime::RuntimeEventType::CompactionEnd);
      end_event.trigger = trigger_text;
      end_event.status = "completed";
      end_event.attempt = attempt + 1;
      end_event.max_attempts = max_compaction_attempts;
      end_event.estimated_tokens = estimated_tokens;
      end_event.threshold_tokens = threshold_tokens;
      end_event.summary_bytes = summary->size();
      if (auto emitted = emit_compaction_event(session, options, std::move(end_event)); !emitted) {
        return std::unexpected(std::move(emitted.error()));
      }
      return true;
    }
    if (snapshot_stale && attempt + 1 < max_compaction_attempts) {
      auto retry_event = base_compaction_event_locked(session, options, runtime::RuntimeEventType::Retry);
      retry_event.trigger = trigger_text;
      retry_event.reason = "stale_compaction_snapshot";
      retry_event.status = "started";
      retry_event.attempt = attempt + 2;
      retry_event.max_attempts = max_compaction_attempts;
      retry_event.snapshot_entries = last_snapshot_entries;
      retry_event.current_entries = last_current_entries;
      if (auto emitted = emit_compaction_event(session, options, std::move(retry_event)); !emitted) {
        return std::unexpected(std::move(emitted.error()));
      }
    }
    if (!snapshot_stale) return false;
  }
  return std::unexpected(stale_compaction_snapshot_error(trigger_text, last_snapshot_entries, last_current_entries));
}

}  // namespace ava::app::runtime
