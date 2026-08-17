#include "sys.h"
#include "ava/session/compaction.h"
#include "ava/session/logical_projection.h"
#include "ava/core/ids.h"
#include "ava/core/json.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <charconv>
#include <filesystem>
#include <fstream>
#include <limits>
#include <utility>

namespace ava::session {
namespace {

constexpr std::size_t max_compaction_config_bytes = 64 * 1024;
constexpr std::size_t fallback_context_window_tokens = 100'000;
constexpr std::size_t max_summary_bytes_limit = 1024 * 1024;
constexpr std::string_view unavailable_summary = "Prior context was compacted manually; provider-generated summary unavailable.";

ava::core::Result<std::string> read_text(std::filesystem::path const& path)
{
  std::error_code status_error;
  if (!std::filesystem::is_regular_file(path, status_error))
  {
    auto error = ava::core::Error(ava::core::ErrorCategory::Io, "compaction config is not a regular file");
    error.with_context("path", path.string());
    if (status_error)
      error.with_context("cause", status_error.message());
    return std::unexpected(std::move(error));
  }

  std::error_code size_error;
  auto const size = std::filesystem::file_size(path, size_error);
  if (size_error || size > max_compaction_config_bytes)
  {
    auto error = ava::core::Error(ava::core::ErrorCategory::Io, "compaction config is too large");
    error.with_context("path", path.string());
    error.with_context("max_bytes", std::to_string(max_compaction_config_bytes));
    if (size_error)
      error.with_context("cause", size_error.message());
    return std::unexpected(std::move(error));
  }

  std::ifstream file(path, std::ios::binary);
  if (!file)
  {
    auto error = ava::core::Error(ava::core::ErrorCategory::Io, "failed to open compaction config");
    error.with_context("path", path.string());
    return std::unexpected(std::move(error));
  }

  std::string content;
  std::array<char, 4096> buffer{};
  while (file)
  {
    file.read(buffer.data(), static_cast<std::streamsize>(buffer.size()));
    if (file.gcount() > 0)
      content.append(buffer.data(), static_cast<std::size_t>(file.gcount()));
    if (content.size() > max_compaction_config_bytes)
    {
      auto error = ava::core::Error(ava::core::ErrorCategory::Io, "compaction config is too large");
      error.with_context("path", path.string());
      error.with_context("max_bytes", std::to_string(max_compaction_config_bytes));
      return std::unexpected(std::move(error));
    }
  }
  if (!file.eof() && file.fail())
  {
    auto error = ava::core::Error(ava::core::ErrorCategory::Io, "failed while reading compaction config");
    error.with_context("path", path.string());
    return std::unexpected(std::move(error));
  }
  return content;
}

ava::core::Error field_error(std::string_view field, std::string message)
{
  auto error = ava::core::Error(ava::core::ErrorCategory::InvalidArgument, std::move(message));
  error.with_context("field", std::string(field));
  return error;
}

ava::core::VoidResult assign_size_field(std::string_view content, std::string_view field, std::size_t& target, bool& present)
{
  auto const value_start = ava::core::json::field_value_start(content, field);
  present = value_start.has_value();
  if (!present)
    return {};

  auto end = *value_start;
  bool const negative = end < content.size() && content[end] == '-';
  if (negative)
    ++end;
  auto const digits_start = end;
  while (end < content.size() && std::isdigit(static_cast<unsigned char>(content[end])) != 0) ++end;
  if (end == digits_start)
    return std::unexpected(field_error(field, "compaction config field must be an integer"));

  auto terminator = end;
  while (terminator < content.size() && std::isspace(static_cast<unsigned char>(content[terminator])) != 0) ++terminator;
  if (terminator >= content.size() || (content[terminator] != ',' && content[terminator] != '}'))
    return std::unexpected(field_error(field, "compaction config field must be an integer"));
  if (negative)
    return std::unexpected(field_error(field, "compaction config field must not be negative or out of range"));

  std::size_t value = 0;
  auto const parsed = std::from_chars(content.data() + static_cast<std::ptrdiff_t>(digits_start), content.data() + static_cast<std::ptrdiff_t>(end), value);
  if (parsed.ec != std::errc{} || parsed.ptr != content.data() + static_cast<std::ptrdiff_t>(end))
    return std::unexpected(field_error(field, "compaction config field must not be negative or out of range"));
  target = value;
  return {};
}

ava::core::VoidResult assign_string_field(std::string_view content, std::string_view field, std::string& target, bool& present)
{
  present = ava::core::json::field_value_start(content, field).has_value();
  if (!present)
    return {};
  auto value = ava::core::json::string_field(content, field);
  if (!value)
    return std::unexpected(field_error(field, "compaction config field must be a string"));
  target = std::move(*value);
  return {};
}

std::string normalized_reason(std::string_view trigger)
{
  if (trigger == "auto" || trigger == "automatic")
    return "automatic";
  if (trigger == "context_overflow" || trigger == "overflow")
    return "overflow";
  return "manual";
}

std::string compaction_data_json(ManualCompactionRequest const& request, std::string_view summary, bool summary_unavailable)
{
  auto const trigger = request.trigger.empty() ? std::string("manual") : request.trigger;
  auto const threshold = request.threshold_tokens > 0 ? request.threshold_tokens : request.config.auto_threshold_tokens;
  auto const post_tokens = estimate_tokens(summary) + estimate_tokens(request.instructions) + request.retained_tokens;
  std::string data = "{\"trigger\":\"" + ava::core::json::escape(trigger) + "\",\"reason\":\"" + normalized_reason(trigger) +
                     "\",\"status\":\"recorded\",\"summary_unavailable\":" + (summary_unavailable ? "true" : "false") + ",\"summary\":\"" +
                     ava::core::json::escape(summary) + "\",\"instructions\":\"" + ava::core::json::escape(request.instructions) + "\"";
  if (!request.config.provider_id.empty())
    data += ",\"provider\":\"" + ava::core::json::escape(request.config.provider_id) + "\"";
  data += ",\"model\":\"" + ava::core::json::escape(request.config.model_id) + "\",\"threshold_type\":\"" +
          (request.config.auto_threshold_tokens_explicit ? std::string("tokens") : std::string("percent")) +
          "\",\"threshold_tokens\":" + std::to_string(threshold) + ",\"configured_threshold_tokens\":" + std::to_string(request.config.auto_threshold_tokens) +
          ",\"configured_threshold_percent\":" + std::to_string(request.config.auto_threshold_percent) +
          ",\"estimated_tokens\":" + std::to_string(request.estimated_tokens) +
          ",\"active_pre_compaction_tokens\":" + std::to_string(request.estimated_tokens) + ",\"retained_tokens\":" + std::to_string(request.retained_tokens) +
          ",\"post_compaction_estimated_tokens\":" + std::to_string(post_tokens) +
          ",\"keep_recent_tokens\":" + std::to_string(request.config.keep_recent_tokens) +
          ",\"keep_recent_turns\":" + std::to_string(request.config.keep_recent_turns) +
          ",\"keep_recent_messages\":" + std::to_string(request.config.keep_recent_messages) +
          ",\"max_summary_bytes\":" + std::to_string(request.config.max_summary_bytes) + ",\"history_projection\":\"portable-v1\"" +
          ",\"recent_context_omitted\":" + (request.recent_context_omitted ? "true" : "false") + ",\"recent_context\":\"" +
          ava::core::json::escape(request.recent_context) + "\"";
  if (normalized_reason(trigger) == "overflow")
    data += ",\"overflow_retry_outcome\":\"scheduled\"";
  data += '}';
  return data;
}

bool valid_compaction_boundary(SessionEntry const& entry)
{
  return entry.type == EntryType::Compaction && ava::core::json::is_valid_object(entry.data_json) &&
         ava::core::json::string_field(entry.data_json, "summary").value_or("").size() > 0;
}

bool token_bearing_entry(SessionEntry const& entry)
{
  return entry.type == EntryType::UserMessage || entry.type == EntryType::AssistantMessage || entry.type == EntryType::ReasoningBlock ||
         entry.type == EntryType::ToolCall || entry.type == EntryType::ToolResult || entry.type == EntryType::Compaction;
}

}  // namespace

CompactionConfig default_compaction_config()
{
  return CompactionConfig{};
}

ava::core::Result<CompactionConfig> parse_compaction_config(std::string_view content)
{
  if (!ava::core::json::is_valid_object(content))
    return std::unexpected(ava::core::Error(ava::core::ErrorCategory::InvalidArgument, "compaction config must be a valid JSON object"));

  auto config = default_compaction_config();
  bool legacy_model_present = false;
  std::string legacy_model;
  if (auto assigned = assign_string_field(content, "provider", config.provider_id, config.provider_explicit); !assigned)
    return std::unexpected(std::move(assigned.error()));
  if (auto assigned = assign_string_field(content, "model", config.model_id, config.model_explicit); !assigned)
    return std::unexpected(std::move(assigned.error()));
  if (auto assigned = assign_string_field(content, "compaction_model", legacy_model, legacy_model_present); !assigned)
    return std::unexpected(std::move(assigned.error()));
  if (legacy_model_present)
  {
    if (config.model_explicit)
      return std::unexpected(field_error("compaction_model", "compaction config cannot specify both model and compaction_model"));
    config.model_id = std::move(legacy_model);
    config.model_explicit = true;
  }

  if (auto assigned = assign_size_field(content, "auto_threshold_tokens", config.auto_threshold_tokens, config.auto_threshold_tokens_explicit); !assigned)
    return std::unexpected(std::move(assigned.error()));
  if (auto assigned = assign_size_field(content, "auto_threshold_percent", config.auto_threshold_percent, config.auto_threshold_percent_explicit); !assigned)
    return std::unexpected(std::move(assigned.error()));
  bool ignored_presence = false;
  if (auto assigned = assign_size_field(content, "keep_recent_tokens", config.keep_recent_tokens, ignored_presence); !assigned)
    return std::unexpected(std::move(assigned.error()));
  if (auto assigned = assign_size_field(content, "keep_recent_turns", config.keep_recent_turns, config.keep_recent_turns_explicit); !assigned)
    return std::unexpected(std::move(assigned.error()));
  if (auto assigned = assign_size_field(content, "keep_recent_messages", config.keep_recent_messages, config.keep_recent_messages_explicit); !assigned)
    return std::unexpected(std::move(assigned.error()));
  if (auto assigned = assign_size_field(content, "max_summary_bytes", config.max_summary_bytes, ignored_presence); !assigned)
    return std::unexpected(std::move(assigned.error()));

  if (config.provider_explicit && !config.model_explicit)
    return std::unexpected(field_error("provider", "compaction provider requires an explicit model"));
  if (config.provider_explicit && config.provider_id.empty())
    return std::unexpected(field_error("provider", "compaction provider must not be empty"));
  if (config.model_explicit && config.model_id.empty())
    return std::unexpected(field_error("model", "compaction model must not be empty"));
  if (config.auto_threshold_tokens_explicit && config.auto_threshold_percent_explicit)
    return std::unexpected(field_error("auto_threshold_percent", "compaction config cannot specify both auto_threshold_tokens and auto_threshold_percent"));
  if (config.auto_threshold_percent < 1 || config.auto_threshold_percent > 95)
    return std::unexpected(field_error("auto_threshold_percent", "compaction auto threshold percent must be from 1 through 95"));
  if (config.keep_recent_turns_explicit && config.keep_recent_messages_explicit)
    return std::unexpected(field_error("keep_recent_messages", "compaction config cannot specify both keep_recent_turns and keep_recent_messages"));
  if (config.keep_recent_tokens > std::numeric_limits<std::size_t>::max() / 4)
    return std::unexpected(field_error("keep_recent_tokens", "compaction recent-token budget is out of range"));
  if (config.max_summary_bytes == 0)
    return std::unexpected(field_error("max_summary_bytes", "compaction max summary bytes must be greater than zero"));
  if (config.max_summary_bytes > max_summary_bytes_limit)
  {
    auto error = field_error("max_summary_bytes", "compaction max summary bytes must not exceed " + std::to_string(max_summary_bytes_limit));
    return std::unexpected(std::move(error));
  }
  return config;
}

ava::core::Result<CompactionConfig> load_compaction_config(ava::config::XdgPaths const& paths)
{
  if (!std::filesystem::exists(paths.compaction_file))
    return default_compaction_config();
  auto content = read_text(paths.compaction_file);
  if (!content)
    return std::unexpected(std::move(content.error()));
  auto config = parse_compaction_config(*content);
  if (!config)
    config.error().with_context("path", paths.compaction_file.string());
  return config;
}

std::size_t estimate_tokens(std::string_view text) noexcept
{
  if (text.empty())
    return 0;
  return (text.size() + 3) / 4;
}

ava::core::Result<std::size_t> estimate_session_tokens(std::vector<SessionEntry> const& entries)
{
  auto projected = project_ordered_public_session_history(entries);
  if (!projected)
    return std::unexpected(std::move(projected.error()));

  std::size_t tokens = 0;
  for (auto const& entry : *projected)
    if (token_bearing_entry(entry) && entry.type != EntryType::Compaction)
      tokens += estimate_tokens(entry.data_json);
  return tokens;
}

ava::core::Result<std::vector<SessionEntry>> project_active_compaction_context(std::vector<SessionEntry> const& entries)
{
  auto projected = project_ordered_public_session_history(entries);
  if (!projected)
    return std::unexpected(std::move(projected.error()));
  auto boundary = std::ranges::find_if(projected->rbegin(), projected->rend(), valid_compaction_boundary);
  if (boundary == projected->rend())
    return projected;
  auto const start = static_cast<std::size_t>(std::distance(projected->begin(), boundary.base()) - 1);
  return std::vector<SessionEntry>(projected->begin() + static_cast<std::ptrdiff_t>(start), projected->end());
}

ava::core::Result<std::size_t> estimate_active_context_tokens(std::vector<SessionEntry> const& entries)
{
  auto active = project_active_compaction_context(entries);
  if (!active)
    return std::unexpected(std::move(active.error()));
  std::size_t tokens = 0;
  for (auto const& entry : *active)
    if (token_bearing_entry(entry))
      tokens += estimate_tokens(entry.data_json);
  return tokens;
}

bool compaction_snapshot_matches(std::vector<SessionEntry> const& expected, std::vector<SessionEntry> const& actual)
{
  if (actual.size() < expected.size())
    return false;
  for (std::size_t index = 0; index < expected.size(); ++index)
  {
    auto const& expected_entry = expected[index];
    auto const& actual_entry = actual[index];
    if (expected_entry.id != actual_entry.id || expected_entry.parent_id != actual_entry.parent_id || expected_entry.type != actual_entry.type ||
        expected_entry.timestamp != actual_entry.timestamp || expected_entry.data_json != actual_entry.data_json ||
        expected_entry.version != actual_entry.version)
    {
      return false;
    }
  }

  // Automatic title metadata is context-neutral. Allow only the currently
  // validated trailing form to finish while provider generation is in flight.
  for (std::size_t index = expected.size(); index < actual.size(); ++index)
  {
    auto const& entry = actual[index];
    if (entry.type != EntryType::SessionMetadata || ava::core::json::string_field(entry.data_json, "actor").value_or("") != "auto-title" ||
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

std::size_t effective_auto_threshold_tokens(CompactionConfig const& config, std::optional<long long> context_window_tokens) noexcept
{
  if (config.auto_threshold_tokens > 0)
    return config.auto_threshold_tokens;
  if (config.auto_threshold_tokens_explicit)
    return 0;
  auto const window = !context_window_tokens || *context_window_tokens <= 0 ? fallback_context_window_tokens : static_cast<std::size_t>(*context_window_tokens);
  auto const whole = (window / 100) * config.auto_threshold_percent;
  auto const remainder = ((window % 100) * config.auto_threshold_percent) / 100;
  return std::max<std::size_t>(1, whole + remainder);
}

ava::core::Result<CompactionDecision> should_auto_compact(std::vector<SessionEntry> const& entries, CompactionConfig const& config)
{
  return should_auto_compact(entries, config, std::nullopt);
}

ava::core::Result<CompactionDecision> should_auto_compact(std::vector<SessionEntry> const& entries, CompactionConfig const& config,
                                                          std::optional<long long> context_window_tokens)
{
  auto estimated = estimate_active_context_tokens(entries);
  if (!estimated)
    return std::unexpected(std::move(estimated.error()));
  auto const threshold = effective_auto_threshold_tokens(config, context_window_tokens);
  return CompactionDecision{.should_compact = threshold > 0 && *estimated >= threshold, .estimated_tokens = *estimated, .threshold_tokens = threshold};
}

ava::core::Result<SessionEntry> make_manual_compaction_entry(ManualCompactionRequest request)
{
  bool const summary_unavailable = request.summary.empty();
  std::string const summary = summary_unavailable ? std::string(unavailable_summary) : request.summary;
  if (!summary_unavailable && summary.size() > request.config.max_summary_bytes)
  {
    auto error = ava::core::Error(ava::core::ErrorCategory::InvalidArgument, "compaction summary is too large");
    error.with_context("max_summary_bytes", std::to_string(request.config.max_summary_bytes));
    return std::unexpected(std::move(error));
  }
  return SessionEntry{.id = ava::core::make_id("entry"),
                      .parent_id = "",
                      .type = EntryType::Compaction,
                      .timestamp = now_timestamp(),
                      .data_json = compaction_data_json(request, summary, summary_unavailable)};
}

ava::core::VoidResult append_manual_compaction(SessionStore& store, SessionLease const& lease, ManualCompactionRequest request)
{
  auto entry = make_manual_compaction_entry(std::move(request));
  if (!entry)
    return std::unexpected(std::move(entry.error()));
  return store.append(lease, std::move(*entry));
}

ava::core::VoidResult append_manual_compaction_ephemeral(SessionStore& store, ManualCompactionRequest request)
{
  auto entry = make_manual_compaction_entry(std::move(request));
  if (!entry)
    return std::unexpected(std::move(entry.error()));
  return store.append_ephemeral(std::move(*entry));
}

}  // namespace ava::session
