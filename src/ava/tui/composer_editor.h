#pragma once

#include "ava/tui/keybindings.h"

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

namespace ava::tui {

struct ComposerDraftSnapshot
{
  std::string text;
  std::size_t cursor = 0;
};

struct ComposerDraftState
{
  std::string text;
  std::size_t cursor = 0;
  std::string kill_buffer;
  std::vector<ComposerDraftSnapshot> undo_stack;
  std::vector<ComposerDraftSnapshot> redo_stack;
  std::vector<std::string> kill_ring;
  std::size_t yank_start = std::string::npos;
  std::size_t yank_end = std::string::npos;
  std::size_t yank_ring_index = 0;
};

[[nodiscard]] std::size_t clamp_composer_draft_cursor(std::string_view text, std::size_t cursor);
void reset_composer_draft(ComposerDraftState& draft, std::string text = {}, std::size_t cursor = std::string::npos);
bool replace_composer_draft(ComposerDraftState& draft, std::string text, std::size_t cursor = std::string::npos);
bool insert_composer_draft_text(ComposerDraftState& draft, std::string_view text);
bool apply_composer_draft_action(ComposerDraftState& draft, TuiAction action);
[[nodiscard]] std::string normalize_composer_paste_text(std::string_view text);

}  // namespace ava::tui
