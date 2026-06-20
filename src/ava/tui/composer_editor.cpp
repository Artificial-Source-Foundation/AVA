#include "ava/tui/composer_editor.h"
#include "ava/tui/composer_internal.h"

#include <algorithm>
#include <cctype>
#include <optional>
#include <utility>

namespace ava::tui {
namespace {

constexpr std::size_t kMaxUndoItems = 100;
constexpr std::size_t kMaxKillRingItems = 16;
constexpr std::size_t kMaxInputHistoryItems = 100;
constexpr std::size_t kPasteMarkerLineThreshold = 10;
constexpr std::size_t kPasteMarkerByteThreshold = 2000;

std::string trim_ascii_copy(std::string_view text)
{
  while (!text.empty() && std::isspace(static_cast<unsigned char>(text.front())) != 0) text.remove_prefix(1);
  while (!text.empty() && std::isspace(static_cast<unsigned char>(text.back())) != 0) text.remove_suffix(1);
  return std::string(text);
}

bool is_ascii_space_at(std::string_view text, std::size_t cursor)
{
  if (cursor >= text.size())
    return false;
  auto const byte = static_cast<unsigned char>(text[cursor]);
  return (byte & 0x80U) == 0 && std::isspace(byte) != 0;
}

std::size_t previous_input_cursor(std::string_view text, std::size_t cursor)
{
  cursor = clamp_composer_draft_cursor(text, cursor);
  if (cursor == 0)
    return 0;
  if (!detail::is_utf8_continuation(static_cast<unsigned char>(text[cursor - 1])))
    return cursor - 1;

  auto start = cursor;
  while (start > 0 && detail::is_utf8_continuation(static_cast<unsigned char>(text[start - 1])))
  {
    --start;
  }
  if (start == 0)
    return cursor - 1;

  auto const starter = start - 1;
  auto const expected_length = detail::utf8_sequence_length(static_cast<unsigned char>(text[starter]));
  auto const actual_length = cursor - starter;
  if (expected_length > 1 && expected_length == actual_length)
    return starter;
  return cursor - 1;
}

std::size_t next_input_cursor(std::string_view text, std::size_t cursor)
{
  cursor = clamp_composer_draft_cursor(text, cursor);
  if (cursor >= text.size())
    return text.size();

  auto const length = detail::utf8_sequence_length(static_cast<unsigned char>(text[cursor]));
  char32_t codepoint = 0;
  if (length > 1 && detail::decode_utf8_codepoint(text, cursor, length, codepoint))
    return cursor + length;
  return cursor + 1;
}

std::size_t line_start_cursor(std::string_view text, std::size_t cursor)
{
  cursor = clamp_composer_draft_cursor(text, cursor);
  if (cursor == 0)
    return 0;
  auto const line_break = text.rfind('\n', cursor - 1);
  return line_break == std::string_view::npos ? std::size_t{0} : line_break + 1;
}

std::size_t line_end_cursor(std::string_view text, std::size_t cursor)
{
  cursor = clamp_composer_draft_cursor(text, cursor);
  auto const line_break = text.find('\n', cursor);
  return line_break == std::string_view::npos ? text.size() : line_break;
}

std::size_t delete_to_line_end_cursor(std::string_view text, std::size_t cursor)
{
  auto const line_end = line_end_cursor(text, cursor);
  if (line_end == cursor && line_end < text.size() && text[line_end] == '\n')
    return line_end + 1;
  return line_end;
}

std::size_t line_column(std::string_view text, std::size_t cursor)
{
  auto const start = line_start_cursor(text, cursor);
  return cursor - start;
}

std::size_t previous_line_cursor(std::string_view text, std::size_t cursor, std::size_t target_column)
{
  cursor = clamp_composer_draft_cursor(text, cursor);
  auto const current_start = line_start_cursor(text, cursor);
  if (current_start == 0)
    return cursor;
  auto const previous_end = current_start - 1;
  auto const previous_start = line_start_cursor(text, previous_end);
  auto const target = previous_start + target_column;
  return std::min(target, previous_end);
}

std::size_t next_line_cursor(std::string_view text, std::size_t cursor, std::size_t target_column)
{
  cursor = clamp_composer_draft_cursor(text, cursor);
  auto const current_end = line_end_cursor(text, cursor);
  if (current_end >= text.size())
    return cursor;
  auto const next_start = current_end + 1;
  auto const next_end = line_end_cursor(text, next_start);
  auto const target = next_start + target_column;
  return std::min(target, next_end);
}

void clear_yank_tracking(ComposerDraftState& draft)
{
  draft.yank_start = std::string::npos;
  draft.yank_end = std::string::npos;
  draft.yank_ring_index = 0;
}

void clear_vertical_tracking(ComposerDraftState& draft)
{
  draft.vertical_column = std::string::npos;
}

void clear_nonvertical_tracking(ComposerDraftState& draft)
{
  clear_yank_tracking(draft);
  clear_vertical_tracking(draft);
}

ComposerDraftSnapshot current_snapshot(ComposerDraftState const& draft)
{
  auto const cursor = clamp_composer_draft_cursor(draft.text, draft.cursor);
  return ComposerDraftSnapshot{
      .text = draft.text, .cursor = cursor, .paste_entries = draft.paste_entries, .next_paste_id = draft.next_paste_id};
}

void push_snapshot(std::vector<ComposerDraftSnapshot>& stack, ComposerDraftSnapshot snapshot)
{
  if (!stack.empty() && stack.back().text == snapshot.text && stack.back().cursor == snapshot.cursor &&
      stack.back().paste_entries == snapshot.paste_entries && stack.back().next_paste_id == snapshot.next_paste_id)
    return;
  stack.push_back(std::move(snapshot));
  if (stack.size() > kMaxUndoItems)
  {
    stack.erase(stack.begin(), stack.begin() + static_cast<std::ptrdiff_t>(stack.size() - kMaxUndoItems));
  }
}

void push_undo(ComposerDraftState& draft)
{
  push_snapshot(draft.undo_stack, current_snapshot(draft));
}

void push_redo(ComposerDraftState& draft)
{
  push_snapshot(draft.redo_stack, current_snapshot(draft));
}

void record_undo(ComposerDraftState& draft)
{
  push_undo(draft);
  draft.redo_stack.clear();
  clear_nonvertical_tracking(draft);
}

void remember_kill(ComposerDraftState& draft, std::string killed)
{
  if (killed.empty())
    return;
  draft.kill_buffer = std::move(killed);
  if (draft.kill_ring.empty() || draft.kill_ring.front() != draft.kill_buffer)
  {
    draft.kill_ring.insert(draft.kill_ring.begin(), draft.kill_buffer);
    if (draft.kill_ring.size() > kMaxKillRingItems)
      draft.kill_ring.resize(kMaxKillRingItems);
  }
  clear_nonvertical_tracking(draft);
}

void ensure_kill_ring(ComposerDraftState& draft)
{
  if (!draft.kill_ring.empty() || draft.kill_buffer.empty())
    return;
  draft.kill_ring.push_back(draft.kill_buffer);
}

std::size_t input_line_count(std::string_view text)
{
  if (text.empty())
    return 0;
  return static_cast<std::size_t>(std::ranges::count(text, '\n')) + 1;
}

bool should_collapse_paste(std::string_view text)
{
  return input_line_count(text) > kPasteMarkerLineThreshold || text.size() > kPasteMarkerByteThreshold;
}

std::string paste_marker(std::size_t id, std::string_view text)
{
  auto const lines = input_line_count(text);
  if (lines > kPasteMarkerLineThreshold)
    return "[paste #" + std::to_string(id) + " +" + std::to_string(lines) + " lines]";
  return "[paste #" + std::to_string(id) + " " + std::to_string(text.size()) + " chars]";
}

struct PasteMarkerRange
{
  std::size_t start = 0;
  std::size_t end = 0;
};

bool active_paste_entry_matches(ComposerDraftState const& draft, ComposerPasteEntry const& entry)
{
  return entry.start != std::string::npos && !entry.marker.empty() &&
         entry.start + entry.marker.size() <= draft.text.size() &&
         draft.text.compare(entry.start, entry.marker.size(), entry.marker) == 0;
}

std::optional<PasteMarkerRange> paste_marker_starting_at(ComposerDraftState const& draft, std::size_t cursor)
{
  cursor = clamp_composer_draft_cursor(draft.text, cursor);
  for (auto const& entry : draft.paste_entries)
  {
    if (entry.start == cursor && active_paste_entry_matches(draft, entry))
    {
      return PasteMarkerRange{.start = cursor, .end = cursor + entry.marker.size()};
    }
  }
  return std::nullopt;
}

std::optional<PasteMarkerRange> paste_marker_touching_left(ComposerDraftState const& draft, std::size_t cursor)
{
  cursor = clamp_composer_draft_cursor(draft.text, cursor);
  for (auto const& entry : draft.paste_entries)
  {
    if (!active_paste_entry_matches(draft, entry))
      continue;
    auto const end = entry.start + entry.marker.size();
    if (entry.start < cursor && cursor <= end)
      return PasteMarkerRange{.start = entry.start, .end = end};
  }
  return std::nullopt;
}

void shift_paste_markers_for_insert(ComposerDraftState& draft, std::size_t cursor, std::size_t length)
{
  for (auto& entry : draft.paste_entries)
  {
    if (!active_paste_entry_matches(draft, entry))
      continue;
    auto const end = entry.start + entry.marker.size();
    if (entry.start < cursor && cursor < end)
    {
      entry.start = std::string::npos;
      continue;
    }
    if (entry.start >= cursor)
      entry.start += length;
  }
  std::erase_if(draft.paste_entries, [](ComposerPasteEntry const& entry) { return entry.start == std::string::npos; });
}

void shift_paste_markers_for_erase(ComposerDraftState& draft, std::size_t start, std::size_t end)
{
  if (end <= start)
    return;
  auto const length = end - start;
  for (auto& entry : draft.paste_entries)
  {
    if (!active_paste_entry_matches(draft, entry))
      continue;
    auto const marker_end = entry.start + entry.marker.size();
    if (end <= entry.start)
    {
      entry.start -= length;
      continue;
    }
    if (start >= marker_end)
      continue;
    entry.start = std::string::npos;
  }
  std::erase_if(draft.paste_entries, [](ComposerPasteEntry const& entry) { return entry.start == std::string::npos; });
}

void shift_paste_markers_for_replace(ComposerDraftState& draft, std::size_t start, std::size_t end,
                                     std::size_t replacement_size)
{
  if (end < start)
    std::swap(start, end);
  auto const erased_size = end - start;
  for (auto& entry : draft.paste_entries)
  {
    if (!active_paste_entry_matches(draft, entry))
      continue;
    auto const marker_end = entry.start + entry.marker.size();
    if (end <= entry.start)
    {
      entry.start = entry.start - erased_size + replacement_size;
      continue;
    }
    if (start >= marker_end)
      continue;
    entry.start = std::string::npos;
  }
  std::erase_if(draft.paste_entries, [](ComposerPasteEntry const& entry) { return entry.start == std::string::npos; });
}

std::size_t previous_atomic_input_cursor(ComposerDraftState const& draft)
{
  if (auto marker = paste_marker_touching_left(draft, draft.cursor))
    return marker->start;
  return previous_input_cursor(draft.text, draft.cursor);
}

std::size_t next_atomic_input_cursor(ComposerDraftState const& draft)
{
  auto const cursor = clamp_composer_draft_cursor(draft.text, draft.cursor);
  if (auto marker = paste_marker_starting_at(draft, cursor))
    return marker->end;
  return next_input_cursor(draft.text, cursor);
}

std::size_t previous_atomic_word_cursor(ComposerDraftState const& draft)
{
  auto cursor = clamp_composer_draft_cursor(draft.text, draft.cursor);
  while (cursor > 0)
  {
    if (paste_marker_touching_left(draft, cursor))
      break;
    auto const previous = previous_input_cursor(draft.text, cursor);
    if (!is_ascii_space_at(draft.text, previous))
      break;
    cursor = previous;
  }
  while (cursor > 0)
  {
    if (auto marker = paste_marker_touching_left(draft, cursor))
      return marker->start;
    auto const previous = previous_input_cursor(draft.text, cursor);
    if (is_ascii_space_at(draft.text, previous))
      break;
    cursor = previous;
  }
  return cursor;
}

std::size_t next_atomic_word_cursor(ComposerDraftState const& draft)
{
  auto cursor = clamp_composer_draft_cursor(draft.text, draft.cursor);
  if (auto marker = paste_marker_starting_at(draft, cursor))
    return marker->end;
  if (auto marker = paste_marker_touching_left(draft, cursor); marker && cursor < marker->end)
    return marker->end;
  while (cursor < draft.text.size() && is_ascii_space_at(draft.text, cursor))
  {
    cursor = next_input_cursor(draft.text, cursor);
  }
  if (auto marker = paste_marker_starting_at(draft, cursor))
    return marker->end;
  while (cursor < draft.text.size() && !is_ascii_space_at(draft.text, cursor))
  {
    if (auto marker = paste_marker_starting_at(draft, cursor))
      return marker->end;
    if (auto marker = paste_marker_touching_left(draft, cursor); marker && cursor < marker->end)
      return marker->end;
    cursor = next_input_cursor(draft.text, cursor);
  }
  return cursor;
}

bool erase_range(ComposerDraftState& draft, std::size_t start, std::size_t end)
{
  start = clamp_composer_draft_cursor(draft.text, start);
  end = clamp_composer_draft_cursor(draft.text, end);
  if (end < start)
    std::swap(start, end);
  if (start == end)
    return false;
  auto killed = draft.text.substr(start, end - start);
  record_undo(draft);
  remember_kill(draft, std::move(killed));
  shift_paste_markers_for_erase(draft, start, end);
  draft.text.erase(start, end - start);
  draft.cursor = start;
  return true;
}

}  // namespace

std::size_t clamp_composer_draft_cursor(std::string_view text, std::size_t cursor)
{
  cursor = std::min(cursor, text.size());
  while (cursor > 0 && cursor < text.size() && detail::is_utf8_continuation(static_cast<unsigned char>(text[cursor])))
  {
    --cursor;
  }
  return cursor;
}

void reset_composer_draft(ComposerDraftState& draft, std::string text, std::size_t cursor)
{
  draft.text = std::move(text);
  draft.cursor = cursor == std::string::npos ? draft.text.size() : clamp_composer_draft_cursor(draft.text, cursor);
  draft.undo_stack.clear();
  draft.redo_stack.clear();
  draft.paste_entries.clear();
  draft.next_paste_id = 1;
  clear_nonvertical_tracking(draft);
}

bool replace_composer_draft(ComposerDraftState& draft, std::string text, std::size_t cursor)
{
  auto const next_cursor = cursor == std::string::npos ? text.size() : clamp_composer_draft_cursor(text, cursor);
  if (draft.text == text && clamp_composer_draft_cursor(draft.text, draft.cursor) == next_cursor)
    return false;
  record_undo(draft);
  draft.text = std::move(text);
  draft.cursor = next_cursor;
  draft.paste_entries.clear();
  draft.next_paste_id = 1;
  return true;
}

bool insert_composer_draft_text(ComposerDraftState& draft, std::string_view text)
{
  if (text.empty())
    return false;
  record_undo(draft);
  draft.cursor = clamp_composer_draft_cursor(draft.text, draft.cursor);
  shift_paste_markers_for_insert(draft, draft.cursor, text.size());
  draft.text.insert(draft.cursor, text);
  draft.cursor += text.size();
  return true;
}

bool insert_composer_paste_text(ComposerDraftState& draft, std::string_view text)
{
  if (text.empty())
    return false;
  if (!should_collapse_paste(text))
    return insert_composer_draft_text(draft, text);

  record_undo(draft);
  auto const id = draft.next_paste_id++;
  auto marker = paste_marker(id, text);
  auto marker_text = marker;
  draft.cursor = clamp_composer_draft_cursor(draft.text, draft.cursor);
  auto const marker_start = draft.cursor;
  shift_paste_markers_for_insert(draft, marker_start, marker_text.size());
  draft.text.insert(draft.cursor, marker_text);
  draft.cursor += marker_text.size();
  draft.paste_entries.push_back(
      ComposerPasteEntry{.id = id, .marker = std::move(marker), .text = std::string(text), .start = marker_start});
  return true;
}

bool jump_composer_draft_to_character(ComposerDraftState& draft, std::string_view character, bool forward)
{
  if (character.empty() || draft.text.empty())
    return false;

  clear_yank_tracking(draft);
  clear_nonvertical_tracking(draft);
  draft.cursor = clamp_composer_draft_cursor(draft.text, draft.cursor);

  if (forward)
  {
    auto const search_from = draft.cursor >= draft.text.size() ? draft.text.size() : next_input_cursor(draft.text, draft.cursor);
    auto const found = draft.text.find(character, search_from);
    if (found == std::string::npos)
      return false;
    draft.cursor = clamp_composer_draft_cursor(draft.text, found);
    return true;
  }

  if (draft.cursor == 0)
    return false;
  auto const search_before = previous_input_cursor(draft.text, draft.cursor);
  auto const found = draft.text.rfind(character, search_before);
  if (found == std::string::npos)
    return false;
  draft.cursor = clamp_composer_draft_cursor(draft.text, found);
  return true;
}

bool apply_composer_draft_action(ComposerDraftState& draft, TuiAction action)
{
  draft.cursor = clamp_composer_draft_cursor(draft.text, draft.cursor);
  switch (action)
  {
    case TuiAction::ClearInput:
      if (draft.text.empty())
        return false;
      {
        auto killed = draft.text;
        record_undo(draft);
        remember_kill(draft, std::move(killed));
      }
      draft.text.clear();
      draft.cursor = 0;
      draft.paste_entries.clear();
      draft.next_paste_id = 1;
      return true;
    case TuiAction::DeleteBackward:
      return erase_range(draft, previous_atomic_input_cursor(draft), draft.cursor);
    case TuiAction::DeleteForward:
      return erase_range(draft, draft.cursor, next_atomic_input_cursor(draft));
    case TuiAction::DeleteWordBackward:
      return erase_range(draft, previous_atomic_word_cursor(draft), draft.cursor);
    case TuiAction::DeleteWordForward:
      return erase_range(draft, draft.cursor, next_atomic_word_cursor(draft));
    case TuiAction::DeleteToLineStart:
      return erase_range(draft, line_start_cursor(draft.text, draft.cursor), draft.cursor);
    case TuiAction::DeleteToLineEnd:
      return erase_range(draft, draft.cursor, delete_to_line_end_cursor(draft.text, draft.cursor));
    case TuiAction::CursorLeft:
      clear_nonvertical_tracking(draft);
      draft.cursor = previous_atomic_input_cursor(draft);
      return true;
    case TuiAction::CursorRight:
      clear_nonvertical_tracking(draft);
      draft.cursor = next_atomic_input_cursor(draft);
      return true;
    case TuiAction::CursorUp: {
      if (draft.vertical_column == std::string::npos)
        draft.vertical_column = line_column(draft.text, draft.cursor);
      auto const next = previous_line_cursor(draft.text, draft.cursor, draft.vertical_column);
      if (next == draft.cursor)
        return false;
      clear_yank_tracking(draft);
      draft.cursor = next;
      return true;
    }
    case TuiAction::CursorDown: {
      if (draft.vertical_column == std::string::npos)
        draft.vertical_column = line_column(draft.text, draft.cursor);
      auto const next = next_line_cursor(draft.text, draft.cursor, draft.vertical_column);
      if (next == draft.cursor)
        return false;
      clear_yank_tracking(draft);
      draft.cursor = next;
      return true;
    }
    case TuiAction::CursorLineStart:
      clear_nonvertical_tracking(draft);
      draft.cursor = line_start_cursor(draft.text, draft.cursor);
      return true;
    case TuiAction::CursorLineEnd:
      clear_nonvertical_tracking(draft);
      draft.cursor = line_end_cursor(draft.text, draft.cursor);
      return true;
    case TuiAction::CursorWordLeft:
      clear_nonvertical_tracking(draft);
      draft.cursor = previous_atomic_word_cursor(draft);
      return true;
    case TuiAction::CursorWordRight:
      clear_nonvertical_tracking(draft);
      draft.cursor = next_atomic_word_cursor(draft);
      return true;
    case TuiAction::JumpForward:
    case TuiAction::JumpBackward:
      return false;
    case TuiAction::Undo: {
      if (draft.undo_stack.empty())
        return false;
      push_redo(draft);
      auto previous = std::move(draft.undo_stack.back());
      draft.undo_stack.pop_back();
      draft.text = std::move(previous.text);
      draft.cursor = clamp_composer_draft_cursor(draft.text, previous.cursor);
      draft.paste_entries = std::move(previous.paste_entries);
      draft.next_paste_id = previous.next_paste_id;
      clear_nonvertical_tracking(draft);
      return true;
    }
    case TuiAction::Redo: {
      if (draft.redo_stack.empty())
        return false;
      push_undo(draft);
      auto next = std::move(draft.redo_stack.back());
      draft.redo_stack.pop_back();
      draft.text = std::move(next.text);
      draft.cursor = clamp_composer_draft_cursor(draft.text, next.cursor);
      draft.paste_entries = std::move(next.paste_entries);
      draft.next_paste_id = next.next_paste_id;
      clear_nonvertical_tracking(draft);
      return true;
    }
    case TuiAction::Yank: {
      ensure_kill_ring(draft);
      if (draft.kill_ring.empty() || draft.kill_ring.front().empty())
        return false;
      auto const yanked = draft.kill_ring.front();
      record_undo(draft);
      auto const start = draft.cursor;
      shift_paste_markers_for_insert(draft, draft.cursor, yanked.size());
      draft.text.insert(draft.cursor, yanked);
      draft.cursor += yanked.size();
      draft.kill_buffer = yanked;
      draft.yank_start = start;
      draft.yank_end = draft.cursor;
      draft.yank_ring_index = 0;
      return true;
    }
    case TuiAction::YankPop: {
      ensure_kill_ring(draft);
      if (draft.kill_ring.size() < 2 || draft.yank_start == std::string::npos || draft.yank_end == std::string::npos || draft.yank_start > draft.text.size() ||
          draft.yank_end > draft.text.size() || draft.yank_start > draft.yank_end)
      {
        return false;
      }
      auto const start = draft.yank_start;
      auto const end = draft.yank_end;
      auto const next_ring_index = (draft.yank_ring_index + 1) % draft.kill_ring.size();
      auto const replacement = draft.kill_ring[next_ring_index];
      record_undo(draft);
      shift_paste_markers_for_replace(draft, start, end, replacement.size());
      draft.text.replace(start, end - start, replacement);
      draft.cursor = start + replacement.size();
      draft.kill_buffer = replacement;
      draft.yank_start = start;
      draft.yank_end = draft.cursor;
      draft.yank_ring_index = next_ring_index;
      return true;
    }
    case TuiAction::Submit:
    case TuiAction::NewLine:
    case TuiAction::Cancel:
    case TuiAction::HistoryPrev:
    case TuiAction::HistoryNext:
    case TuiAction::PalettePrev:
    case TuiAction::PaletteNext:
    case TuiAction::SelectPrev:
    case TuiAction::SelectNext:
    case TuiAction::SelectPageUp:
    case TuiAction::SelectPageDown:
    case TuiAction::SelectConfirm:
    case TuiAction::SelectCancel:
    case TuiAction::AutocompleteAccept:
    case TuiAction::PromptAllow:
    case TuiAction::PromptDeny:
    case TuiAction::DetailsToggle:
    case TuiAction::PageUp:
    case TuiAction::PageDown:
    case TuiAction::ModeToggle:
    case TuiAction::Interrupt:
    case TuiAction::Exit:
    case TuiAction::VariantCycle:
    case TuiAction::ModelSelect:
    case TuiAction::ModelCycleForward:
    case TuiAction::ModelCycleBackward:
    case TuiAction::MessageDequeue:
    case TuiAction::MessagePrev:
    case TuiAction::MessageNext:
    case TuiAction::JumpToBottom:
      return false;
  }
  return false;
}

bool push_composer_input_history(std::vector<std::string>& history, std::string input)
{
  input = trim_ascii_copy(input);
  if (input.empty())
    return false;
  if (!history.empty() && history.back() == input)
    return false;
  history.push_back(std::move(input));
  if (history.size() > kMaxInputHistoryItems)
  {
    history.erase(history.begin(),
                  history.begin() + static_cast<std::ptrdiff_t>(history.size() - kMaxInputHistoryItems));
  }
  return true;
}

void clear_composer_input_history_browse(std::optional<std::size_t>& history_index, std::string& draft_input)
{
  history_index.reset();
  draft_input.clear();
}

bool browse_composer_input_history(ComposerDraftState& draft, std::vector<std::string> const& history,
                                   std::optional<std::size_t>& history_index, std::string& draft_input,
                                   bool previous)
{
  if (history.empty())
    return false;

  if (previous)
  {
    if (!history_index)
    {
      draft_input = draft.text;
      history_index = history.size() - 1;
    }
    else if (*history_index > 0)
    {
      --(*history_index);
    }
    else
    {
      return false;
    }
    return replace_composer_draft(draft, history[*history_index], 0);
  }

  if (!history_index)
    return false;

  if (*history_index + 1 >= history.size())
  {
    auto restored = std::move(draft_input);
    clear_composer_input_history_browse(history_index, draft_input);
    return replace_composer_draft(draft, std::move(restored));
  }

  ++(*history_index);
  return replace_composer_draft(draft, history[*history_index]);
}

std::string normalize_composer_paste_text(std::string_view text)
{
  std::string output;
  output.reserve(text.size());
  for (std::size_t index = 0; index < text.size(); ++index)
  {
    auto const byte = static_cast<unsigned char>(text[index]);
    if (byte == '\r')
    {
      if (index + 1 < text.size() && text[index + 1] == '\n')
        ++index;
      output.push_back('\n');
      continue;
    }
    if (byte == '\n' || byte == '\t' || byte >= 0x20)
      output.push_back(static_cast<char>(byte));
  }
  return output;
}

std::string expanded_composer_draft_text(ComposerDraftState const& draft)
{
  if (draft.text.empty() || draft.paste_entries.empty())
    return draft.text;

  std::string output;
  output.reserve(draft.text.size());
  for (std::size_t index = 0; index < draft.text.size();)
  {
    auto const found = std::ranges::find_if(draft.paste_entries, [&](ComposerPasteEntry const& entry) {
      return entry.start == index && active_paste_entry_matches(draft, entry);
    });
    if (found != draft.paste_entries.end())
    {
      output += found->text;
      index += found->marker.size();
      continue;
    }
    output.push_back(draft.text[index]);
    ++index;
  }
  return output;
}

}  // namespace ava::tui
