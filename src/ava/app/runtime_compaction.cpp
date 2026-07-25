#include "sys.h"
#include "ava/app/runtime_compaction.h"
#include "ava/app/runtime_credentials.h"
#include "ava/app/runtime_json.h"
#include "ava/app/runtime_retry.h"
#include "ava/app/runtime/Session.h"
#include "ava/agent/message_builder.h"
#include "ava/session/logical_projection.h"
#include "ava/session/validation.h"
#include "ava/provider/curl_transport.h"
#include "ava/provider/registry.h"
#include "ava/core/json.h"
#include "ava/core/string_utils.h"

#include <algorithm>
#include <optional>
#include <utility>

namespace ava::app {
namespace {

constexpr std::size_t kMaxCompactionPromptEntryBytes = 8192;

runtime::Event base_compaction_event_locked(runtime::Session const& session, runtime::RunOptions const& options, runtime::EventType type)
{
  auto build = [&] {
    runtime::Event event;
    event.type = type;
    event.timestamp = ava::session::now_timestamp();
    event.session_id = session.store.session_id();
    event.mode = session.mode();
    event.provider_id = session.model.provider_id;
    event.model_id = session.model.model_id;
    return event;
  };
  if (!options.session_mutex)
    return build();
  std::lock_guard lock(*options.session_mutex);
  return build();
}

ava::core::VoidResult emit_compaction_event(runtime::Session const& session, runtime::RunOptions const& options, runtime::Event event)
{
  if (!options.event_sink)
    return {};
  if (event.timestamp.empty())
  {
    event.timestamp = ava::session::now_timestamp();
  }
  if (event.session_id.empty())
  {
    if (options.session_mutex)
    {
      std::lock_guard lock(*options.session_mutex);
      event.session_id = session.store.session_id();
    }
    else
    {
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
  if (data.size() <= kMaxCompactionPromptEntryBytes)
    return std::string(data);
  return std::string(data.substr(0, kMaxCompactionPromptEntryBytes)) + "\n[entry data truncated from " + std::to_string(data.size()) + " bytes]";
}

std::vector<ava::session::SessionEntry> physical_active_compaction_tail(std::vector<ava::session::SessionEntry> const& entries)
{
  auto const boundary = std::ranges::find_if(entries.rbegin(), entries.rend(), [](auto const& entry) {
    return entry.type == ava::session::EntryType::Compaction && ava::core::json::is_valid_object(entry.data_json) &&
           !ava::core::json::string_field(entry.data_json, "summary").value_or("").empty();
  });
  if (boundary == entries.rend())
    return entries;
  auto const start = static_cast<std::size_t>(std::distance(entries.begin(), std::prev(boundary.base())));
  return std::vector<ava::session::SessionEntry>(entries.begin() + static_cast<std::ptrdiff_t>(start), entries.end());
}

bool is_utf8_continuation_byte(char value)
{
  return (static_cast<unsigned char>(value) & 0xC0U) == 0x80U;
}

std::size_t utf8_prefix_size(std::string_view text, std::size_t prefix_bytes)
{
  auto size = std::min(prefix_bytes, text.size());
  while (size > 0 && size < text.size() && is_utf8_continuation_byte(text[size])) --size;
  return size;
}

bool message_has_part(ava::provider::ChatMessage const& message, ava::provider::ContentPartType type)
{
  if (std::ranges::any_of(message.content_parts, [&](auto const& part) { return part.type == type; }))
    return true;
  // Suppressed or compatibility-only tool records can intentionally lack
  // provider-native parts; their fallback payload is still structured and
  // must remain atomic for retention.
  if (type == ava::provider::ContentPartType::ToolUse)
    return message.content.starts_with("Tool call requested by assistant.") || message.content.starts_with("Tool call (") ||
           message.content.find("\n\nTool call requested by assistant.") != std::string::npos || message.content.find("\n\nTool call (") != std::string::npos;
  if (type == ava::provider::ContentPartType::ToolResult)
    return message.content.starts_with("Tool result data only") || message.content.starts_with("Tool result (");
  return false;
}

bool starts_user_turn(ava::provider::ChatMessage const& message)
{
  return message.role == "user" && !message_has_part(message, ava::provider::ContentPartType::ToolResult);
}

std::string render_message_range(std::vector<ava::provider::ChatMessage> const& messages, std::size_t begin, std::size_t end)
{
  std::string text;
  for (auto index = begin; index < end; ++index)
  {
    if (!text.empty())
      text += "\n\n";
    text += messages[index].role;
    text += ":\n";
    text += messages[index].content;
  }
  return text;
}

void erase_replayed_active_user_messages(std::vector<ava::provider::ChatMessage>& messages, std::vector<std::string> const& replayed_user_messages)
{
  for (auto replay = replayed_user_messages.rbegin(); replay != replayed_user_messages.rend(); ++replay)
  {
    auto const match = std::ranges::find_if(messages.rbegin(), messages.rend(), [&](auto const& message) {
      if (!starts_user_turn(message))
        return false;
      if (message.content == *replay)
        return true;
      auto const image_suffix = *replay + "\n\n[historical image omitted:";
      return message.content.starts_with(image_suffix);
    });
    if (match != messages.rend())
      messages.erase(std::next(match).base());
  }
}

std::vector<std::pair<std::size_t, std::size_t>> complete_selection_ranges(std::vector<ava::provider::ChatMessage> const& messages,
                                                                           ava::session::CompactionConfig const& config)
{
  std::vector<std::pair<std::size_t, std::size_t>> ranges;
  if (messages.empty())
    return ranges;
  if (config.keep_recent_messages_explicit)
  {
    auto start = messages.size() - std::min(config.keep_recent_messages, messages.size());
    while (start > 0 && message_has_part(messages[start], ava::provider::ContentPartType::ToolResult)) --start;
    if (start < messages.size())
      ranges.emplace_back(start, messages.size());
    return ranges;
  }

  std::vector<std::size_t> turn_starts;
  for (std::size_t index = 0; index < messages.size(); ++index)
    if (starts_user_turn(messages[index]))
      turn_starts.push_back(index);
  if (turn_starts.empty() || config.keep_recent_turns == 0)
    return ranges;
  auto const first_turn = turn_starts.size() - std::min(config.keep_recent_turns, turn_starts.size());
  for (auto turn = first_turn; turn < turn_starts.size(); ++turn)
    ranges.emplace_back(turn_starts[turn], turn + 1 < turn_starts.size() ? turn_starts[turn + 1] : messages.size());
  return ranges;
}

struct RetentionUnit
{
  std::size_t begin = 0;
  std::size_t end = 0;
  bool tool_bearing = false;
};

std::vector<RetentionUnit> retention_units(std::vector<ava::provider::ChatMessage> const& messages, std::pair<std::size_t, std::size_t> range)
{
  std::vector<RetentionUnit> units;
  for (auto index = range.first; index < range.second;)
  {
    auto end = index + 1;
    bool const tool_use = message_has_part(messages[index], ava::provider::ContentPartType::ToolUse);
    bool const tool_result = message_has_part(messages[index], ava::provider::ContentPartType::ToolResult);
    if (tool_use)
    {
      while (end < range.second && message_has_part(messages[end], ava::provider::ContentPartType::ToolResult)) ++end;
    }
    units.push_back(RetentionUnit{.begin = index, .end = end, .tool_bearing = tool_use || tool_result});
    index = end;
  }
  return units;
}

std::string truncated_plain_message(ava::provider::ChatMessage const& message, std::size_t max_bytes)
{
  auto const full = message.role + ":\n" + message.content;
  if (full.size() <= max_bytes)
    return full;

  auto const label = message.role + ":\n";
  std::string const marker = "\n[AVA: plain text truncated]";
  if (max_bytes <= label.size())
    return label.substr(0, max_bytes);
  if (max_bytes <= label.size() + marker.size())
    return (label + marker).substr(0, max_bytes);
  auto const content_bytes = max_bytes - label.size() - marker.size();
  return label + message.content.substr(0, utf8_prefix_size(message.content, content_bytes)) + marker;
}

std::string partial_latest_range(std::vector<ava::provider::ChatMessage> const& messages, std::pair<std::size_t, std::size_t> range, std::size_t max_bytes)
{
  if (range.first == range.second || max_bytes == 0)
    return {};
  auto const units = retention_units(messages, range);
  auto const anchor = std::ranges::find_if(units, [&](auto const& unit) { return starts_user_turn(messages[unit.begin]); });
  std::vector<std::string> suffix;
  std::size_t suffix_bytes = 0;

  if (anchor == units.end())
  {
    for (auto unit = units.rbegin(); unit != units.rend(); ++unit)
    {
      auto rendered = render_message_range(messages, unit->begin, unit->end);
      auto const needed = rendered.size() + (suffix.empty() ? 0 : 2);
      if (needed > max_bytes - std::min(max_bytes, suffix_bytes))
      {
        if (suffix.empty() && !unit->tool_bearing)
          suffix.push_back(truncated_plain_message(messages[unit->begin], max_bytes));
        break;
      }
      suffix_bytes += needed;
      suffix.push_back(std::move(rendered));
    }
    std::reverse(suffix.begin(), suffix.end());
  }
  else
  {
    auto const anchor_full = render_message_range(messages, anchor->begin, anchor->end);
    auto const anchor_minimum = std::min(anchor_full.size(), messages[anchor->begin].role.size() + std::string_view(":\n\n[AVA: plain text truncated]").size());
    for (auto unit = units.rbegin(); unit.base() != std::next(anchor); ++unit)
    {
      auto rendered = render_message_range(messages, unit->begin, unit->end);
      auto const next_suffix_bytes = suffix_bytes + rendered.size() + (suffix.empty() ? 0 : 2);
      auto const separator = next_suffix_bytes == 0 ? 0 : 2;
      if (anchor_minimum + separator > max_bytes || next_suffix_bytes > max_bytes - anchor_minimum - separator)
        break;
      suffix_bytes = next_suffix_bytes;
      suffix.push_back(std::move(rendered));
    }
    std::reverse(suffix.begin(), suffix.end());
    auto const separator = suffix.empty() ? 0 : 2;
    auto const anchor_budget = max_bytes - std::min(max_bytes, suffix_bytes + separator);
    auto anchor_text = truncated_plain_message(messages[anchor->begin], anchor_budget);
    suffix.insert(suffix.begin(), std::move(anchor_text));
  }

  std::string result;
  for (auto const& item : suffix)
  {
    if (item.empty())
      continue;
    if (!result.empty())
      result += "\n\n";
    result += item;
  }
  return result;
}

std::pair<std::string, bool> retain_ranges_with_budget(std::vector<ava::provider::ChatMessage> const& messages,
                                                       std::vector<std::pair<std::size_t, std::size_t>> const& ranges, std::size_t keep_recent_tokens)
{
  if (ranges.empty() || keep_recent_tokens == 0)
    return {};
  auto const max_bytes = keep_recent_tokens * 4;
  std::vector<std::string> all_ranges;
  for (auto const& range : ranges) all_ranges.push_back(render_message_range(messages, range.first, range.second));
  std::string complete;
  for (auto const& rendered : all_ranges)
  {
    if (!complete.empty())
      complete += "\n\n";
    complete += rendered;
  }
  if (complete.size() <= max_bytes)
    return {std::move(complete), false};

  std::string const marker = "[AVA: recent context tail truncated; completed prefix omitted]\n";
  if (max_bytes <= marker.size())
    return {marker.substr(0, max_bytes), true};
  auto const content_budget = max_bytes - marker.size();
  std::vector<std::string> retained;
  std::size_t retained_bytes = 0;
  for (auto range = ranges.rbegin(); range != ranges.rend(); ++range)
  {
    auto rendered = render_message_range(messages, range->first, range->second);
    auto const needed = rendered.size() + (retained.empty() ? 0 : 2);
    if (needed <= content_budget - std::min(content_budget, retained_bytes))
    {
      retained_bytes += needed;
      retained.push_back(std::move(rendered));
      continue;
    }
    if (retained.empty())
    {
      auto partial = partial_latest_range(messages, *range, content_budget);
      if (!partial.empty())
        retained.push_back(std::move(partial));
    }
    break;
  }
  std::reverse(retained.begin(), retained.end());
  std::string tail = marker;
  for (auto const& item : retained)
  {
    if (tail.size() > marker.size())
      tail += "\n\n";
    tail += item;
  }
  return {std::move(tail), true};
}

ava::core::Result<std::string> parse_compaction_response_text(ava::provider::Provider const& provider, ava::provider::HttpResponse const& response, bool stream)
{
  auto events = provider.parse_response(response, stream);
  if (!events)
  {
    // Provider parser errors may contain response diagnostics. Keep only the
    // locally-derived HTTP status on the public/session-facing error.
    auto error = ava::core::Error(ava::core::ErrorCategory::Provider, "compaction summary request failed with status " + std::to_string(response.status_code));
    error.with_context("status", std::to_string(response.status_code));
    return std::unexpected(std::move(error));
  }
  std::string streamed_text;
  for (auto const& event : *events)
  {
    if (event.type == ava::provider::StreamEventType::TextDelta)
    {
      streamed_text += event.text;
    }
    else if (event.type == ava::provider::StreamEventType::Error)
    {
      return std::unexpected(ava::core::Error(ava::core::ErrorCategory::Provider, "compaction summary stream error"));
    }
  }
  if (!streamed_text.empty())
    return streamed_text;
  return std::unexpected(ava::core::Error(ava::core::ErrorCategory::Provider, "compaction summary response text is missing"));
}

}  // namespace

bool same_session_snapshot(std::vector<ava::session::SessionEntry> const& expected, std::vector<ava::session::SessionEntry> const& actual)
{
  if (actual.size() < expected.size())
    return false;
  for (std::size_t index = 0; index < expected.size(); ++index)
  {
    if (expected[index].id != actual[index].id || expected[index].parent_id != actual[index].parent_id || expected[index].type != actual[index].type ||
        expected[index].timestamp != actual[index].timestamp || expected[index].data_json != actual[index].data_json)
    {
      return false;
    }
  }
  // Automatic title metadata is context-neutral. Allow it to finish while a
  // provider-backed compaction is in flight without spending a retry or
  // treating the generated title as conversation history.
  for (std::size_t index = expected.size(); index < actual.size(); ++index)
  {
    auto const& entry = actual[index];
    if (entry.type != ava::session::EntryType::SessionMetadata || ava::core::json::string_field(entry.data_json, "actor").value_or("") != "auto-title" ||
        !ava::core::json::string_field(entry.data_json, "generated_title") || ava::core::json::field_value_start(entry.data_json, "name") ||
        ava::core::json::field_value_start(entry.data_json, "labels") || ava::core::json::field_value_start(entry.data_json, "archived") ||
        ava::core::json::field_value_start(entry.data_json, "parent_session_id") || ava::core::json::field_value_start(entry.data_json, "source_session_id") ||
        ava::core::json::field_value_start(entry.data_json, "branch_from_entry_id") || ava::core::json::field_value_start(entry.data_json, "branch_origin") ||
        ava::core::json::field_value_start(entry.data_json, "original_cwd"))
    {
      return false;
    }
  }
  return true;
}

ava::core::Error stale_compaction_snapshot_error(std::string_view trigger, std::size_t snapshot_entries, std::size_t current_entries)
{
  auto error = ava::core::Error(ava::core::ErrorCategory::Session, "session changed during context compaction after retry");
  error.with_context("trigger", std::string(trigger));
  error.with_context("snapshot_entries", std::to_string(snapshot_entries));
  error.with_context("current_entries", std::to_string(current_entries));
  return error;
}

ava::core::Result<ava::session::CompactionConfig> resolve_compaction_config(runtime::Session const& session, ava::session::CompactionConfig config)
{
  if (!config.model_explicit)
  {
    config.provider_id = session.model.provider_id;
    config.model_id = session.model.model_id;
    return config;
  }
  auto const provider_id = config.provider_explicit ? config.provider_id : session.model.provider_id;
  auto const model_id = config.model_id;
  auto model = resolve_runtime_model(session.paths(), provider_id, model_id);
  if (!model)
  {
    model.error().with_context("compaction_provider", provider_id).with_context("compaction_model", model_id);
    return std::unexpected(std::move(model.error()));
  }
  config.provider_id = model->provider_id;
  config.model_id = model->model_id;
  return config;
}

ava::core::Result<PreparedCompactionContext> prepare_compaction_context(std::vector<ava::session::SessionEntry> const& entries,
                                                                        ava::session::CompactionConfig const& config,
                                                                        std::vector<std::string> const& replayed_user_messages)
{
  auto tail_entries = physical_active_compaction_tail(entries);
  tail_entries.erase(std::remove_if(tail_entries.begin(), tail_entries.end(), ava::session::is_internal_replay_user_message), tail_entries.end());
  auto messages = ava::agent::build_provider_messages_from_entries(
      tail_entries, ava::agent::MessageBuildOptions{.replay_mode = ava::agent::HistoryReplayMode::ForcePortable});
  if (!messages)
    return std::unexpected(std::move(messages.error()));
  erase_replayed_active_user_messages(*messages, replayed_user_messages);

  std::vector<ava::session::SessionEntry> portable_entries;
  portable_entries.reserve(messages->size());
  for (std::size_t index = 0; index < messages->size(); ++index)
  {
    auto const& message = messages->at(index);
    portable_entries.push_back(
        ava::session::SessionEntry{.id = "compaction_projection_" + std::to_string(index + 1),
                                   .parent_id = "",
                                   .type = message.role == "assistant" ? ava::session::EntryType::AssistantMessage : ava::session::EntryType::UserMessage,
                                   .timestamp = "",
                                   .data_json = "{\"text\":\"" + ava::core::json::escape(message.content) + "\"}"});
  }
  auto const estimated = ava::session::estimate_tokens(render_message_range(*messages, 0, messages->size()));
  auto ranges = complete_selection_ranges(*messages, config);
  auto [recent_context, omitted] = retain_ranges_with_budget(*messages, ranges, config.keep_recent_tokens);
  auto const retained_tokens = ava::session::estimate_tokens(recent_context);
  return PreparedCompactionContext{.active_entries = std::move(portable_entries),
                                   .recent_context = std::move(recent_context),
                                   .estimated_tokens = estimated,
                                   .retained_tokens = retained_tokens,
                                   .recent_context_omitted = omitted};
}

ava::core::Result<std::string> build_compaction_summary_prompt(std::vector<ava::session::SessionEntry> const& entries,
                                                               ava::session::CompactionConfig const& config, std::string_view instructions,
                                                               std::size_t estimated_tokens)
{
  auto tail_entries = physical_active_compaction_tail(entries);
  tail_entries.erase(std::remove_if(tail_entries.begin(), tail_entries.end(), ava::session::is_internal_replay_user_message), tail_entries.end());
  auto messages = ava::agent::build_provider_messages_from_entries(
      tail_entries, ava::agent::MessageBuildOptions{.replay_mode = ava::agent::HistoryReplayMode::ForcePortable});
  if (!messages)
    return std::unexpected(std::move(messages.error()));

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
  prompt += "- configured_threshold_tokens: " + std::to_string(config.auto_threshold_tokens) + "\n";
  prompt += "- configured_threshold_percent: " + std::to_string(config.auto_threshold_percent) + "\n";
  prompt += "- keep_recent_tokens: " + std::to_string(config.keep_recent_tokens) + "\n";
  prompt += "- keep_recent_turns: " + std::to_string(config.keep_recent_turns) + "\n";
  prompt += "- keep_recent_messages: " + std::to_string(config.keep_recent_messages) + "\n";
  prompt += "- summary_provider: " + config.provider_id + "\n";
  prompt += "- summary_model: " + config.model_id + "\n\n";
  prompt += "Portable conversation messages in chronological order:\n";
  for (std::size_t index = 0; index < messages->size(); ++index)
  {
    prompt += "\n## Message " + std::to_string(index + 1) + "\n";
    prompt += "role: " + messages->at(index).role + "\n";
    prompt += "content:\n";
    prompt += capped_entry_data(messages->at(index).content);
    prompt += "\n";
  }
  return prompt;
}

ava::core::Result<std::string> generate_compaction_summary(runtime::Session const& session, std::vector<ava::session::SessionEntry> const& entries,
                                                           ava::session::CompactionConfig const& config, std::string_view instructions,
                                                           std::size_t estimated_tokens, ava::provider::Provider const& provider,
                                                           ava::provider::Transport& transport, runtime::RunOptions const& options)
{
  if (session.is_offline() || options.offline)
  {
    return std::unexpected(offline_provider_error("compact"));
  }
  auto effective_config = resolve_compaction_config(session, config);
  if (!effective_config)
    return std::unexpected(std::move(effective_config.error()));
  ava::core::Result<ava::config::ModelInfo> summary_model =
      effective_config->model_explicit ? resolve_runtime_model(session.paths(), effective_config->provider_id, effective_config->model_id) : session.model;
  if (!summary_model)
    return std::unexpected(std::move(summary_model.error()));

  auto summary_options = options;
  std::unique_ptr<ava::provider::Provider> owned_provider;
  ava::provider::Provider const* summary_provider = &provider;
  if (effective_config->provider_id != session.model.provider_id)
  {
    summary_options.access_token.clear();
    summary_options.credential_type = "bearer";
    summary_options.openai_oauth = false;
    summary_options.openai_account_id.clear();
    ava::provider::CurlCliTransport auth_transport;
    auto prepared = prepare_runtime_credentials(session.paths(), effective_config->provider_id, std::move(summary_options), auth_transport, "compaction");
    if (!prepared)
      return std::unexpected(std::move(prepared.error()));
    summary_options = std::move(*prepared);
    auto created = ava::provider::builtin_provider_registry().create(effective_config->provider_id);
    if (!created)
      return std::unexpected(std::move(created.error()));
    owned_provider = std::move(*created);
    summary_provider = owned_provider.get();
  }
  if (summary_options.access_token.empty())
  {
    auto error = ava::core::Error(ava::core::ErrorCategory::PermissionDenied, "compaction requires provider access token");
    error.with_context("provider", effective_config->provider_id);
    return std::unexpected(std::move(error));
  }

  std::optional<ava::provider::RetryTransport> retry_transport;
  ava::provider::Transport* summary_transport = &transport;
  if (summary_options.enable_transport_retries)
  {
    retry_transport.emplace(transport, runtime::runtime_retry_options(session, summary_options));
    summary_transport = &*retry_transport;
    summary_options.enable_transport_retries = false;
  }
  // Compaction is a provider request in the same runtime turn, so observe its
  // logical request with the already-established run/turn context. Do not
  // construct the decorator on the disabled path.
  std::optional<ava::provider::ObservedTransport> observed_transport;
  if (summary_options.observation && summary_options.observation->enabled())
  {
    try
    {
      observed_transport.emplace(*summary_transport,
                                 ava::provider::TransportObservation{.observation = summary_options.observation, .context = summary_options.trace_context});
      summary_transport = &*observed_transport;
    }
    catch (...)
    {
      summary_options.observation->account_external_failure();
    }
  }

  constexpr std::string_view system_prompt =
      "You are AVA's deterministic session compaction summarizer. Create faithful continuation context from the "
      "provided session record. Return only the requested Markdown summary; do not include prefaces or code fences.";
  auto prompt = build_compaction_summary_prompt(entries, *effective_config, instructions, estimated_tokens);
  if (!prompt)
    return std::unexpected(std::move(prompt.error()));
  ava::provider::ProviderRequest const provider_request{.provider_id = effective_config->provider_id,
                                                        .model_id = effective_config->model_id,
                                                        .system_prompt = std::string(system_prompt),
                                                        .messages = {ava::provider::ChatMessage{.role = "user", .content = std::move(*prompt)}},
                                                        .tools_json = {},
                                                        .stream = summary_options.openai_oauth && summary_model->supports_streaming.value_or(true),
                                                        .max_output_tokens = summary_model->max_output_tokens};
  ava::provider::ProviderAuthContext const auth_context{
      .access_token = summary_options.access_token,
      .credential_type = summary_options.openai_oauth && summary_options.credential_type == "bearer" ? "oauth" : summary_options.credential_type,
      .account_id = summary_options.openai_account_id};
  auto request = summary_provider->build_request(provider_request, auth_context);
  if (!request)
    return std::unexpected(std::move(request.error()));

  auto response = summary_transport->send(*request, summary_options.cancel_requested);
  if (!response)
  {
    if (summary_options.cancel_requested && summary_options.cancel_requested())
    {
      return std::unexpected(agent_loop_canceled_error());
    }
    return std::unexpected(std::move(response.error()));
  }
  if (summary_options.cancel_requested && summary_options.cancel_requested())
  {
    return std::unexpected(agent_loop_canceled_error());
  }
  auto summary = parse_compaction_response_text(*summary_provider, *response, provider_request.stream);
  if (!summary)
    return std::unexpected(std::move(summary.error()));
  *summary = core::trim(*summary);
  if (summary->empty())
  {
    return std::unexpected(ava::core::Error(ava::core::ErrorCategory::Provider, "compaction summary generation returned an empty summary"));
  }
  if (summary->size() > config.max_summary_bytes)
  {
    auto error = ava::core::Error(ava::core::ErrorCategory::InvalidArgument, "generated compaction summary is too large");
    error.with_context("max_summary_bytes", std::to_string(config.max_summary_bytes));
    error.with_context("summary_bytes", std::to_string(summary->size()));
    return std::unexpected(std::move(error));
  }
  return *summary;
}

}  // namespace ava::app

namespace ava::app::runtime {

ava::core::Result<bool> compact_runtime_context(Session& session, ava::session::SessionReadAuthority read_authority, std::string_view trigger,
                                                ava::provider::Provider const& provider, ava::provider::Transport& transport, RunOptions const& options,
                                                std::vector<std::string> const& replayed_user_messages)
{
  if (options.access_token.empty())
  {
    return std::unexpected(ava::core::Error(ava::core::ErrorCategory::PermissionDenied, "compaction requires provider access token"));
  }

  auto loaded_config = ava::session::load_compaction_config(session.paths());
  if (!loaded_config)
    return std::unexpected(std::move(loaded_config.error()));
  auto config = resolve_compaction_config(session, std::move(*loaded_config));
  if (!config)
    return std::unexpected(std::move(config.error()));

  constexpr std::size_t max_compaction_attempts = 2;
  auto const trigger_text = std::string(trigger);
  std::size_t last_snapshot_entries = 0;
  std::size_t last_current_entries = 0;
  bool context_retry_event_emitted = false;
  for (std::size_t attempt = 0; attempt < max_compaction_attempts; ++attempt)
  {
    if (options.cancel_requested && options.cancel_requested())
    {
      return std::unexpected(agent_loop_canceled_error());
    }

    ava::core::Result<std::vector<ava::session::SessionEntry>> entries =
        std::unexpected(ava::core::Error(ava::core::ErrorCategory::Unknown, "session entries were not loaded"));
    if (options.session_mutex)
    {
      std::lock_guard lock(*options.session_mutex);
      entries = read_authority.load();
    }
    else
    {
      entries = read_authority.load();
    }
    if (!entries)
      return std::unexpected(std::move(entries.error()));

    auto prepared = prepare_compaction_context(*entries, *config, replayed_user_messages);
    if (!prepared)
      return std::unexpected(std::move(prepared.error()));
    auto const threshold = ava::session::effective_auto_threshold_tokens(*config, session.model.context_window_tokens);
    std::size_t estimated_tokens = prepared->estimated_tokens;
    std::size_t threshold_tokens = threshold;
    if (trigger == "auto")
    {
      auto decision = ava::session::should_auto_compact(*entries, *config, session.model.context_window_tokens);
      if (!decision)
        return std::unexpected(std::move(decision.error()));
      if (!decision->should_compact)
        return false;
      estimated_tokens = decision->estimated_tokens;
      threshold_tokens = decision->threshold_tokens;
    }

    if (trigger == "context_overflow" && !context_retry_event_emitted)
    {
      auto retry_event = base_compaction_event_locked(session, options, EventType::Retry);
      retry_event.trigger = trigger_text;
      retry_event.reason = "context_overflow";
      retry_event.status = "started";
      retry_event.attempt = 1;
      retry_event.max_attempts = 1;
      retry_event.estimated_tokens = estimated_tokens;
      retry_event.threshold_tokens = threshold_tokens;
      if (auto emitted = emit_compaction_event(session, options, std::move(retry_event)); !emitted)
      {
        return std::unexpected(std::move(emitted.error()));
      }
      context_retry_event_emitted = true;
    }

    auto start_event = base_compaction_event_locked(session, options, EventType::CompactionStart);
    start_event.provider_id = config->provider_id;
    start_event.model_id = config->model_id;
    start_event.trigger = trigger_text;
    start_event.reason = trigger == "auto" ? "automatic" : trigger == "context_overflow" ? "overflow" : "manual";
    start_event.status = "started";
    start_event.attempt = attempt + 1;
    start_event.max_attempts = max_compaction_attempts;
    start_event.estimated_tokens = estimated_tokens;
    start_event.threshold_tokens = threshold_tokens;
    start_event.retained_tokens = prepared->retained_tokens;
    if (auto emitted = emit_compaction_event(session, options, std::move(start_event)); !emitted)
    {
      return std::unexpected(std::move(emitted.error()));
    }

    auto summary = generate_compaction_summary(session, prepared->active_entries, *config, "", estimated_tokens, provider, transport, options);
    if (!summary)
      return std::unexpected(std::move(summary.error()));
    if (options.cancel_requested && options.cancel_requested())
    {
      return std::unexpected(agent_loop_canceled_error());
    }

    bool snapshot_stale = false;
    auto validate_and_append = [&]() -> ava::core::Result<bool> {
      auto current_entries = read_authority.load();
      if (!current_entries)
        return std::unexpected(std::move(current_entries.error()));
      if (options.cancel_requested && options.cancel_requested())
      {
        return std::unexpected(agent_loop_canceled_error());
      }
      if (!same_session_snapshot(*entries, *current_entries))
      {
        snapshot_stale = true;
        last_snapshot_entries = entries->size();
        last_current_entries = current_entries->size();
        return false;
      }
      auto entry =
          ava::session::make_manual_compaction_entry(ava::session::ManualCompactionRequest{.summary = *summary,
                                                                                           .instructions = "",
                                                                                           .config = *config,
                                                                                           .estimated_tokens = estimated_tokens,
                                                                                           .threshold_tokens = threshold_tokens,
                                                                                           .retained_tokens = prepared->retained_tokens,
                                                                                           .trigger = trigger_text,
                                                                                           .recent_context = prepared->recent_context,
                                                                                           .recent_context_omitted = prepared->recent_context_omitted});
      if (!entry)
        return std::unexpected(std::move(entry.error()));
      auto appended = options.active_append_route ? options.active_append_route(std::move(*entry)) : session.append_owned(std::move(*entry));
      if (!appended)
        return std::unexpected(std::move(appended.error()));
      return true;
    };
    ava::core::Result<bool> appended = false;
    if (options.session_mutex)
    {
      std::lock_guard lock(*options.session_mutex);
      appended = validate_and_append();
    }
    else
    {
      appended = validate_and_append();
    }
    if (!appended)
      return std::unexpected(std::move(appended.error()));
    if (*appended)
    {
      auto end_event = base_compaction_event_locked(session, options, EventType::CompactionEnd);
      end_event.provider_id = config->provider_id;
      end_event.model_id = config->model_id;
      end_event.trigger = trigger_text;
      end_event.reason = trigger == "auto" ? "automatic" : trigger == "context_overflow" ? "overflow" : "manual";
      end_event.status = "completed";
      end_event.attempt = attempt + 1;
      end_event.max_attempts = max_compaction_attempts;
      end_event.estimated_tokens = estimated_tokens;
      end_event.threshold_tokens = threshold_tokens;
      end_event.retained_tokens = prepared->retained_tokens;
      end_event.post_compaction_tokens = ava::session::estimate_tokens(*summary) + prepared->retained_tokens;
      end_event.summary_bytes = summary->size();
      if (auto emitted = emit_compaction_event(session, options, std::move(end_event)); !emitted)
      {
        return std::unexpected(std::move(emitted.error()));
      }
      return true;
    }
    if (snapshot_stale && attempt + 1 < max_compaction_attempts)
    {
      auto retry_event = base_compaction_event_locked(session, options, EventType::Retry);
      retry_event.trigger = trigger_text;
      retry_event.reason = "stale_compaction_snapshot";
      retry_event.status = "started";
      retry_event.attempt = attempt + 2;
      retry_event.max_attempts = max_compaction_attempts;
      retry_event.snapshot_entries = last_snapshot_entries;
      retry_event.current_entries = last_current_entries;
      if (auto emitted = emit_compaction_event(session, options, std::move(retry_event)); !emitted)
      {
        return std::unexpected(std::move(emitted.error()));
      }
    }
    if (!snapshot_stale)
      return false;
  }
  return std::unexpected(stale_compaction_snapshot_error(trigger_text, last_snapshot_entries, last_current_entries));
}

}  // namespace ava::app::runtime
