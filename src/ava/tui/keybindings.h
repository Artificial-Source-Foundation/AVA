#pragma once

#include "ava/tui/terminal.h"
#include "ava/core/result.h"

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace ava::tui {

enum class TuiAction
{
  Submit,
  NewLine,
  Cancel,
  ClearInput,
  DeleteBackward,
  HistoryPrev,
  HistoryNext,
  PalettePrev,
  PaletteNext,
  CursorLeft,
  CursorRight,
  CursorLineStart,
  CursorLineEnd,
  CursorWordLeft,
  CursorWordRight,
  DeleteWordBackward,
  DeleteToLineStart,
  DeleteToLineEnd,
  Undo,
  Redo,
  Yank,
  YankPop,
  AutocompleteAccept,
  PromptAllow,
  PromptDeny,
  DetailsToggle,
  PageUp,
  PageDown,
  ModeToggle,
  Interrupt,
  Exit,
  VariantCycle,
};

struct TuiKeyBindings
{
  std::vector<std::pair<TuiAction, std::vector<Key>>> bindings;
};

struct TuiKeyBindingHelpItem
{
  std::string action;
  std::string description;
  std::string keys;
};

[[nodiscard]] TuiKeyBindings default_key_bindings();
// Returns the first configured action for a key. Runtime dispatch should prefer
// key_matches_action() when shared keys need context-specific handling.
[[nodiscard]] std::optional<TuiAction> action_for_key(TuiKeyBindings const& bindings, Key key);
[[nodiscard]] bool key_matches_action(TuiKeyBindings const& bindings, TuiAction action, Key key);
[[nodiscard]] std::optional<Key> parse_key_name(std::string_view text);
[[nodiscard]] std::string key_display(Key key);
[[nodiscard]] std::string action_name(TuiAction action);
[[nodiscard]] std::string action_description(TuiAction action);
[[nodiscard]] std::string keys_display(TuiKeyBindings const& bindings, TuiAction action);
[[nodiscard]] std::vector<TuiKeyBindingHelpItem> key_binding_help_items(TuiKeyBindings const& bindings);
[[nodiscard]] ava::core::Result<TuiKeyBindings> parse_key_bindings_json(std::string_view json);
[[nodiscard]] ava::core::Result<TuiKeyBindings> parse_key_bindings_json(std::string_view json, TuiKeyBindings base);
[[nodiscard]] ava::core::Result<TuiKeyBindings> load_key_bindings(std::filesystem::path const& keybinds_file);

}  // namespace ava::tui
