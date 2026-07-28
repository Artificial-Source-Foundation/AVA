#include "sys.h"
#include "ava/event/events.h"
#include "ava/app/command_format.h"
#include "ava/app/command_sessions.h"
#include "ava/app/runtime.h"
#include "ava/app/runtime/Session.h"
#include "ava/session/compaction.h"
#include "ava/session/record.h"

#include <cstddef>
#include <mutex>
#include <string>
#include <utility>
#include <vector>

namespace ava::app {
namespace {

ava::event::RuntimeEventMetadata command_event_metadata(runtime::Session const& session)
{
  return ava::event::RuntimeEventMetadata{
      .timestamp = ava::session::now_timestamp(),
      .session_id = session.store.session_id(),
  };
}

bool command_canceled(CommandRequest const& request)
{
  return request.cancel_requested && request.cancel_requested();
}

ava::core::Error command_canceled_error()
{
  return ava::core::Error(ava::core::ErrorCategory::Unknown, "agent loop canceled");
}

}  // namespace

ava::core::Result<CommandResult> run_compact_command(runtime::Session& session, CommandRequest const& request)
{
  CommandResult result;
  result.handled = true;
  auto fail_compaction = [&](ava::core::Error error) -> ava::core::Result<CommandResult> {
    if (request.propagate_compaction_errors)
      return std::unexpected(std::move(error));
    add_output(result, error.format());
    return result;
  };
  auto const instructions = command_argument(request.command, "/compact");
  if (!request.compaction_summary_generator)
  {
    return fail_compaction(ava::core::Error(ava::core::ErrorCategory::InvalidArgument, "/compact requires provider-backed summary generation"));
  }
  auto loaded_config = ava::session::load_compaction_config(session.paths());
  if (!loaded_config)
  {
    return fail_compaction(std::move(loaded_config.error()));
  }
  auto config = resolve_compaction_config(session, std::move(*loaded_config));
  if (!config)
  {
    return fail_compaction(std::move(config.error()));
  }
  auto read_authority = session.read_authority();
  if (!read_authority)
    return fail_compaction(std::move(read_authority.error()));

  constexpr std::size_t max_compaction_attempts = 2;
  std::size_t last_snapshot_entries = 0;
  std::size_t last_current_entries = 0;
  for (std::size_t attempt = 0; attempt < max_compaction_attempts; ++attempt)
  {
    if (command_canceled(request))
      return fail_compaction(command_canceled_error());
    ava::core::Result<std::vector<ava::session::SessionEntry>> entries =
        std::unexpected(ava::core::Error(ava::core::ErrorCategory::Unknown, "session entries were not loaded"));
    if (request.session_mutex)
    {
      std::lock_guard lock(*request.session_mutex);
      entries = read_authority->load();
    }
    else
    {
      entries = read_authority->load();
    }
    if (!entries)
    {
      return fail_compaction(std::move(entries.error()));
    }
    auto prepared = prepare_compaction_context(*entries, *config);
    if (!prepared)
      return fail_compaction(std::move(prepared.error()));
    ava::event::CompactionPayload start_payload{
        .provider = config->provider_id,
        .model = config->model_id,
        .status = "started",
        .trigger = "manual",
        .reason = "manual",
        .attempt = attempt + 1,
        .max_attempts = max_compaction_attempts,
        .estimated_tokens = prepared->estimated_tokens,
        .threshold_tokens = ava::session::effective_auto_threshold_tokens(*config, session.model().context_window_tokens),
        .retained_tokens = prepared->retained_tokens,
    };
    if (auto emitted = ava::event::emit_event(
            request.event_sink,
            ava::event::RuntimeEvent{command_event_metadata(session), ava::event::CompactionStartEvent{.payload = std::move(start_payload)}});
        !emitted)
    {
      return fail_compaction(std::move(emitted.error()));
    }
    auto summary = request.compaction_summary_generator(prepared->active_entries, *config, instructions, prepared->estimated_tokens);
    if (!summary)
    {
      return fail_compaction(std::move(summary.error()));
    }
    if (command_canceled(request))
      return fail_compaction(command_canceled_error());
    if (summary->empty())
    {
      return fail_compaction(ava::core::Error(ava::core::ErrorCategory::Provider, "compaction summary generation returned an empty summary"));
    }
    if (summary->size() > config->max_summary_bytes)
    {
      auto error = ava::core::Error(ava::core::ErrorCategory::InvalidArgument, "generated compaction summary is too large");
      error.with_context("max_summary_bytes", std::to_string(config->max_summary_bytes));
      error.with_context("summary_bytes", std::to_string(summary->size()));
      return fail_compaction(std::move(error));
    }

    bool snapshot_stale = false;
    auto validate_and_append = [&]() -> ava::core::VoidResult {
      auto current_entries = read_authority->load();
      if (!current_entries)
        return std::unexpected(std::move(current_entries.error()));
      if (command_canceled(request))
        return std::unexpected(command_canceled_error());
      if (!same_session_snapshot(*entries, *current_entries))
      {
        snapshot_stale = true;
        last_snapshot_entries = entries->size();
        last_current_entries = current_entries->size();
        return {};
      }
      auto entry = ava::session::make_manual_compaction_entry(ava::session::ManualCompactionRequest{
          .summary = *summary,
          .instructions = instructions,
          .config = *config,
          .estimated_tokens = prepared->estimated_tokens,
          .threshold_tokens = ava::session::effective_auto_threshold_tokens(*config, session.model().context_window_tokens),
          .retained_tokens = prepared->retained_tokens,
          .trigger = "manual",
          .recent_context = prepared->recent_context,
          .recent_context_omitted = prepared->recent_context_omitted});
      if (!entry)
        return std::unexpected(std::move(entry.error()));
      return session.append_owned(std::move(*entry));
    };
    ava::core::VoidResult appended;
    if (request.session_mutex)
    {
      std::lock_guard lock(*request.session_mutex);
      appended = validate_and_append();
    }
    else
    {
      appended = validate_and_append();
    }
    if (!appended)
    {
      return fail_compaction(std::move(appended.error()));
    }
    if (!snapshot_stale)
    {
      ava::event::CompactionPayload end_payload{
          .provider = config->provider_id,
          .model = config->model_id,
          .status = "completed",
          .trigger = "manual",
          .reason = "manual",
          .attempt = attempt + 1,
          .max_attempts = max_compaction_attempts,
          .estimated_tokens = prepared->estimated_tokens,
          .threshold_tokens = ava::session::effective_auto_threshold_tokens(*config, session.model().context_window_tokens),
          .retained_tokens = prepared->retained_tokens,
          .post_compaction_tokens = ava::session::estimate_tokens(*summary) + ava::session::estimate_tokens(instructions) + prepared->retained_tokens,
          .summary_bytes = summary->size(),
      };
      if (auto emitted = ava::event::emit_event(
              request.event_sink, ava::event::RuntimeEvent{command_event_metadata(session), ava::event::CompactionEndEvent{.payload = std::move(end_payload)}});
          !emitted)
      {
        return fail_compaction(std::move(emitted.error()));
      }
      add_output(result, "compaction summary recorded");
      return result;
    }
    if (attempt + 1 < max_compaction_attempts)
    {
      ava::event::RetryPayload retry_payload;
      retry_payload.status = "started";
      retry_payload.trigger = "manual";
      retry_payload.reason = "stale_compaction_snapshot";
      retry_payload.attempt = attempt + 2;
      retry_payload.max_attempts = max_compaction_attempts;
      ava::event::RetryDiagnostics retry_diagnostics;
      retry_diagnostics.snapshot_entries = last_snapshot_entries;
      retry_diagnostics.current_entries = last_current_entries;
      if (auto emitted = ava::event::emit_event(
              request.event_sink,
              ava::event::RuntimeEvent{command_event_metadata(session),
                                       ava::event::RetryEvent{.payload = std::move(retry_payload), .diagnostics = std::move(retry_diagnostics)}});
          !emitted)
      {
        return fail_compaction(std::move(emitted.error()));
      }
    }
  }
  return fail_compaction(stale_compaction_snapshot_error("manual", last_snapshot_entries, last_current_entries));
}

}  // namespace ava::app
