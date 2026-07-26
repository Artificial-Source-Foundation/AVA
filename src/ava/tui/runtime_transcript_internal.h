#pragma once

#include "ava/agent/question.h"
#include "ava/tui/composer.h"

#include <chrono>
#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>
#include "debug.h"

namespace ava::tui {

inline constexpr std::size_t kMaxTranscriptItems = 1000;

struct CappedTranscriptSnapshotUpdate
{
  std::size_t leading_evictions = 0;
  std::ptrdiff_t item_index_shift = 0;

  AVA_DEBUG_PRINT_MEMBERS_ON
};

namespace runtime_transcript {

[[nodiscard]] std::string assistant_meta_for_snapshot(ComposerSnapshot const& snapshot,
                                                      std::optional<std::chrono::steady_clock::duration> elapsed = std::nullopt);
void apply_assistant_turn_meta(std::vector<TranscriptItem>& transcript, std::string const& meta, bool thinking_visible = true);
void push_fallback_assistant_outputs(ComposerSnapshot& snapshot, std::vector<std::string> const& outputs, std::string const& meta);
[[nodiscard]] std::string base64_encode(std::string_view text);
[[nodiscard]] bool copy_text_to_terminal_clipboard(std::string_view text);
[[nodiscard]] std::optional<std::string_view> copy_text_from_answer(ava::agent::QuestionAnswer const& answer);
[[nodiscard]] std::optional<std::string> latest_ava_message_copy_text(std::vector<TranscriptItem> const& transcript);
[[nodiscard]] std::optional<std::string> latest_tool_copy_text(std::vector<TranscriptItem> const& transcript, std::string_view query = {});
[[nodiscard]] std::vector<std::pair<std::string, bool>> capture_tool_detail_visibility(std::vector<TranscriptItem> const& transcript);
void carry_tool_detail_visibility(std::vector<std::pair<std::string, bool>> const& overrides, std::vector<TranscriptItem>& transcript);
[[nodiscard]] std::optional<std::string> latest_tool_diff_copy_text(std::vector<TranscriptItem> const& transcript, std::string_view query = {});
[[nodiscard]] std::string diff_transcript_text(std::string_view title, std::string_view diff);
[[nodiscard]] std::optional<std::string> latest_permission_copy_text(std::vector<TranscriptItem> const& transcript, std::string_view query = {});
[[nodiscard]] std::string question_answer_audit_detail(ava::agent::QuestionAnswer const& answer);
void push_transcript(ComposerSnapshot& snapshot, TranscriptItem item);
void push_history(std::vector<std::string>& history, std::string input);

}  // namespace runtime_transcript

[[nodiscard]] CappedTranscriptSnapshotUpdate apply_capped_transcript_snapshot(std::vector<TranscriptItem>& destination,
                                                                              std::vector<TranscriptItem> const& submitted_transcript,
                                                                              std::vector<TranscriptItem> turn_transcript,
                                                                              std::size_t previous_leading_evictions);

// Toggles only the latest original card matching the established tool query.
// The returned transcript index lets the runtime preserve viewport anchoring.
[[nodiscard]] std::optional<std::size_t> toggle_latest_matching_tool_details(std::vector<TranscriptItem>& transcript, std::string_view query,
                                                                             ToolPresentation inherited);
inline std::optional<std::size_t> toggle_latest_matching_tool_details(std::vector<TranscriptItem>& transcript, std::string_view query, bool expanded)
{
  return toggle_latest_matching_tool_details(transcript, query, expanded ? ToolPresentation::Expanded : ToolPresentation::Compact);
}

}  // namespace ava::tui
