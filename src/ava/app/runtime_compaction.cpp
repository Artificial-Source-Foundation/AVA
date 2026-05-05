#include "ava/app/runtime_compaction.h"

#include <optional>
#include <utility>

#include "ava/app/runtime_compaction_support.h"
#include "ava/app/runtime_json.h"
#include "ava/app/runtime_retry.h"

namespace ava::app {
namespace {

RuntimeEvent base_compaction_event_locked(RuntimeSession const& session, RuntimeRunOptions const& options,
                                          RuntimeEventType type)
{
  auto build = [&] {
    RuntimeEvent event;
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

ava::core::VoidResult emit_compaction_event(RuntimeSession const& session, RuntimeRunOptions const& options,
                                            RuntimeEvent event)
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

}  // namespace

ava::core::Result<std::string> generate_compaction_summary(
    RuntimeSession const& session, std::vector<ava::session::SessionEntry> const& entries,
    ava::session::CompactionConfig const& config, std::string_view instructions, std::size_t estimated_tokens,
    ava::provider::Provider const& provider, ava::provider::Transport& transport, RuntimeRunOptions const& options)
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
  if (!response) return std::unexpected(std::move(response.error()));
  auto summary = detail::parse_compaction_response_text(provider, *response, provider_request.stream);
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

ava::core::Result<bool> compact_runtime_context(RuntimeSession& session, ava::session::SessionStore& store,
                                                std::string_view trigger, ava::provider::Provider const& provider,
                                                ava::provider::Transport& transport, RuntimeRunOptions const& options,
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
      auto retry_event = base_compaction_event_locked(session, options, RuntimeEventType::Retry);
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

    auto start_event = base_compaction_event_locked(session, options, RuntimeEventType::CompactionStart);
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
    auto recent_context = detail::build_recent_context_tail(*entries, config->keep_recent_messages,
                                                            config->keep_recent_tokens, replayed_user_messages);
    if (!recent_context) return std::unexpected(std::move(recent_context.error()));
    if (options.cancel_requested && options.cancel_requested()) {
      return std::unexpected(ava::core::Error(ava::core::ErrorCategory::Unknown, "agent loop canceled"));
    }

    bool snapshot_stale = false;
    auto validate_and_append = [&]() -> ava::core::Result<bool> {
      auto current_entries = store.load();
      if (!current_entries) return std::unexpected(std::move(current_entries.error()));
      if (options.cancel_requested && options.cancel_requested()) {
        return std::unexpected(ava::core::Error(ava::core::ErrorCategory::Unknown, "agent loop canceled"));
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
      auto end_event = base_compaction_event_locked(session, options, RuntimeEventType::CompactionEnd);
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
      auto retry_event = base_compaction_event_locked(session, options, RuntimeEventType::Retry);
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
