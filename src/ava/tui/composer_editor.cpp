#include "ava/tui/composer_editor.h"

#include <algorithm>
#include <cctype>

#include "ava/tui/composer_internal.h"

namespace ava::tui {
namespace {

constexpr std::size_t kMaxUndoItems = 100;

bool is_ascii_space_at(std::string_view text, std::size_t cursor) {
  if (cursor >= text.size()) return false;
  const auto byte = static_cast<unsigned char>(text[cursor]);
  return (byte & 0x80U) == 0 && std::isspace(byte) != 0;
}

std::size_t previous_input_cursor(std::string_view text, std::size_t cursor) {
  cursor = clamp_composer_draft_cursor(text, cursor);
  if (cursor == 0) return 0;
  if (!detail::is_utf8_continuation(static_cast<unsigned char>(text[cursor - 1]))) return cursor - 1;

  auto start = cursor;
  while (start > 0 && detail::is_utf8_continuation(static_cast<unsigned char>(text[start - 1]))) {
    --start;
  }
  if (start == 0) return cursor - 1;

  const auto starter = start - 1;
  const auto expected_length = detail::utf8_sequence_length(static_cast<unsigned char>(text[starter]));
  const auto actual_length = cursor - starter;
  if (expected_length > 1 && expected_length == actual_length) return starter;
  return cursor - 1;
}

std::size_t next_input_cursor(std::string_view text, std::size_t cursor) {
  cursor = clamp_composer_draft_cursor(text, cursor);
  if (cursor >= text.size()) return text.size();

  const auto length = detail::utf8_sequence_length(static_cast<unsigned char>(text[cursor]));
  char32_t codepoint = 0;
  if (length > 1 && detail::decode_utf8_codepoint(text, cursor, length, codepoint)) return cursor + length;
  return cursor + 1;
}

std::size_t previous_word_cursor(std::string_view text, std::size_t cursor) {
  cursor = clamp_composer_draft_cursor(text, cursor);
  while (cursor > 0) {
    const auto previous = previous_input_cursor(text, cursor);
    if (!is_ascii_space_at(text, previous)) break;
    cursor = previous;
  }
  while (cursor > 0) {
    const auto previous = previous_input_cursor(text, cursor);
    if (is_ascii_space_at(text, previous)) break;
    cursor = previous;
  }
  return cursor;
}

std::size_t next_word_cursor(std::string_view text, std::size_t cursor) {
  cursor = clamp_composer_draft_cursor(text, cursor);
  while (cursor < text.size() && !is_ascii_space_at(text, cursor)) {
    cursor = next_input_cursor(text, cursor);
  }
  while (cursor < text.size() && is_ascii_space_at(text, cursor)) {
    cursor = next_input_cursor(text, cursor);
  }
  return cursor;
}

std::size_t line_start_cursor(std::string_view text, std::size_t cursor) {
  cursor = clamp_composer_draft_cursor(text, cursor);
  if (cursor == 0) return 0;
  const auto line_break = text.rfind('\n', cursor - 1);
  return line_break == std::string_view::npos ? std::size_t{0} : line_break + 1;
}

std::size_t line_end_cursor(std::string_view text, std::size_t cursor) {
  cursor = clamp_composer_draft_cursor(text, cursor);
  const auto line_break = text.find('\n', cursor);
  return line_break == std::string_view::npos ? text.size() : line_break;
}

void record_undo(ComposerDraftState& draft) {
  const auto cursor = clamp_composer_draft_cursor(draft.text, draft.cursor);
  if (!draft.undo_stack.empty() && draft.undo_stack.back().text == draft.text &&
      draft.undo_stack.back().cursor == cursor) {
    return;
  }
  draft.undo_stack.push_back(ComposerDraftSnapshot{.text = draft.text, .cursor = cursor});
  if (draft.undo_stack.size() > kMaxUndoItems) {
    draft.undo_stack.erase(
        draft.undo_stack.begin(),
        draft.undo_stack.begin() + static_cast<std::ptrdiff_t>(draft.undo_stack.size() - kMaxUndoItems));
  }
}

bool erase_range(ComposerDraftState& draft, std::size_t start, std::size_t end) {
  start = clamp_composer_draft_cursor(draft.text, start);
  end = clamp_composer_draft_cursor(draft.text, end);
  if (end < start) std::swap(start, end);
  if (start == end) return false;
  draft.kill_buffer = draft.text.substr(start, end - start);
  record_undo(draft);
  draft.text.erase(start, end - start);
  draft.cursor = start;
  return true;
}

}  // namespace

std::size_t clamp_composer_draft_cursor(std::string_view text, std::size_t cursor) {
  cursor = std::min(cursor, text.size());
  while (cursor > 0 && cursor < text.size() && detail::is_utf8_continuation(static_cast<unsigned char>(text[cursor]))) {
    --cursor;
  }
  return cursor;
}

void reset_composer_draft(ComposerDraftState& draft, std::string text, std::size_t cursor) {
  draft.text = std::move(text);
  draft.cursor = cursor == std::string::npos ? draft.text.size() : clamp_composer_draft_cursor(draft.text, cursor);
  draft.undo_stack.clear();
}

bool replace_composer_draft(ComposerDraftState& draft, std::string text, std::size_t cursor) {
  const auto next_cursor = cursor == std::string::npos ? text.size() : clamp_composer_draft_cursor(text, cursor);
  if (draft.text == text && clamp_composer_draft_cursor(draft.text, draft.cursor) == next_cursor) return false;
  record_undo(draft);
  draft.text = std::move(text);
  draft.cursor = next_cursor;
  return true;
}

bool insert_composer_draft_text(ComposerDraftState& draft, std::string_view text) {
  if (text.empty()) return false;
  record_undo(draft);
  draft.cursor = clamp_composer_draft_cursor(draft.text, draft.cursor);
  draft.text.insert(draft.cursor, text);
  draft.cursor += text.size();
  return true;
}

bool apply_composer_draft_action(ComposerDraftState& draft, TuiAction action) {
  draft.cursor = clamp_composer_draft_cursor(draft.text, draft.cursor);
  switch (action) {
    case TuiAction::ClearInput:
      if (draft.text.empty()) return false;
      draft.kill_buffer = draft.text;
      record_undo(draft);
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
      draft.cursor = previous_input_cursor(draft.text, draft.cursor);
      return true;
    case TuiAction::CursorRight:
      draft.cursor = next_input_cursor(draft.text, draft.cursor);
      return true;
    case TuiAction::CursorLineStart:
      draft.cursor = line_start_cursor(draft.text, draft.cursor);
      return true;
    case TuiAction::CursorLineEnd:
      draft.cursor = line_end_cursor(draft.text, draft.cursor);
      return true;
    case TuiAction::CursorWordLeft:
      draft.cursor = previous_word_cursor(draft.text, draft.cursor);
      return true;
    case TuiAction::CursorWordRight:
      draft.cursor = next_word_cursor(draft.text, draft.cursor);
      return true;
    case TuiAction::Undo: {
      if (draft.undo_stack.empty()) return false;
      auto previous = std::move(draft.undo_stack.back());
      draft.undo_stack.pop_back();
      draft.text = std::move(previous.text);
      draft.cursor = clamp_composer_draft_cursor(draft.text, previous.cursor);
      return true;
    }
    case TuiAction::Yank:
      if (draft.kill_buffer.empty()) return false;
      return insert_composer_draft_text(draft, draft.kill_buffer);
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

std::string normalize_composer_paste_text(std::string_view text) {
  std::string output;
  output.reserve(text.size());
  for (std::size_t index = 0; index < text.size(); ++index) {
    const auto byte = static_cast<unsigned char>(text[index]);
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
