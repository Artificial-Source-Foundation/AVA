#include "ava/app/command_sessions.h"

#include "ava/app/command_format.h"

#include "ava/session/compaction.h"
#include "ava/session/export.h"
#include "ava/session/stats.h"

#include "ava/context/context_loader.h"

#include "ava/core/ids.h"

#include <algorithm>
#include <cctype>
#include <iomanip>
#include <mutex>
#include <optional>
#include <sstream>
#include <utility>

namespace ava::app {
namespace {

ava::core::VoidResult append_mode_change(ava::session::SessionStore& store, ava::agent::Mode mode)
{
  return store.append(ava::session::SessionEntry{
      .id = ava::core::make_id("entry"),
      .parent_id = "",
      .type = ava::session::EntryType::ModeChange,
      .timestamp = ava::session::now_timestamp(),
      .data_json = "{\"mode\":\"" + ava::agent::to_string(mode) + "\"}",
  });
}

std::string format_cost_usd(long double value)
{
  std::ostringstream output;
  output << '$' << std::fixed << std::setprecision(6) << value;
  return output.str();
}

std::string trim_ascii(std::string_view text)
{
  while (!text.empty() && std::isspace(static_cast<unsigned char>(text.front())) != 0) text.remove_prefix(1);
  while (!text.empty() && std::isspace(static_cast<unsigned char>(text.back())) != 0) text.remove_suffix(1);
  return std::string(text);
}

std::string lower_ascii(std::string_view text)
{
  std::string lowered(text);
  std::ranges::transform(lowered, lowered.begin(),
                         [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
  return lowered;
}

bool contains_ascii_case_insensitive(std::string_view text, std::string_view query)
{
  if (query.empty()) return true;
  return lower_ascii(text).find(lower_ascii(query)) != std::string::npos;
}

bool session_matches_query(ava::session::SessionSummary const& summary, std::string_view query)
{
  return contains_ascii_case_insensitive(summary.session_id, query) ||
         contains_ascii_case_insensitive(summary.last_updated, query);
}

bool context_source_matches_query(ContextSourceMetadata const& source, std::string_view query)
{
  return contains_ascii_case_insensitive(source.path.generic_string(), query) ||
         contains_ascii_case_insensitive(ava::context::to_string(source.source_type), query);
}

RuntimeEvent base_command_event(RuntimeSession const& session, RuntimeEventType type)
{
  RuntimeEvent event;
  event.type = type;
  event.timestamp = ava::session::now_timestamp();
  event.session_id = session.store.session_id();
  event.mode = session.mode;
  event.provider_id = session.model.provider_id;
  event.model_id = session.model.model_id;
  return event;
}

ava::core::VoidResult emit_command_event(CommandRequest const& request, RuntimeEvent event)
{
  if (!request.event_sink) return {};
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

template <typename Value>
void append_known_value(std::ostringstream& output, bool& wrote_any, std::string_view label,
                        std::optional<Value> const& value)
{
  if (!value) return;
  if (wrote_any) output << ' ';
  output << label << '=' << *value;
  wrote_any = true;
}

std::string shorten_middle(std::string text, std::size_t max_columns)
{
  if (text.size() <= max_columns || max_columns < 8) return text;
  auto const front = (max_columns - 3) / 2;
  auto const back = max_columns - 3 - front;
  return text.substr(0, front) + "..." + text.substr(text.size() - back);
}

std::string compact_workspace_label(std::filesystem::path const& workspace)
{
  auto const filename = workspace.filename().generic_string();
  if (!filename.empty()) return shorten_middle(filename, 32);
  return shorten_middle(workspace.generic_string(), 48);
}

std::string compact_cwd_label(std::filesystem::path const& cwd, std::filesystem::path const& workspace)
{
  auto text = display_path(cwd, workspace);
  if (text.empty()) text = ".";
  return shorten_middle(std::move(text), 48);
}

std::string known_values_text(ava::session::SessionStats const& stats)
{
  std::ostringstream output;
  bool wrote_any = false;
  append_known_value(output, wrote_any, "input", stats.input_tokens);
  append_known_value(output, wrote_any, "output", stats.output_tokens);
  append_known_value(output, wrote_any, "reasoning", stats.reasoning_tokens);
  append_known_value(output, wrote_any, "cache_read", stats.cache_read_tokens);
  append_known_value(output, wrote_any, "cache_write", stats.cache_write_tokens);
  append_known_value(output, wrote_any, "total", stats.total_tokens);
  return wrote_any ? output.str() : std::string("unavailable");
}

std::string estimated_bytes_text(ava::session::SessionStats const& stats)
{
  std::ostringstream output;
  bool wrote_any = false;
  append_known_value(output, wrote_any, "input", stats.estimated_input_bytes);
  append_known_value(output, wrote_any, "output", stats.estimated_output_bytes);
  append_known_value(output, wrote_any, "total", stats.estimated_total_bytes);
  return wrote_any ? output.str() : std::string("unavailable");
}

std::string cost_text(ava::session::SessionStats const& stats)
{
  if (stats.cost_complete) return stats.total_cost_usd ? format_cost_usd(*stats.total_cost_usd) : "unavailable";
  if (stats.known_cost_usd) {
    return "at least " + format_cost_usd(*stats.known_cost_usd) + " (" + std::to_string(stats.unknown_cost_entries) +
           " unknown)";
  }
  return "incomplete (" + std::to_string(stats.unknown_cost_entries) + " unknown)";
}

std::string format_session_stats_text(RuntimeSession const& session, ava::session::SessionStats const& stats)
{
  std::ostringstream output;
  output << "Session stats\n";
  output << "  session: " << shorten_middle(session.store.session_id(), 32) << "   entries: " << stats.entry_count
         << '\n';
  output << "  model: " << session.model.provider_id << '/' << session.model.model_id
         << "   mode: " << ava::agent::to_string(session.mode) << '\n';
  output << "  workspace: " << compact_workspace_label(session.workspace_dir)
         << "   cwd: " << compact_cwd_label(session.current_dir, session.workspace_dir) << '\n';
  if (!stats.first_timestamp.empty() || !stats.last_timestamp.empty()) {
    output << "  time: " << (stats.first_timestamp.empty() ? "unknown" : stats.first_timestamp) << " -> "
           << (stats.last_timestamp.empty() ? "unknown" : stats.last_timestamp) << '\n';
  }

  output << "\nMessages:\n";
  output << "  user " << stats.counts.user_message << "   assistant " << stats.counts.assistant_message << "   tools "
         << stats.counts.tool_call << '/' << stats.counts.tool_result << "   permissions "
         << stats.counts.permission_decision << '\n';
  output << "  compactions " << stats.counts.compaction << "   mode/model " << stats.counts.mode_change << '/'
         << stats.counts.model_change << "   errors/cancels " << stats.counts.error << '/' << stats.counts.cancel
         << '\n';

  output << "\nUsage:\n";
  output << "  tokens: " << known_values_text(stats) << '\n';
  output << "  est bytes: " << estimated_bytes_text(stats) << '\n';
  output << "  cost: " << cost_text(stats) << "   usage entries exact/estimated " << stats.exact_usage_entries << '/'
         << stats.estimated_usage_entries << '\n';

  output << "\nHints:\n";
  output << "  export: /export   resume: ava --session " << session.store.session_id();
  return output.str();
}

}  // namespace

ava::core::Result<CommandResult> run_sessions_command(RuntimeSession& session, std::string_view query)
{
  CommandResult result;
  result.handled = true;
  auto const trimmed_query = trim_ascii(query);
  auto sessions = ava::session::SessionStore::list_sessions(session.workspace_dir, session.paths.sessions_dir);
  if (!sessions) {
    add_output(result, sessions.error().format());
    return result;
  }
  if (sessions->empty()) {
    add_output(result, "No sessions for this workspace.");
    return result;
  }
  std::string output;
  for (auto const& summary : *sessions) {
    if (!session_matches_query(summary, trimmed_query)) continue;
    output += summary.session_id + "  entries=" + std::to_string(summary.entry_count);
    if (!summary.last_updated.empty()) output += "  updated=" + summary.last_updated;
    output += '\n';
  }
  if (output.empty() && !trimmed_query.empty()) {
    add_output(result, "No sessions matching: " + sanitize_inline_text(trimmed_query));
    return result;
  }
  add_output(result, std::move(output));
  return result;
}

ava::core::Result<CommandResult> run_mode_command(RuntimeSession& session)
{
  CommandResult result;
  result.handled = true;
  auto const new_mode = ava::agent::toggle_mode(session.mode);
  auto prompt_state = select_runtime_prompt_state(session, new_mode);
  if (!prompt_state) return std::unexpected(std::move(prompt_state.error()));
  if (auto appended = append_mode_change(session.store, new_mode); !appended) {
    return std::unexpected(std::move(appended.error()));
  }
  apply_runtime_prompt_state(session, std::move(*prompt_state));
  add_output(result, "mode switched to " + ava::agent::to_string(session.mode));
  return result;
}

ava::core::Result<CommandResult> run_context_command(RuntimeSession& session, std::string_view query)
{
  CommandResult result;
  result.handled = true;
  auto const trimmed_query = trim_ascii(query);
  if (session.context_sources.empty()) {
    add_output(result, "No context sources loaded.");
    return result;
  }
  std::string output;
  for (auto const& source : session.context_sources) {
    if (!context_source_matches_query(source, trimmed_query)) continue;
    output += ava::context::to_string(source.source_type) + "  " + source.path.string() +
              "  bytes=" + std::to_string(source.byte_count) + '\n';
  }
  if (output.empty() && !trimmed_query.empty()) {
    add_output(result, "No context sources matching: " + sanitize_inline_text(trimmed_query));
    return result;
  }
  add_output(result, std::move(output));
  return result;
}

ava::core::Result<CommandResult> run_stats_command(RuntimeSession& session)
{
  CommandResult result;
  result.handled = true;
  auto entries = session.store.load();
  if (!entries) {
    add_output(result, entries.error().format());
    return result;
  }
  add_output(result, format_session_stats_text(session, ava::session::compute_session_stats(*entries)));
  return result;
}

ava::core::Result<CommandResult> run_compact_command(RuntimeSession& session, CommandRequest const& request)
{
  CommandResult result;
  result.handled = true;
  auto fail_compaction = [&](ava::core::Error error) -> ava::core::Result<CommandResult> {
    if (request.propagate_compaction_errors) return std::unexpected(std::move(error));
    add_output(result, error.format());
    return result;
  };
  auto const instructions = command_argument(request.command, "/compact");
  if (!request.compaction_summary_generator) {
    return fail_compaction(ava::core::Error(ava::core::ErrorCategory::InvalidArgument,
                                            "/compact requires provider-backed summary generation"));
  }
  auto config = ava::session::load_compaction_config(session.paths);
  if (!config) {
    return fail_compaction(std::move(config.error()));
  }

  constexpr std::size_t max_compaction_attempts = 2;
  std::size_t last_snapshot_entries = 0;
  std::size_t last_current_entries = 0;
  for (std::size_t attempt = 0; attempt < max_compaction_attempts; ++attempt) {
    if (command_canceled(request)) return fail_compaction(command_canceled_error());
    ava::core::Result<std::vector<ava::session::SessionEntry>> entries =
        std::unexpected(ava::core::Error(ava::core::ErrorCategory::Unknown, "session entries were not loaded"));
    if (request.session_mutex) {
      std::lock_guard lock(*request.session_mutex);
      entries = session.store.load();
    } else {
      entries = session.store.load();
    }
    if (!entries) {
      return fail_compaction(std::move(entries.error()));
    }
    auto const estimated_tokens = ava::session::estimate_session_tokens(*entries);
    auto start_event = base_command_event(session, RuntimeEventType::CompactionStart);
    start_event.trigger = "manual";
    start_event.status = "started";
    start_event.attempt = attempt + 1;
    start_event.max_attempts = max_compaction_attempts;
    start_event.estimated_tokens = estimated_tokens;
    if (auto emitted = emit_command_event(request, std::move(start_event)); !emitted) {
      return fail_compaction(std::move(emitted.error()));
    }
    auto summary = request.compaction_summary_generator(*entries, *config, instructions, estimated_tokens);
    if (!summary) {
      return fail_compaction(std::move(summary.error()));
    }
    if (command_canceled(request)) return fail_compaction(command_canceled_error());
    if (summary->empty()) {
      return fail_compaction(ava::core::Error(ava::core::ErrorCategory::Provider,
                                              "compaction summary generation returned an empty summary"));
    }
    if (summary->size() > config->max_summary_bytes) {
      auto error =
          ava::core::Error(ava::core::ErrorCategory::InvalidArgument, "generated compaction summary is too large");
      error.with_context("max_summary_bytes", std::to_string(config->max_summary_bytes));
      error.with_context("summary_bytes", std::to_string(summary->size()));
      return fail_compaction(std::move(error));
    }

    bool snapshot_stale = false;
    auto validate_and_append = [&]() -> ava::core::VoidResult {
      auto current_entries = session.store.load();
      if (!current_entries) return std::unexpected(std::move(current_entries.error()));
      if (command_canceled(request)) return std::unexpected(command_canceled_error());
      if (!same_session_snapshot(*entries, *current_entries)) {
        snapshot_stale = true;
        last_snapshot_entries = entries->size();
        last_current_entries = current_entries->size();
        return {};
      }
      return ava::session::append_manual_compaction(
          session.store, ava::session::ManualCompactionRequest{.summary = *summary,
                                                               .instructions = instructions,
                                                               .config = *config,
                                                               .estimated_tokens = estimated_tokens,
                                                               .threshold_tokens = 0,
                                                               .trigger = "manual",
                                                               .recent_context = ""});
    };
    ava::core::VoidResult appended;
    if (request.session_mutex) {
      std::lock_guard lock(*request.session_mutex);
      appended = validate_and_append();
    } else {
      appended = validate_and_append();
    }
    if (!appended) {
      return fail_compaction(std::move(appended.error()));
    }
    if (!snapshot_stale) {
      auto end_event = base_command_event(session, RuntimeEventType::CompactionEnd);
      end_event.trigger = "manual";
      end_event.status = "completed";
      end_event.attempt = attempt + 1;
      end_event.max_attempts = max_compaction_attempts;
      end_event.estimated_tokens = estimated_tokens;
      end_event.summary_bytes = summary->size();
      if (auto emitted = emit_command_event(request, std::move(end_event)); !emitted) {
        return fail_compaction(std::move(emitted.error()));
      }
      add_output(result, "compaction summary recorded");
      return result;
    }
    if (attempt + 1 < max_compaction_attempts) {
      auto retry_event = base_command_event(session, RuntimeEventType::Retry);
      retry_event.trigger = "manual";
      retry_event.reason = "stale_compaction_snapshot";
      retry_event.status = "started";
      retry_event.attempt = attempt + 2;
      retry_event.max_attempts = max_compaction_attempts;
      retry_event.snapshot_entries = last_snapshot_entries;
      retry_event.current_entries = last_current_entries;
      if (auto emitted = emit_command_event(request, std::move(retry_event)); !emitted) {
        return fail_compaction(std::move(emitted.error()));
      }
    }
  }
  return fail_compaction(stale_compaction_snapshot_error("manual", last_snapshot_entries, last_current_entries));
}

ava::core::Result<CommandResult> run_export_command(RuntimeSession& session)
{
  CommandResult result;
  result.handled = true;
  auto entries = session.store.load();
  if (!entries) {
    add_output(result, entries.error().format());
    return result;
  }
  add_output(result, ava::session::format_session_markdown(*entries));
  return result;
}

}  // namespace ava::app
