#include "sys.h"
#include "ava/session/logical_projection.h"
#include "ava/session/transcript.h"
#include "ava/session/validation.h"
#include "ava/core/json.h"

#include <algorithm>
#include <utility>

namespace ava::session {

namespace {

ava::core::Result<bool> append_transcript_item(std::vector<TranscriptItem>& transcript, std::size_t& text_bytes, SessionEntry const& entry,
                                               TranscriptLimits const& limits)
{
  if (is_internal_replay_user_message(entry) || (entry.type != EntryType::UserMessage && entry.type != EntryType::AssistantMessage))
    return true;
  auto const data = sanitized_message_data_json(entry.data_json, entry.type == EntryType::UserMessage);
  auto text = ava::core::json::string_field(data, "text");
  if (!text || text->empty())
    return true;
  if (text->size() > limits.max_item_text_bytes || transcript.size() >= limits.max_items || text->size() > limits.max_text_bytes - text_bytes)
  {
    auto error = ava::core::Error(ava::core::ErrorCategory::InvalidArgument, "session transcript exceeds replay limits");
    error.with_context("max_items", std::to_string(limits.max_items));
    error.with_context("max_text_bytes", std::to_string(limits.max_text_bytes));
    error.with_context("max_item_text_bytes", std::to_string(limits.max_item_text_bytes));
    return std::unexpected(std::move(error));
  }
  text_bytes += text->size();
  transcript.push_back(TranscriptItem{.role = entry.type == EntryType::UserMessage ? TranscriptRole::User : TranscriptRole::Assistant,
                                      .message_id = entry.id,
                                      .timestamp = entry.timestamp,
                                      .text = std::move(*text)});
  return true;
}

}  // namespace

ava::core::Result<std::vector<TranscriptItem>> project_transcript(std::vector<SessionEntry> const& entries, TranscriptLimits limits)
{
  if (limits.max_items == 0 || limits.max_text_bytes == 0 || limits.max_item_text_bytes == 0)
    return std::unexpected(ava::core::Error(ava::core::ErrorCategory::InvalidArgument, "transcript projection limits must be non-zero"));

  auto projected = project_ordered_public_session_history(entries);
  if (!projected)
    return std::unexpected(std::move(projected.error()));

  std::vector<TranscriptItem> transcript;
  std::size_t text_bytes = 0;
  for (auto const& entry : *projected)
  {
    auto appended = append_transcript_item(transcript, text_bytes, entry, limits);
    if (!appended)
      return std::unexpected(std::move(appended.error()));
  }
  return transcript;
}

ava::core::Result<std::vector<TranscriptItem>> project_transcript_bounded(SessionStore const& store, SessionReadLimits read_limits, TranscriptLimits limits,
                                                                          SessionCancelCallback cancel_requested)
{
  if (limits.max_items == 0 || limits.max_text_bytes == 0 || limits.max_item_text_bytes == 0)
    return std::unexpected(ava::core::Error(ava::core::ErrorCategory::InvalidArgument, "transcript projection limits must be non-zero"));
  // The compatibility overload first obtains one bounded snapshot so the
  // shared v4 classifier can reject malformed interleaving atomically.
  auto entries = store.load_bounded(read_limits, std::move(cancel_requested));
  if (!entries)
    return std::unexpected(std::move(entries.error()));
  return project_transcript(*entries, limits);
}

}  // namespace ava::session
