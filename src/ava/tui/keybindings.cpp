#include "ava/tui/keybindings.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <fstream>
#include <iterator>

#include "ava/core/error.h"

namespace ava::tui {
namespace {

constexpr std::array kActions = {TuiAction::Submit,
                                 TuiAction::NewLine,
                                 TuiAction::Cancel,
                                 TuiAction::ClearInput,
                                 TuiAction::DeleteBackward,
                                 TuiAction::HistoryPrev,
                                 TuiAction::HistoryNext,
                                 TuiAction::PalettePrev,
                                 TuiAction::PaletteNext,
                                 TuiAction::CursorLeft,
                                 TuiAction::CursorRight,
                                 TuiAction::CursorLineStart,
                                 TuiAction::CursorLineEnd,
                                 TuiAction::CursorWordLeft,
                                 TuiAction::CursorWordRight,
                                 TuiAction::DeleteWordBackward,
                                 TuiAction::DeleteToLineStart,
                                 TuiAction::DeleteToLineEnd,
                                 TuiAction::Undo,
                                 TuiAction::Yank,
                                 TuiAction::AutocompleteAccept,
                                 TuiAction::PromptAllow,
                                 TuiAction::PromptDeny,
                                 TuiAction::DetailsToggle,
                                 TuiAction::PageUp,
                                 TuiAction::PageDown,
                                 TuiAction::ModeToggle,
                                 TuiAction::Interrupt,
                                 TuiAction::Exit,
                                 TuiAction::VariantCycle};

std::string normalize_token(std::string_view text) {
  std::string result;
  result.reserve(text.size());
  for (const char ch : text) {
    if (std::isspace(static_cast<unsigned char>(ch)) != 0 || ch == '-' || ch == '_') continue;
    result.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(ch))));
  }
  return result;
}

std::string trim(std::string_view text) {
  std::size_t begin = 0;
  while (begin < text.size() && std::isspace(static_cast<unsigned char>(text[begin])) != 0) ++begin;
  std::size_t end = text.size();
  while (end > begin && std::isspace(static_cast<unsigned char>(text[end - 1])) != 0) --end;
  return std::string(text.substr(begin, end - begin));
}

int hex_value(char ch) {
  if (ch >= '0' && ch <= '9') return ch - '0';
  if (ch >= 'a' && ch <= 'f') return ch - 'a' + 10;
  if (ch >= 'A' && ch <= 'F') return ch - 'A' + 10;
  return -1;
}

std::optional<int> parse_hex_code_unit(std::string_view text, std::size_t hex_start) {
  if (hex_start + 3 >= text.size()) return std::nullopt;
  const int a = hex_value(text[hex_start]);
  const int b = hex_value(text[hex_start + 1]);
  const int c = hex_value(text[hex_start + 2]);
  const int d = hex_value(text[hex_start + 3]);
  if (a < 0 || b < 0 || c < 0 || d < 0) return std::nullopt;
  return (a << 12) | (b << 8) | (c << 4) | d;
}

void append_utf8(std::string& out, int codepoint) {
  if (codepoint <= 0x7F) {
    out.push_back(static_cast<char>(codepoint));
  } else if (codepoint <= 0x7FF) {
    out.push_back(static_cast<char>(0xC0 | ((codepoint >> 6) & 0x1F)));
    out.push_back(static_cast<char>(0x80 | (codepoint & 0x3F)));
  } else if (codepoint <= 0xFFFF) {
    out.push_back(static_cast<char>(0xE0 | ((codepoint >> 12) & 0x0F)));
    out.push_back(static_cast<char>(0x80 | ((codepoint >> 6) & 0x3F)));
    out.push_back(static_cast<char>(0x80 | (codepoint & 0x3F)));
  } else {
    out.push_back(static_cast<char>(0xF0 | ((codepoint >> 18) & 0x07)));
    out.push_back(static_cast<char>(0x80 | ((codepoint >> 12) & 0x3F)));
    out.push_back(static_cast<char>(0x80 | ((codepoint >> 6) & 0x3F)));
    out.push_back(static_cast<char>(0x80 | (codepoint & 0x3F)));
  }
}

std::vector<std::string> split_comma_list(std::string_view text) {
  std::vector<std::string> parts;
  std::size_t start = 0;
  while (start <= text.size()) {
    const auto comma = text.find(',', start);
    const auto end = comma == std::string_view::npos ? text.size() : comma;
    auto part = trim(text.substr(start, end - start));
    if (!part.empty()) parts.push_back(std::move(part));
    if (comma == std::string_view::npos) break;
    start = comma + 1;
  }
  return parts;
}

ava::core::Result<std::vector<Key>> parse_key_list(std::string_view text) {
  std::vector<Key> keys;
  for (const auto& part : split_comma_list(text)) {
    const auto key = parse_key_name(part);
    if (!key) {
      auto error = ava::core::Error(ava::core::ErrorCategory::InvalidArgument, "unknown TUI key binding");
      error.with_context("key", part);
      return std::unexpected(std::move(error));
    }
    keys.push_back(*key);
  }
  if (keys.empty()) {
    auto error = ava::core::Error(ava::core::ErrorCategory::InvalidArgument, "empty TUI key binding");
    error.with_context("value", std::string(text));
    return std::unexpected(std::move(error));
  }
  return keys;
}

ava::core::Error keybinds_error(std::string message) {
  return ava::core::Error(ava::core::ErrorCategory::InvalidArgument, std::move(message));
}

void skip_json_whitespace(std::string_view json, std::size_t& offset) {
  while (offset < json.size() && std::isspace(static_cast<unsigned char>(json[offset])) != 0) ++offset;
}

ava::core::Result<std::string> parse_json_key(std::string_view json, std::size_t& offset) {
  if (offset >= json.size() || json[offset] != '"') {
    return std::unexpected(keybinds_error("expected JSON object key"));
  }
  ++offset;
  std::string key;
  while (offset < json.size()) {
    const auto ch = json[offset++];
    if (ch == '"') return key;
    if (static_cast<unsigned char>(ch) < 0x20) {
      return std::unexpected(keybinds_error("invalid control character in JSON object key"));
    }
    if (ch != '\\') {
      key.push_back(ch);
      continue;
    }
    if (offset >= json.size()) return std::unexpected(keybinds_error("unterminated JSON escape"));
    const auto escape = json[offset++];
    if (escape == 'u') {
      const auto code_unit = parse_hex_code_unit(json, offset);
      if (!code_unit) return std::unexpected(keybinds_error("invalid JSON unicode escape"));
      append_utf8(key, *code_unit);
      offset += 4;
      continue;
    }
    switch (escape) {
      case '"':
        key.push_back('"');
        break;
      case '\\':
        key.push_back('\\');
        break;
      case '/':
        key.push_back('/');
        break;
      case 'n':
        key.push_back('\n');
        break;
      case 'r':
        key.push_back('\r');
        break;
      case 't':
        key.push_back('\t');
        break;
      case 'b':
        key.push_back('\b');
        break;
      case 'f':
        key.push_back('\f');
        break;
      default:
        return std::unexpected(keybinds_error("invalid JSON object key escape"));
    }
  }
  return std::unexpected(keybinds_error("unterminated JSON object key"));
}

std::optional<TuiAction> action_from_name(std::string_view name) {
  for (const auto action : kActions) {
    if (action_name(action) == name) return action;
  }
  return std::nullopt;
}

ava::core::Result<std::vector<std::pair<TuiAction, std::string>>> parse_key_binding_entries(std::string_view json) {
  std::vector<std::pair<TuiAction, std::string>> entries;
  std::size_t offset = 0;
  skip_json_whitespace(json, offset);
  if (offset >= json.size() || json[offset] != '{') {
    return std::unexpected(keybinds_error("keybinds.json must be a JSON object"));
  }
  ++offset;
  skip_json_whitespace(json, offset);
  if (offset < json.size() && json[offset] == '}') {
    ++offset;
    skip_json_whitespace(json, offset);
    if (offset != json.size()) return std::unexpected(keybinds_error("unexpected content after keybinds object"));
    return entries;
  }

  while (true) {
    skip_json_whitespace(json, offset);
    auto key = parse_json_key(json, offset);
    if (!key) return std::unexpected(std::move(key.error()));
    const auto action = action_from_name(*key);
    if (!action) {
      auto error = keybinds_error("unknown TUI keybinding action");
      error.with_context("action", *key);
      return std::unexpected(std::move(error));
    }
    skip_json_whitespace(json, offset);
    if (offset >= json.size() || json[offset] != ':') {
      return std::unexpected(keybinds_error("expected ':' after keybinds action"));
    }
    ++offset;
    skip_json_whitespace(json, offset);
    if (offset >= json.size() || json[offset] != '"') {
      auto error = keybinds_error("TUI keybinding values must be strings");
      error.with_context("action", *key);
      return std::unexpected(std::move(error));
    }
    auto value = parse_json_key(json, offset);
    if (!value) return std::unexpected(std::move(value.error()));
    entries.push_back({*action, std::move(*value)});
    skip_json_whitespace(json, offset);
    if (offset >= json.size()) return std::unexpected(keybinds_error("unterminated keybinds object"));
    if (json[offset] == ',') {
      ++offset;
      continue;
    }
    if (json[offset] == '}') {
      ++offset;
      skip_json_whitespace(json, offset);
      if (offset != json.size()) return std::unexpected(keybinds_error("unexpected content after keybinds object"));
      return entries;
    }
    return std::unexpected(keybinds_error("expected ',' or '}' in keybinds object"));
  }
}

std::optional<std::vector<Key>*> keys_for_action(TuiKeyBindings& bindings, TuiAction action) {
  for (auto& [candidate, keys] : bindings.bindings) {
    if (candidate == action) return &keys;
  }
  return std::nullopt;
}

void remove_keys_from_other_actions(TuiKeyBindings& bindings, TuiAction action,
                                    const std::vector<Key>& keys_to_remove) {
  for (auto& [candidate, keys] : bindings.bindings) {
    if (candidate == action) continue;
    std::erase_if(keys, [&](Key key) { return std::ranges::find(keys_to_remove, key) != keys_to_remove.end(); });
  }
}

}  // namespace

TuiKeyBindings default_key_bindings() {
  return TuiKeyBindings{.bindings = {{TuiAction::Submit, {Key::Enter}},
                                     {TuiAction::NewLine, {Key::ShiftEnter}},
                                     {TuiAction::Cancel, {Key::Escape}},
                                     {TuiAction::ClearInput, {}},
                                     {TuiAction::DeleteBackward, {Key::Backspace}},
                                     {TuiAction::HistoryPrev, {Key::ArrowUp}},
                                     {TuiAction::HistoryNext, {Key::ArrowDown}},
                                     {TuiAction::PalettePrev, {Key::ArrowUp}},
                                     {TuiAction::PaletteNext, {Key::ArrowDown}},
                                     {TuiAction::CursorLeft, {Key::ArrowLeft, Key::CtrlB}},
                                     {TuiAction::CursorRight, {Key::ArrowRight, Key::CtrlF}},
                                     {TuiAction::CursorLineStart, {Key::CtrlA}},
                                     {TuiAction::CursorLineEnd, {Key::CtrlE}},
                                     {TuiAction::CursorWordLeft, {}},
                                     {TuiAction::CursorWordRight, {}},
                                     {TuiAction::DeleteWordBackward, {Key::CtrlW}},
                                     {TuiAction::DeleteToLineStart, {Key::CtrlU}},
                                     {TuiAction::DeleteToLineEnd, {Key::CtrlK}},
                                     {TuiAction::Undo, {Key::CtrlZ}},
                                     {TuiAction::Yank, {Key::CtrlY}},
                                     {TuiAction::AutocompleteAccept, {Key::Tab}},
                                     {TuiAction::PromptAllow, {}},
                                     {TuiAction::PromptDeny, {}},
                                     {TuiAction::DetailsToggle, {}},
                                     {TuiAction::PageUp, {Key::PageUp}},
                                     {TuiAction::PageDown, {Key::PageDown}},
                                     {TuiAction::ModeToggle, {Key::Tab}},
                                     {TuiAction::Interrupt, {Key::CtrlC}},
                                     {TuiAction::Exit, {Key::CtrlD}},
                                     {TuiAction::VariantCycle, {Key::CtrlT}}}};
}

std::optional<TuiAction> action_for_key(const TuiKeyBindings& bindings, Key key) {
  for (const auto& [action, keys] : bindings.bindings) {
    if (std::ranges::find(keys, key) != keys.end()) return action;
  }
  return std::nullopt;
}

bool key_matches_action(const TuiKeyBindings& bindings, TuiAction action, Key key) {
  for (const auto& [candidate, keys] : bindings.bindings) {
    if (candidate == action && std::ranges::find(keys, key) != keys.end()) return true;
  }
  return false;
}

std::optional<Key> parse_key_name(std::string_view text) {
  const auto normalized = normalize_token(text);
  if (normalized == "enter" || normalized == "return") return Key::Enter;
  if (normalized == "shift+enter" || normalized == "ctrl+j" || normalized == "ctrlj") return Key::ShiftEnter;
  if (normalized == "backspace" || normalized == "bs") return Key::Backspace;
  if (normalized == "tab") return Key::Tab;
  if (normalized == "esc" || normalized == "escape") return Key::Escape;
  if (normalized == "arrowup" || normalized == "up") return Key::ArrowUp;
  if (normalized == "arrowdown" || normalized == "down") return Key::ArrowDown;
  if (normalized == "arrowleft" || normalized == "left") return Key::ArrowLeft;
  if (normalized == "arrowright" || normalized == "right") return Key::ArrowRight;
  if (normalized == "pageup" || normalized == "pgup") return Key::PageUp;
  if (normalized == "pagedown" || normalized == "pgdown") return Key::PageDown;
  if (normalized == "ctrl+a" || normalized == "ctrla") return Key::CtrlA;
  if (normalized == "ctrl+b" || normalized == "ctrlb") return Key::CtrlB;
  if (normalized == "ctrl+c" || normalized == "ctrlc") return Key::CtrlC;
  if (normalized == "ctrl+d" || normalized == "ctrld") return Key::CtrlD;
  if (normalized == "ctrl+e" || normalized == "ctrle") return Key::CtrlE;
  if (normalized == "ctrl+f" || normalized == "ctrlf") return Key::CtrlF;
  if (normalized == "ctrl+k" || normalized == "ctrlk") return Key::CtrlK;
  if (normalized == "ctrl+t" || normalized == "ctrlt") return Key::CtrlT;
  if (normalized == "ctrl+u" || normalized == "ctrlu") return Key::CtrlU;
  if (normalized == "ctrl+w" || normalized == "ctrlw") return Key::CtrlW;
  if (normalized == "ctrl+y" || normalized == "ctrly") return Key::CtrlY;
  if (normalized == "ctrl+z" || normalized == "ctrlz") return Key::CtrlZ;
  return std::nullopt;
}

std::string key_display(Key key) {
  switch (key) {
    case Key::Enter:
      return "Enter";
    case Key::Backspace:
      return "Backspace";
    case Key::Tab:
      return "Tab";
    case Key::Escape:
      return "Esc";
    case Key::ArrowUp:
      return "Up";
    case Key::ArrowDown:
      return "Down";
    case Key::ArrowLeft:
      return "Left";
    case Key::ArrowRight:
      return "Right";
    case Key::PageUp:
      return "PageUp";
    case Key::PageDown:
      return "PageDown";
    case Key::ShiftEnter:
      return "Shift+Enter";
    case Key::CtrlA:
      return "Ctrl+A";
    case Key::CtrlB:
      return "Ctrl+B";
    case Key::CtrlC:
      return "Ctrl+C";
    case Key::CtrlD:
      return "Ctrl+D";
    case Key::CtrlE:
      return "Ctrl+E";
    case Key::CtrlF:
      return "Ctrl+F";
    case Key::CtrlK:
      return "Ctrl+K";
    case Key::CtrlT:
      return "Ctrl+T";
    case Key::CtrlU:
      return "Ctrl+U";
    case Key::CtrlW:
      return "Ctrl+W";
    case Key::CtrlY:
      return "Ctrl+Y";
    case Key::CtrlZ:
      return "Ctrl+Z";
    case Key::MouseWheelUp:
      return "MouseWheelUp";
    case Key::MouseWheelDown:
      return "MouseWheelDown";
    case Key::MouseLeftClick:
      return "MouseLeftClick";
    case Key::Character:
    case Key::Unknown:
      return "";
  }
  return "";
}

std::string action_name(TuiAction action) {
  switch (action) {
    case TuiAction::Submit:
      return "submit";
    case TuiAction::NewLine:
      return "new_line";
    case TuiAction::Cancel:
      return "cancel";
    case TuiAction::ClearInput:
      return "clear_input";
    case TuiAction::DeleteBackward:
      return "delete_backward";
    case TuiAction::HistoryPrev:
      return "history_prev";
    case TuiAction::HistoryNext:
      return "history_next";
    case TuiAction::PalettePrev:
      return "palette_prev";
    case TuiAction::PaletteNext:
      return "palette_next";
    case TuiAction::CursorLeft:
      return "cursor_left";
    case TuiAction::CursorRight:
      return "cursor_right";
    case TuiAction::CursorLineStart:
      return "cursor_line_start";
    case TuiAction::CursorLineEnd:
      return "cursor_line_end";
    case TuiAction::CursorWordLeft:
      return "cursor_word_left";
    case TuiAction::CursorWordRight:
      return "cursor_word_right";
    case TuiAction::DeleteWordBackward:
      return "delete_word_backward";
    case TuiAction::DeleteToLineStart:
      return "delete_to_line_start";
    case TuiAction::DeleteToLineEnd:
      return "delete_to_line_end";
    case TuiAction::Undo:
      return "undo";
    case TuiAction::Yank:
      return "yank";
    case TuiAction::AutocompleteAccept:
      return "autocomplete_accept";
    case TuiAction::PromptAllow:
      return "prompt_allow";
    case TuiAction::PromptDeny:
      return "prompt_deny";
    case TuiAction::DetailsToggle:
      return "details_toggle";
    case TuiAction::PageUp:
      return "page_up";
    case TuiAction::PageDown:
      return "page_down";
    case TuiAction::ModeToggle:
      return "mode_toggle";
    case TuiAction::Interrupt:
      return "interrupt";
    case TuiAction::Exit:
      return "exit";
    case TuiAction::VariantCycle:
      return "variant_cycle";
  }
  return "unknown";
}

std::string action_description(TuiAction action) {
  switch (action) {
    case TuiAction::Submit:
      return "Submit input or select the highlighted slash command";
    case TuiAction::NewLine:
      return "Insert a newline in the composer";
    case TuiAction::Cancel:
      return "Dismiss slash suggestions or clear composer input";
    case TuiAction::ClearInput:
      return "Clear the current composer input";
    case TuiAction::DeleteBackward:
      return "Delete the previous character";
    case TuiAction::HistoryPrev:
      return "Recall the previous input history item, or scroll up when history is empty";
    case TuiAction::HistoryNext:
      return "Recall the next input history item, or scroll down when not browsing history";
    case TuiAction::PalettePrev:
      return "Move to the previous slash palette item";
    case TuiAction::PaletteNext:
      return "Move to the next slash palette item";
    case TuiAction::CursorLeft:
      return "Move the input cursor left";
    case TuiAction::CursorRight:
      return "Move the input cursor right";
    case TuiAction::CursorLineStart:
      return "Move the input cursor to the start of the current line";
    case TuiAction::CursorLineEnd:
      return "Move the input cursor to the end of the current line";
    case TuiAction::CursorWordLeft:
      return "Move the input cursor to the previous word";
    case TuiAction::CursorWordRight:
      return "Move the input cursor to the next word";
    case TuiAction::DeleteWordBackward:
      return "Delete the word before the cursor";
    case TuiAction::DeleteToLineStart:
      return "Delete from the cursor to the start of the current line";
    case TuiAction::DeleteToLineEnd:
      return "Delete from the cursor to the end of the current line";
    case TuiAction::Undo:
      return "Undo the last composer edit";
    case TuiAction::Yank:
      return "Paste the last killed composer text";
    case TuiAction::AutocompleteAccept:
      return "Accept the highlighted slash command suggestion";
    case TuiAction::PromptAllow:
      return "Allow the active permission prompt when prompt keybind support exists";
    case TuiAction::PromptDeny:
      return "Deny or cancel the active prompt";
    case TuiAction::DetailsToggle:
      return "Toggle details for the focused prompt or tool when available";
    case TuiAction::PageUp:
      return "Scroll the transcript up by half a page";
    case TuiAction::PageDown:
      return "Scroll the transcript down by half a page";
    case TuiAction::ModeToggle:
      return "Toggle build/plan mode when the slash palette is not active";
    case TuiAction::Interrupt:
      return "Clear a non-empty composer draft, otherwise exit the current TUI loop";
    case TuiAction::Exit:
      return "Exit the TUI";
    case TuiAction::VariantCycle:
      return "Cycle model/variant choices when that backend support exists";
  }
  return "Unknown action";
}

std::string keys_display(const TuiKeyBindings& bindings, TuiAction action) {
  for (const auto& [candidate, keys] : bindings.bindings) {
    if (candidate != action) continue;
    std::string text;
    for (const auto key : keys) {
      auto display = key_display(key);
      if (display.empty()) continue;
      if (!text.empty()) text += ", ";
      text += display;
    }
    return text;
  }
  return "";
}

std::vector<TuiKeyBindingHelpItem> key_binding_help_items(const TuiKeyBindings& bindings) {
  std::vector<TuiKeyBindingHelpItem> items;
  items.reserve(bindings.bindings.size());
  for (const auto& [action, keys] : bindings.bindings) {
    static_cast<void>(keys);
    auto keys_text = keys_display(bindings, action);
    if (keys_text.empty()) continue;
    items.push_back(TuiKeyBindingHelpItem{
        .action = action_name(action), .description = action_description(action), .keys = std::move(keys_text)});
  }
  return items;
}

ava::core::Result<TuiKeyBindings> parse_key_bindings_json(std::string_view json) {
  return parse_key_bindings_json(json, default_key_bindings());
}

ava::core::Result<TuiKeyBindings> parse_key_bindings_json(std::string_view json, TuiKeyBindings base) {
  auto entries = parse_key_binding_entries(json);
  if (!entries) return std::unexpected(std::move(entries.error()));

  for (const auto& [action, value] : *entries) {
    auto keys = parse_key_list(value);
    if (!keys) return std::unexpected(std::move(keys.error()));
    remove_keys_from_other_actions(base, action, *keys);
    auto target = keys_for_action(base, action);
    if (!target) {
      base.bindings.push_back({action, std::move(*keys)});
    } else {
      **target = std::move(*keys);
    }
  }

  return base;
}

ava::core::Result<TuiKeyBindings> load_key_bindings(const std::filesystem::path& keybinds_file) {
  std::error_code exists_error;
  if (!std::filesystem::exists(keybinds_file, exists_error)) return default_key_bindings();
  if (exists_error) {
    auto error = ava::core::Error(ava::core::ErrorCategory::Io, "failed to inspect TUI keybinds file");
    error.with_context("path", keybinds_file.string()).with_context("cause", exists_error.message());
    return std::unexpected(std::move(error));
  }

  std::ifstream input(keybinds_file, std::ios::binary);
  if (!input) {
    auto error = ava::core::Error(ava::core::ErrorCategory::Io, "failed to read TUI keybinds file");
    error.with_context("path", keybinds_file.string());
    return std::unexpected(std::move(error));
  }
  const std::string content((std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>());
  auto parsed = parse_key_bindings_json(content);
  if (!parsed) parsed.error().with_context("path", keybinds_file.string());
  return parsed;
}

}  // namespace ava::tui
