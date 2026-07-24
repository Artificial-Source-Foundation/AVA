#include "sys.h"
#include "ava/tui/composer_editor.h"
#include "ava/tui/runtime_transcript_internal.h"
#include "ava/tui/text.h"
#include "ava/tui/tool_cards.h"

#include <algorithm>
#include <cerrno>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>
#include <unistd.h>

namespace ava::tui::runtime_transcript {
namespace {

constexpr std::string_view kCopyOptionPrefix = "copy:";
constexpr std::string_view kBase64Alphabet = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

std::string title_case_ascii(std::string_view text)
{
  std::string output;
  output.reserve(text.size());
  bool at_word_start = true;
  for (char ch : text)
  {
    auto const byte = static_cast<unsigned char>(ch);
    if (byte == '_' || byte == '-')
    {
      output.push_back(' ');
      at_word_start = true;
      continue;
    }
    if (at_word_start && byte >= 'a' && byte <= 'z')
    {
      output.push_back(static_cast<char>(byte - ('a' - 'A')));
    }
    else
    {
      output.push_back(ch);
    }
    at_word_start = byte == ' ';
  }
  return output;
}

std::string turn_duration_label(std::chrono::steady_clock::duration duration)
{
  auto const milliseconds = std::max<std::int64_t>(0, std::chrono::duration_cast<std::chrono::milliseconds>(duration).count());
  if (milliseconds < 1000)
    return std::to_string(milliseconds) + "ms";

  auto const seconds = milliseconds / 1000;
  if (seconds < 10)
  {
    auto const tenths = (milliseconds % 1000) / 100;
    return std::to_string(seconds) + "." + std::to_string(tenths) + "s";
  }
  if (seconds < 60)
    return std::to_string((milliseconds + 500) / 1000) + "s";

  auto const minutes = seconds / 60;
  auto const remaining_seconds = seconds % 60;
  return std::to_string(minutes) + "m " + std::to_string(remaining_seconds) + "s";
}

bool transcript_item_has_content(TranscriptItem const& item)
{
  return !item.text.empty() || !item.thinking.empty() || !text_empty(item.text_model) || !text_empty(item.thinking_model);
}

bool write_all_to_stdout(std::string_view text)
{
  std::size_t offset = 0;
  while (offset < text.size())
  {
    auto const written = ::write(STDOUT_FILENO, text.data() + offset, text.size() - offset);
    if (written < 0)
    {
      if (errno == EINTR)
        continue;
      return false;
    }
    if (written == 0)
      return false;
    offset += static_cast<std::size_t>(written);
  }
  return true;
}

std::string tool_card_identity_key(ToolTimelineItem const& item)
{
  if (!item.call_id.empty())
    return "call\n" + item.call_id;
  if (!item.request_id.empty())
    return "request\n" + item.request_id;
  if (!item.correlation_id.empty())
    return "correlation\n" + item.correlation_id;
  return "fallback\n" + item.name + "\n" + item.argument_summary;
}

std::size_t truncate_transcript(std::vector<TranscriptItem>& transcript)
{
  if (transcript.size() <= kMaxTranscriptItems)
    return 0;
  auto const removed = transcript.size() - kMaxTranscriptItems;
  transcript.erase(transcript.begin(), transcript.begin() + static_cast<std::ptrdiff_t>(removed));
  return removed;
}

}  // namespace

std::string assistant_meta_for_snapshot(ComposerSnapshot const& snapshot, std::optional<std::chrono::steady_clock::duration> elapsed)
{
  if (snapshot.model.empty())
    return {};
  auto mode = title_case_ascii(snapshot.mode);
  if (mode.empty())
    mode = "AVA";
  auto meta = mode + " · " + snapshot.model;
  if (elapsed)
    meta += " · " + turn_duration_label(*elapsed);
  return meta;
}

void apply_assistant_turn_meta(std::vector<TranscriptItem>& transcript, std::string const& meta)
{
  if (meta.empty())
    return;
  for (auto& item : transcript)
  {
    if (!item.tool && item.label == "ava" && transcript_item_has_content(item))
      item.meta = meta;
  }
}

std::string base64_encode(std::string_view text)
{
  std::string output;
  output.reserve(((text.size() + 2) / 3) * 4);
  for (std::size_t index = 0; index < text.size(); index += 3)
  {
    auto const first = static_cast<unsigned char>(text[index]);
    auto const second = index + 1 < text.size() ? static_cast<unsigned char>(text[index + 1]) : 0;
    auto const third = index + 2 < text.size() ? static_cast<unsigned char>(text[index + 2]) : 0;
    auto const block = (static_cast<unsigned int>(first) << 16U) | (static_cast<unsigned int>(second) << 8U) | static_cast<unsigned int>(third);
    output.push_back(kBase64Alphabet[(block >> 18U) & 0x3FU]);
    output.push_back(kBase64Alphabet[(block >> 12U) & 0x3FU]);
    output.push_back(index + 1 < text.size() ? kBase64Alphabet[(block >> 6U) & 0x3FU] : '=');
    output.push_back(index + 2 < text.size() ? kBase64Alphabet[block & 0x3FU] : '=');
  }
  return output;
}

bool copy_text_to_terminal_clipboard(std::string_view text)
{
  if (text.empty())
    return false;
  auto sequence = std::string("\x1b]52;c;") + base64_encode(text) + "\x1b\\";
  return write_all_to_stdout(sequence);
}

std::optional<std::string_view> copy_text_from_answer(ava::agent::QuestionAnswer const& answer)
{
  for (auto const& option : answer.selected_options)
  {
    if (option.starts_with(kCopyOptionPrefix))
      return std::string_view(option).substr(kCopyOptionPrefix.size());
  }
  return std::nullopt;
}

std::optional<std::string> latest_ava_message_copy_text(std::vector<TranscriptItem> const& transcript)
{
  for (auto item = transcript.rbegin(); item != transcript.rend(); ++item)
  {
    if (item->label != "ava" && item->label != "assistant")
      continue;
    if (item->text.empty())
      continue;
    return item->text;
  }
  return std::nullopt;
}

std::optional<std::string> latest_tool_copy_text(std::vector<TranscriptItem> const& transcript, std::string_view query)
{
  for (auto item = transcript.rbegin(); item != transcript.rend(); ++item)
  {
    if (!item->tool)
      continue;
    if (!detail::tool_card_matches_copy_query(*item->tool, query))
      continue;
    auto text = detail::tool_card_copy_text(*item->tool);
    if (!text.empty())
      return text;
  }
  return std::nullopt;
}

std::vector<std::pair<std::string, bool>> capture_tool_detail_visibility(std::vector<TranscriptItem> const& transcript)
{
  std::vector<std::pair<std::string, bool>> overrides;
  for (auto const& item : transcript)
  {
    if (item.tool && item.tool->details_visible)
      overrides.emplace_back(tool_card_identity_key(*item.tool), *item.tool->details_visible);
  }
  return overrides;
}

void carry_tool_detail_visibility(std::vector<std::pair<std::string, bool>> const& overrides, std::vector<TranscriptItem>& transcript)
{
  std::vector<bool> used(overrides.size(), false);
  for (std::size_t item_index = transcript.size(); item_index > 0; --item_index)
  {
    auto& item = transcript[item_index - 1];
    if (!item.tool)
      continue;
    auto const key = tool_card_identity_key(*item.tool);
    for (std::size_t override_index = overrides.size(); override_index > 0; --override_index)
    {
      auto const index = override_index - 1;
      if (used[index] || overrides[index].first != key)
        continue;
      item.tool->details_visible = overrides[index].second;
      used[index] = true;
      break;
    }
  }
}

std::optional<std::string> latest_tool_diff_copy_text(std::vector<TranscriptItem> const& transcript, std::string_view query)
{
  for (auto item = transcript.rbegin(); item != transcript.rend(); ++item)
  {
    if (!item->tool)
      continue;
    if (!detail::tool_card_matches_copy_query(*item->tool, query))
      continue;
    auto text = detail::tool_card_diff_copy_text(*item->tool);
    if (!text.empty())
      return text;
  }
  return std::nullopt;
}

std::string diff_transcript_text(std::string_view title, std::string_view diff)
{
  auto text = std::string(title);
  text += "\n\n```diff\n";
  text += diff;
  if (!diff.empty() && diff.back() != '\n')
    text += '\n';
  text += "```";
  return text;
}

std::optional<std::string> latest_permission_copy_text(std::vector<TranscriptItem> const& transcript, std::string_view query)
{
  for (auto item = transcript.rbegin(); item != transcript.rend(); ++item)
  {
    if (!item->tool)
      continue;
    auto text = detail::tool_card_permission_copy_text(*item->tool, query);
    if (!text.empty())
      return text;
  }
  return std::nullopt;
}

std::string question_answer_audit_detail(ava::agent::QuestionAnswer const& answer)
{
  auto detail = std::string("question answered");
  if (!answer.selected_options.empty())
  {
    auto const first = std::string_view(answer.selected_options.front());
    detail += first.starts_with(kCopyOptionPrefix) ? ": copy" : ": " + answer.selected_options.front();
  }
  if (!answer.custom_text.empty())
    detail += answer.selected_options.empty() ? ": custom" : ", custom";
  return detail;
}

void push_transcript(ComposerSnapshot& snapshot, TranscriptItem item)
{
  snapshot.transcript.push_back(std::move(item));
  truncate_transcript(snapshot.transcript);
  ++snapshot.transcript_generation;
}

void push_history(std::vector<std::string>& history, std::string input)
{
  static_cast<void>(push_composer_input_history(history, std::move(input)));
}

}  // namespace ava::tui::runtime_transcript

namespace ava::tui {

CappedTranscriptSnapshotUpdate apply_capped_transcript_snapshot(std::vector<TranscriptItem>& destination,
                                                                std::vector<TranscriptItem> const& submitted_transcript,
                                                                std::vector<TranscriptItem> const& turn_transcript, std::size_t previous_leading_evictions)
{
  destination = submitted_transcript;
  destination.insert(destination.end(), turn_transcript.begin(), turn_transcript.end());
  auto const leading_evictions = destination.size() > kMaxTranscriptItems ? destination.size() - kMaxTranscriptItems : std::size_t{0};
  if (leading_evictions > 0)
    destination.erase(destination.begin(), destination.begin() + static_cast<std::ptrdiff_t>(leading_evictions));
  auto const item_index_shift = static_cast<std::ptrdiff_t>(previous_leading_evictions) - static_cast<std::ptrdiff_t>(leading_evictions);
  return CappedTranscriptSnapshotUpdate{.leading_evictions = leading_evictions, .item_index_shift = item_index_shift};
}

std::optional<std::size_t> toggle_latest_matching_tool_details(std::vector<TranscriptItem>& transcript, std::string_view query, bool global_details_visible)
{
  for (std::size_t index = transcript.size(); index > 0; --index)
  {
    auto& item = transcript[index - 1];
    if (!item.tool || !detail::tool_card_matches_copy_query(*item.tool, query))
      continue;
    item.tool->details_visible = !detail::tool_card_details_visible(*item.tool, global_details_visible);
    return index - 1;
  }
  return std::nullopt;
}

}  // namespace ava::tui
