#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "ava/core/result.h"
#include "ava/tui/terminal.h"

namespace ava::tui {

enum class TuiAction {
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
  Yank,
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

struct TuiKeyBindings {
  std::vector<std::pair<TuiAction, std::vector<Key>>> bindings;
};

struct TuiKeyBindingHelpItem {
  std::string action;
  std::string description;
  std::string keys;
};

[[nodiscard]] TuiKeyBindings default_key_bindings();
// Returns the first configured action for a key. Runtime dispatch should prefer
// key_matches_action() when shared keys need context-specific handling.
[[nodiscard]] std::optional<TuiAction> action_for_key(const TuiKeyBindings& bindings, Key key);
[[nodiscard]] bool key_matches_action(const TuiKeyBindings& bindings, TuiAction action, Key key);
[[nodiscard]] std::optional<Key> parse_key_name(std::string_view text);
[[nodiscard]] std::string key_display(Key key);
[[nodiscard]] std::string action_name(TuiAction action);
[[nodiscard]] std::string action_description(TuiAction action);
[[nodiscard]] std::string keys_display(const TuiKeyBindings& bindings, TuiAction action);
[[nodiscard]] std::vector<TuiKeyBindingHelpItem> key_binding_help_items(const TuiKeyBindings& bindings);
[[nodiscard]] ava::core::Result<TuiKeyBindings> parse_key_bindings_json(std::string_view json);
[[nodiscard]] ava::core::Result<TuiKeyBindings> parse_key_bindings_json(std::string_view json, TuiKeyBindings base);
[[nodiscard]] ava::core::Result<TuiKeyBindings> load_key_bindings(const std::filesystem::path& keybinds_file);

}  // namespace ava::tui
