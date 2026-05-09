#include "ava/session/compaction.h"
#include "ava/core/ids.h"
#include "ava/core/json.h"

#include <algorithm>
#include <array>
#include <filesystem>
#include <fstream>
#include <utility>

namespace ava::session {
namespace {

constexpr std::size_t max_compaction_config_bytes = 64 * 1024;
constexpr std::size_t fallback_auto_threshold_tokens = 80'000;
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

ava::core::VoidResult assign_size_field(std::string_view content, std::string_view field, std::size_t& target)
{
  auto const value = ava::core::json::integer_field(content, field);
  if (!value)
    return {};
  if (*value < 0)
  {
    auto error = ava::core::Error(ava::core::ErrorCategory::InvalidArgument, "compaction config field must not be negative");
    error.with_context("field", std::string(field));
    return std::unexpected(std::move(error));
  }
  target = static_cast<std::size_t>(*value);
  return {};
}

std::string compaction_data_json(ManualCompactionRequest const& request, std::string_view summary, bool summary_unavailable)
{
  auto const trigger = request.trigger.empty() ? std::string("manual") : request.trigger;
  auto const threshold = request.threshold_tokens > 0 ? request.threshold_tokens : request.config.auto_threshold_tokens;
  return "{\"trigger\":\"" + ava::core::json::escape(trigger) +
         "\",\"status\":\"recorded\",\"summary_unavailable\":" + (summary_unavailable ? std::string("true") : std::string("false")) + ",\"summary\":\"" +
         ava::core::json::escape(summary) + "\",\"instructions\":\"" + ava::core::json::escape(request.instructions) + "\",\"model\":\"" +
         ava::core::json::escape(request.config.model_id) + "\",\"threshold_tokens\":" + std::to_string(threshold) +
         ",\"estimated_tokens\":" + std::to_string(request.estimated_tokens) + ",\"keep_recent_tokens\":" + std::to_string(request.config.keep_recent_tokens) +
         ",\"keep_recent_messages\":" + std::to_string(request.config.keep_recent_messages) +
         ",\"max_summary_bytes\":" + std::to_string(request.config.max_summary_bytes) + ",\"recent_context\":\"" +
         ava::core::json::escape(request.recent_context) + "\"}";
}

}  // namespace

CompactionConfig default_compaction_config()
{
  return CompactionConfig{};
}

ava::core::Result<CompactionConfig> parse_compaction_config(std::string_view content)
{
  auto config = default_compaction_config();
  if (auto model = ava::core::json::string_field(content, "model"))
    config.model_id = *model;
  if (auto model = ava::core::json::string_field(content, "compaction_model"))
    config.model_id = *model;

  if (ava::core::json::field_value_start(content, "auto_threshold_tokens"))
  {
    config.auto_threshold_tokens_explicit = true;
  }
  if (auto assigned = assign_size_field(content, "auto_threshold_tokens", config.auto_threshold_tokens); !assigned)
  {
    return std::unexpected(std::move(assigned.error()));
  }
  if (auto assigned = assign_size_field(content, "keep_recent_tokens", config.keep_recent_tokens); !assigned)
  {
    return std::unexpected(std::move(assigned.error()));
  }
  if (auto assigned = assign_size_field(content, "keep_recent_messages", config.keep_recent_messages); !assigned)
  {
    return std::unexpected(std::move(assigned.error()));
  }
  if (auto assigned = assign_size_field(content, "max_summary_bytes", config.max_summary_bytes); !assigned)
  {
    return std::unexpected(std::move(assigned.error()));
  }

  if (config.model_id.empty())
  {
    auto error = ava::core::Error(ava::core::ErrorCategory::InvalidArgument, "compaction model must not be empty");
    return std::unexpected(std::move(error));
  }
  if (config.max_summary_bytes == 0)
  {
    auto error = ava::core::Error(ava::core::ErrorCategory::InvalidArgument, "compaction max summary bytes must be greater than zero");
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
  return parse_compaction_config(*content);
}

std::size_t estimate_tokens(std::string_view text) noexcept
{
  if (text.empty())
    return 0;
  return (text.size() + 3) / 4;
}

std::size_t estimate_session_tokens(std::vector<SessionEntry> const& entries) noexcept
{
  std::size_t tokens = 0;
  for (auto const& entry : entries)
  {
    if (entry.type == EntryType::UserMessage || entry.type == EntryType::AssistantMessage || entry.type == EntryType::ReasoningBlock ||
        entry.type == EntryType::ToolCall || entry.type == EntryType::ToolResult)
    {
      tokens += estimate_tokens(entry.data_json);
    }
  }
  return tokens;
}

std::size_t effective_auto_threshold_tokens(CompactionConfig const& config, std::optional<long long> context_window_tokens) noexcept
{
  if (config.auto_threshold_tokens > 0)
    return config.auto_threshold_tokens;
  if (config.auto_threshold_tokens_explicit)
    return 0;
  if (!context_window_tokens || *context_window_tokens <= 0)
    return fallback_auto_threshold_tokens;
  auto const window = static_cast<std::size_t>(*context_window_tokens);
  return std::max<std::size_t>(1, (window * 4) / 5);
}

std::size_t estimate_active_context_tokens(std::vector<SessionEntry> const& entries) noexcept
{
  std::size_t tokens = 0;
  for (auto const& entry : entries)
  {
    if (entry.type == EntryType::Compaction)
    {
      tokens = estimate_tokens(entry.data_json);
    }
    else if (entry.type == EntryType::UserMessage || entry.type == EntryType::AssistantMessage || entry.type == EntryType::ReasoningBlock ||
             entry.type == EntryType::ToolCall || entry.type == EntryType::ToolResult)
    {
      tokens += estimate_tokens(entry.data_json);
    }
  }
  return tokens;
}

CompactionDecision should_auto_compact(std::vector<SessionEntry> const& entries, CompactionConfig const& config) noexcept
{
  return should_auto_compact(entries, config, std::nullopt);
}

CompactionDecision should_auto_compact(std::vector<SessionEntry> const& entries, CompactionConfig const& config,
                                       std::optional<long long> context_window_tokens) noexcept
{
  auto const estimated = estimate_active_context_tokens(entries);
  auto const threshold = effective_auto_threshold_tokens(config, context_window_tokens);
  return CompactionDecision{.should_compact = threshold > 0 && estimated >= threshold, .estimated_tokens = estimated, .threshold_tokens = threshold};
}

ava::core::VoidResult append_manual_compaction(SessionStore& store, ManualCompactionRequest request)
{
  bool const summary_unavailable = request.summary.empty();
  std::string const summary = summary_unavailable ? std::string(unavailable_summary) : request.summary;
  if (!summary_unavailable && summary.size() > request.config.max_summary_bytes)
  {
    auto error = ava::core::Error(ava::core::ErrorCategory::InvalidArgument, "compaction summary is too large");
    error.with_context("max_summary_bytes", std::to_string(request.config.max_summary_bytes));
    return std::unexpected(std::move(error));
  }

  return store.append(SessionEntry{.id = ava::core::make_id("entry"),
                                   .parent_id = "",
                                   .type = EntryType::Compaction,
                                   .timestamp = now_timestamp(),
                                   .data_json = compaction_data_json(request, summary, summary_unavailable)});
}

}  // namespace ava::session
