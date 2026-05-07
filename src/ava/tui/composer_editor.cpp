#include "ava/tui/composer_editor.h"

#include "ava/tui/composer_internal.h"

#include <algorithm>
#include <cctype>
#include <utility>

namespace ava::tui {
namespace {

constexpr std::size_t kMaxUndoItems = 100;
constexpr std::size_t kMaxKillRingItems = 16;

bool is_ascii_space_at(std::string_view text, std::size_t cursor)
{
  if (cursor >= text.size()) return false;
  auto const byte = static_cast<unsigned char>(text[cursor]);
  return (byte & 0x80U) == 0 && std::isspace(byte) != 0;
}

std::size_t previous_input_cursor(std::string_view text, std::size_t cursor)
{
  cursor = clamp_composer_draft_cursor(text, cursor);
  if (cursor == 0) return 0;
  if (!detail::is_utf8_continuation(static_cast<unsigned char>(text[cursor - 1]))) return cursor - 1;

  auto start = cursor;
  while (start > 0 && detail::is_utf8_continuation(static_cast<unsigned char>(text[start - 1]))) {
    --start;
  }
  if (start == 0) return cursor - 1;

  auto const starter = start - 1;
  auto const expected_length = detail::utf8_sequence_length(static_cast<unsigned char>(text[starter]));
  auto const actual_length = cursor - starter;
  if (expected_length > 1 && expected_length == actual_length) return starter;
  return cursor - 1;
}

std::size_t next_input_cursor(std::string_view text, std::size_t cursor)
{
  cursor = clamp_composer_draft_cursor(text, cursor);
  if (cursor >= text.size()) return text.size();

  auto const length = detail::utf8_sequence_length(static_cast<unsigned char>(text[cursor]));
  char32_t codepoint = 0;
  if (length > 1 && detail::decode_utf8_codepoint(text, cursor, length, codepoint)) return cursor + length;
  return cursor + 1;
}

std::size_t previous_word_cursor(std::string_view text, std::size_t cursor)
{
  cursor = clamp_composer_draft_cursor(text, cursor);
  while (cursor > 0) {
    auto const previous = previous_input_cursor(text, cursor);
    if (!is_ascii_space_at(text, previous)) break;
    cursor = previous;
  }
  while (cursor > 0) {
    auto const previous = previous_input_cursor(text, cursor);
    if (is_ascii_space_at(text, previous)) break;
    cursor = previous;
  }
  return cursor;
}

std::size_t next_word_cursor(std::string_view text, std::size_t cursor)
{
  cursor = clamp_composer_draft_cursor(text, cursor);
  while (cursor < text.size() && !is_ascii_space_at(text, cursor)) {
    cursor = next_input_cursor(text, cursor);
  }
  while (cursor < text.size() && is_ascii_space_at(text, cursor)) {
    cursor = next_input_cursor(text, cursor);
  }
  return cursor;
}

std::size_t line_start_cursor(std::string_view text, std::size_t cursor)
{
  cursor = clamp_composer_draft_cursor(text, cursor);
  if (cursor == 0) return 0;
  auto const line_break = text.rfind('\n', cursor - 1);
  return line_break == std::string_view::npos ? std::size_t{0} : line_break + 1;
}

std::size_t line_end_cursor(std::string_view text, std::size_t cursor)
{
  cursor = clamp_composer_draft_cursor(text, cursor);
  auto const line_break = text.find('\n', cursor);
  return line_break == std::string_view::npos ? text.size() : line_break;
}

void clear_yank_tracking(ComposerDraftState& draft)
{
  draft.yank_start = std::string::npos;
  draft.yank_end = std::string::npos;
  draft.yank_ring_index = 0;
}

ComposerDraftSnapshot current_snapshot(ComposerDraftState const& draft)
{
  auto const cursor = clamp_composer_draft_cursor(draft.text, draft.cursor);
  return ComposerDraftSnapshot{.text = draft.text, .cursor = cursor};
}

void push_snapshot(std::vector<ComposerDraftSnapshot>& stack, ComposerDraftSnapshot snapshot)
{
  if (!stack.empty() && stack.back().text == snapshot.text && stack.back().cursor == snapshot.cursor) return;
  stack.push_back(std::move(snapshot));
  if (stack.size() > kMaxUndoItems) {
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
  clear_yank_tracking(draft);
}

void remember_kill(ComposerDraftState& draft, std::string killed)
{
  if (killed.empty()) return;
  draft.kill_buffer = std::move(killed);
  if (draft.kill_ring.empty() || draft.kill_ring.front() != draft.kill_buffer) {
    draft.kill_ring.insert(draft.kill_ring.begin(), draft.kill_buffer);
    if (draft.kill_ring.size() > kMaxKillRingItems) draft.kill_ring.resize(kMaxKillRingItems);
  }
  clear_yank_tracking(draft);
}

void ensure_kill_ring(ComposerDraftState& draft)
{
  if (!draft.kill_ring.empty() || draft.kill_buffer.empty()) return;
  draft.kill_ring.push_back(draft.kill_buffer);
}

bool erase_range(ComposerDraftState& draft, std::size_t start, std::size_t end)
{
  start = clamp_composer_draft_cursor(draft.text, start);
  end = clamp_composer_draft_cursor(draft.text, end);
  if (end < start) std::swap(start, end);
  if (start == end) return false;
  auto killed = draft.text.substr(start, end - start);
  record_undo(draft);
  remember_kill(draft, std::move(killed));
  draft.text.erase(start, end - start);
  draft.cursor = start;
  return true;
}

}  // namespace

std::size_t clamp_composer_draft_cursor(std::string_view text, std::size_t cursor)
{
  cursor = std::min(cursor, text.size());
  while (cursor > 0 && cursor < text.size() && detail::is_utf8_continuation(static_cast<unsigned char>(text[cursor]))) {
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
  clear_yank_tracking(draft);
}

bool replace_composer_draft(ComposerDraftState& draft, std::string text, std::size_t cursor)
{
  auto const next_cursor = cursor == std::string::npos ? text.size() : clamp_composer_draft_cursor(text, cursor);
  if (draft.text == text && clamp_composer_draft_cursor(draft.text, draft.cursor) == next_cursor) return false;
  record_undo(draft);
  draft.text = std::move(text);
  draft.cursor = next_cursor;
  return true;
}

bool insert_composer_draft_text(ComposerDraftState& draft, std::string_view text)
{
  if (text.empty()) return false;
  record_undo(draft);
  draft.cursor = clamp_composer_draft_cursor(draft.text, draft.cursor);
  draft.text.insert(draft.cursor, text);
  draft.cursor += text.size();
  return true;
}

bool apply_composer_draft_action(ComposerDraftState& draft, TuiAction action)
{
  draft.cursor = clamp_composer_draft_cursor(draft.text, draft.cursor);
  switch (action) {
    case TuiAction::ClearInput:
      if (draft.text.empty()) return false;
      {
        auto killed = draft.text;
        record_undo(draft);
        remember_kill(draft, std::move(killed));
      }
      draft.text.clear();
      draft.cursor = 0;
      return true;
    case TuiAction::DeleteBackward:
      return erase_range(draft, previous_input_cursor(draft.text, draft.cursor), draft.cursor);
    case TuiAction::DeleteWordBackward:
      return erase_range(draft, previous_word_cursor(draft.text, draft.cursor), draft.cursor);
    case TuiAction::DeleteToLineStart:
      return erase_range(draft, line_start_cursor(draft.text, draft.cursor), draft.cursor);
    case TuiAction::DeleteToLineEnd:
      return erase_range(draft, draft.cursor, line_end_cursor(draft.text, draft.cursor));
    case TuiAction::CursorLeft:
      clear_yank_tracking(draft);
      draft.cursor = previous_input_cursor(draft.text, draft.cursor);
      return true;
    case TuiAction::CursorRight:
      clear_yank_tracking(draft);
      draft.cursor = next_input_cursor(draft.text, draft.cursor);
      return true;
    case TuiAction::CursorLineStart:
      clear_yank_tracking(draft);
      draft.cursor = line_start_cursor(draft.text, draft.cursor);
      return true;
    case TuiAction::CursorLineEnd:
      clear_yank_tracking(draft);
      draft.cursor = line_end_cursor(draft.text, draft.cursor);
      return true;
    case TuiAction::CursorWordLeft:
      clear_yank_tracking(draft);
      draft.cursor = previous_word_cursor(draft.text, draft.cursor);
      return true;
    case TuiAction::CursorWordRight:
      clear_yank_tracking(draft);
      draft.cursor = next_word_cursor(draft.text, draft.cursor);
      return true;
    case TuiAction::Undo: {
      if (draft.undo_stack.empty()) return false;
      push_redo(draft);
      auto previous = std::move(draft.undo_stack.back());
      draft.undo_stack.pop_back();
      draft.text = std::move(previous.text);
      draft.cursor = clamp_composer_draft_cursor(draft.text, previous.cursor);
      clear_yank_tracking(draft);
      return true;
    }
    case TuiAction::Redo: {
      if (draft.redo_stack.empty()) return false;
      push_undo(draft);
      auto next = std::move(draft.redo_stack.back());
      draft.redo_stack.pop_back();
      draft.text = std::move(next.text);
      draft.cursor = clamp_composer_draft_cursor(draft.text, next.cursor);
      clear_yank_tracking(draft);
      return true;
    }
    case TuiAction::Yank: {
      ensure_kill_ring(draft);
      if (draft.kill_ring.empty() || draft.kill_ring.front().empty()) return false;
      auto const yanked = draft.kill_ring.front();
      record_undo(draft);
      auto const start = draft.cursor;
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
      if (draft.kill_ring.size() < 2 || draft.yank_start == std::string::npos || draft.yank_end == std::string::npos ||
          draft.yank_start > draft.text.size() || draft.yank_end > draft.text.size() ||
          draft.yank_start > draft.yank_end) {
        return false;
      }
      auto const start = draft.yank_start;
      auto const end = draft.yank_end;
      auto const next_ring_index = (draft.yank_ring_index + 1) % draft.kill_ring.size();
      auto const replacement = draft.kill_ring[next_ring_index];
      record_undo(draft);
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
      return false;
  }
  return false;
}

std::string normalize_composer_paste_text(std::string_view text)
{
  std::string output;
  output.reserve(text.size());
  for (std::size_t index = 0; index < text.size(); ++index) {
    auto const byte = static_cast<unsigned char>(text[index]);
    if (byte == '\r') {
      if (index + 1 < text.size() && text[index + 1] == '\n') ++index;
      output.push_back('\n');
      continue;
    }
    if (byte == '\n' || byte == '\t' || byte >= 0x20) output.push_back(static_cast<char>(byte));
  }
  return output;
}

}  // namespace ava::tui
