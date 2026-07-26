#include "sys.h"
#include "ava/app/command_format.h"
#include "ava/app/command_sessions.h"
#include "ava/app/events.h"
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

runtime::Event base_command_event(runtime::Session const& session, runtime::EventType type)
{
  runtime::Event event;
  event.type = type;
  event.timestamp = ava::session::now_timestamp();
  event.session_id = session.store.session_id();
  event.mode = session.mode();
  event.provider_id = session.model().provider_id;
  event.model_id = session.model().model_id;
  return event;
}

ava::core::VoidResult emit_command_event(CommandRequest const& request, runtime::Event event)
{
  if (!request.event_sink)
    return {};
  return emit_event(request.event_sink, event);
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
    auto start_event = base_command_event(session, runtime::EventType::CompactionStart);
    start_event.provider_id = config->provider_id;
    start_event.model_id = config->model_id;
    start_event.trigger = "manual";
    start_event.reason = "manual";
    start_event.status = "started";
    start_event.attempt = attempt + 1;
    start_event.max_attempts = max_compaction_attempts;
    start_event.estimated_tokens = prepared->estimated_tokens;
    start_event.threshold_tokens = ava::session::effective_auto_threshold_tokens(*config, session.model().context_window_tokens);
    start_event.retained_tokens = prepared->retained_tokens;
    if (auto emitted = emit_command_event(request, std::move(start_event)); !emitted)
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
      auto entry = ava::session::make_manual_compaction_entry(
          ava::session::ManualCompactionRequest{.summary = *summary,
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
      auto end_event = base_command_event(session, runtime::EventType::CompactionEnd);
      end_event.provider_id = config->provider_id;
      end_event.model_id = config->model_id;
      end_event.trigger = "manual";
      end_event.reason = "manual";
      end_event.status = "completed";
      end_event.attempt = attempt + 1;
      end_event.max_attempts = max_compaction_attempts;
      end_event.estimated_tokens = prepared->estimated_tokens;
      end_event.threshold_tokens = ava::session::effective_auto_threshold_tokens(*config, session.model().context_window_tokens);
      end_event.retained_tokens = prepared->retained_tokens;
      end_event.post_compaction_tokens = ava::session::estimate_tokens(*summary) + ava::session::estimate_tokens(instructions) + prepared->retained_tokens;
      end_event.summary_bytes = summary->size();
      if (auto emitted = emit_command_event(request, std::move(end_event)); !emitted)
      {
        return fail_compaction(std::move(emitted.error()));
      }
      add_output(result, "compaction summary recorded");
      return result;
    }
    if (attempt + 1 < max_compaction_attempts)
    {
      auto retry_event = base_command_event(session, runtime::EventType::Retry);
      retry_event.trigger = "manual";
      retry_event.reason = "stale_compaction_snapshot";
      retry_event.status = "started";
      retry_event.attempt = attempt + 2;
      retry_event.max_attempts = max_compaction_attempts;
      retry_event.snapshot_entries = last_snapshot_entries;
      retry_event.current_entries = last_current_entries;
      if (auto emitted = emit_command_event(request, std::move(retry_event)); !emitted)
      {
        return fail_compaction(std::move(emitted.error()));
      }
    }
  }
  return fail_compaction(stale_compaction_snapshot_error("manual", last_snapshot_entries, last_current_entries));
}

}  // namespace ava::app
