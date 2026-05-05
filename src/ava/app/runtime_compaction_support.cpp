#include "ava/app/runtime_compaction_support.h"

#include <algorithm>
#include <iterator>
#include <optional>
#include <utility>

#include "ava/agent/message_builder.h"
#include "ava/app/runtime.h"
#include "ava/app/runtime_json.h"
#include "ava/core/json.h"

namespace ava::app::detail {
namespace {

constexpr std::size_t kMaxCompactionPromptEntryBytes = 8192;

bool is_utf8_continuation_byte(char value)
{
  return (static_cast<unsigned char>(value) & 0xC0U) == 0x80U;
}

}  // namespace

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
  return capped_entry_data(entry.data_json);
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

}  // namespace ava::app::detail

namespace ava::app {

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
    prompt += detail::compaction_entry_data(entry);
    prompt += "\n";
  }
  return prompt;
}

}  // namespace ava::app
