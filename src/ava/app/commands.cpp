#include "sys.h"
#include "ava/app/command_connect.h"
#include "ava/app/command_format.h"
#include "ava/app/command_help.h"
#include "ava/app/command_jobs.h"
#include "ava/app/command_mcp.h"
#include "ava/app/command_models.h"
#include "ava/app/command_permissions.h"
#include "ava/app/command_plugins.h"
#include "ava/app/command_registry.h"
#include "ava/app/command_sessions.h"
#include "ava/app/command_tools.h"
#include "ava/app/commands.h"
#include "ava/app/display_settings.h"
#include "ava/app/plugin_event_hooks.h"
#include "ava/app/project_trust.h"
#include "ava/app/runtime_prompt.h"
#include "ava/app/runtime/Session.h"
#include "ava/tools/file_tools.h"
#include "ava/tui/keybindings.h"
#include "ava/tui/theme.h"
#include "ava/plugin/diagnostics.h"
#include "ava/plugin/static_resources.h"
#include "ava/mcp/config.h"
#include "ava/mcp/stdio_client.h"
#include "ava/session/compaction.h"
#include "ava/permissions/permission.h"
#include "ava/context/skill_loader.h"
#include "ava/core/atomic_file.h"
#include "ava/core/json.h"

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

namespace ava::app {
namespace {

constexpr auto kMaxKeybindingsImportBytes = std::uintmax_t{256 * 1024};

struct JsonObjectEntry
{
  std::string key;
  std::string raw_key;
  std::string raw_value;
};

bool starts_with_command(std::string_view line, std::string_view command) noexcept
{
  return line == command || (line.starts_with(command) && line.size() > command.size() && line[command.size()] == ' ');
}

bool is_json_whitespace(char ch) noexcept
{
  return ch == ' ' || ch == '\t' || ch == '\n' || ch == '\r';
}

void skip_json_whitespace(std::string_view text, std::size_t& offset) noexcept
{
  while (offset < text.size() && is_json_whitespace(text[offset])) ++offset;
}

std::string trim_copy(std::string_view text)
{
  std::size_t begin = 0;
  while (begin < text.size() && std::isspace(static_cast<unsigned char>(text[begin])) != 0) ++begin;
  std::size_t end = text.size();
  while (end > begin && std::isspace(static_cast<unsigned char>(text[end - 1])) != 0) --end;
  return std::string(text.substr(begin, end - begin));
}

std::optional<std::size_t> json_string_literal_end(std::string_view text, std::size_t start) noexcept
{
  if (start >= text.size() || text[start] != '"')
    return std::nullopt;
  bool escaped = false;
  for (std::size_t index = start + 1; index < text.size(); ++index)
  {
    auto const ch = text[index];
    if (escaped)
    {
      escaped = false;
      continue;
    }
    if (ch == '\\')
    {
      escaped = true;
      continue;
    }
    if (ch == '"')
      return index;
  }
  return std::nullopt;
}

std::optional<std::size_t> json_balanced_value_end(std::string_view text, std::size_t start)
{
  if (start >= text.size() || (text[start] != '{' && text[start] != '['))
    return std::nullopt;
  std::vector<char> expected_closers;
  bool in_string = false;
  bool escaped = false;
  for (std::size_t index = start; index < text.size(); ++index)
  {
    auto const ch = text[index];
    if (in_string)
    {
      if (escaped)
      {
        escaped = false;
        continue;
      }
      if (ch == '\\')
      {
        escaped = true;
        continue;
      }
      if (ch == '"')
        in_string = false;
      continue;
    }
    if (ch == '"')
    {
      in_string = true;
      continue;
    }
    if (ch == '{')
    {
      expected_closers.push_back('}');
      continue;
    }
    if (ch == '[')
    {
      expected_closers.push_back(']');
      continue;
    }
    if (ch == '}' || ch == ']')
    {
      if (expected_closers.empty() || expected_closers.back() != ch)
        return std::nullopt;
      expected_closers.pop_back();
      if (expected_closers.empty())
        return index + 1;
    }
  }
  return std::nullopt;
}

std::optional<std::size_t> json_value_end(std::string_view text, std::size_t start)
{
  if (start >= text.size())
    return std::nullopt;
  if (text[start] == '"')
  {
    auto const end = json_string_literal_end(text, start);
    if (!end)
      return std::nullopt;
    return *end + 1;
  }
  if (text[start] == '{' || text[start] == '[')
    return json_balanced_value_end(text, start);

  auto end = start;
  while (end < text.size() && text[end] != ',' && text[end] != '}') ++end;
  while (end > start && is_json_whitespace(text[end - 1])) --end;
  return end > start ? std::optional<std::size_t>(end) : std::nullopt;
}

std::optional<std::vector<JsonObjectEntry>> top_level_json_object_entries(std::string_view object)
{
  if (!ava::core::json::is_valid_object(object))
    return std::nullopt;

  std::vector<JsonObjectEntry> entries;
  std::size_t offset = 0;
  skip_json_whitespace(object, offset);
  if (offset >= object.size() || object[offset] != '{')
    return std::nullopt;
  ++offset;
  skip_json_whitespace(object, offset);
  if (offset < object.size() && object[offset] == '}')
    return entries;

  while (offset < object.size())
  {
    skip_json_whitespace(object, offset);
    auto const key_start = offset;
    auto const key_end = json_string_literal_end(object, key_start);
    if (!key_end)
      return std::nullopt;
    auto raw_key = std::string(object.substr(key_start, *key_end - key_start + 1));
    auto const decoded_key = ava::core::json::string_field("{\"value\":" + raw_key + "}", "value");
    if (!decoded_key)
      return std::nullopt;
    offset = *key_end + 1;
    skip_json_whitespace(object, offset);
    if (offset >= object.size() || object[offset] != ':')
      return std::nullopt;
    ++offset;
    skip_json_whitespace(object, offset);
    auto const value_start = offset;
    auto const value_end = json_value_end(object, value_start);
    if (!value_end)
      return std::nullopt;
    auto raw_value = std::string(object.substr(value_start, *value_end - value_start));
    entries.push_back(JsonObjectEntry{.key = *decoded_key, .raw_key = std::move(raw_key), .raw_value = std::move(raw_value)});
    offset = *value_end;
    skip_json_whitespace(object, offset);
    if (offset < object.size() && object[offset] == ',')
    {
      ++offset;
      continue;
    }
    if (offset < object.size() && object[offset] == '}')
      return entries;
    return std::nullopt;
  }
  return std::nullopt;
}

std::vector<std::string> split_keybinding_tokens(std::vector<std::string> const& args, std::size_t first)
{
  std::vector<std::string> tokens;
  for (std::size_t index = first; index < args.size(); ++index)
  {
    std::string_view text = args[index];
    std::size_t start = 0;
    while (start <= text.size())
    {
      auto const comma = text.find(',', start);
      auto const end = comma == std::string_view::npos ? text.size() : comma;
      auto token = trim_copy(text.substr(start, end - start));
      if (!token.empty())
        tokens.push_back(std::move(token));
      if (comma == std::string_view::npos)
        break;
      start = comma + 1;
    }
  }
  return tokens;
}

std::optional<std::vector<std::string>> keybinding_key_displays(std::vector<std::string> const& args, std::size_t first, std::string& error)
{
  auto tokens = split_keybinding_tokens(args, first);
  if (tokens.empty())
  {
    error = "missing keybinding key";
    return std::nullopt;
  }

  std::vector<std::string> displays;
  for (auto const& token : tokens)
  {
    auto const key = ava::tui::parse_key_name(token);
    if (!key)
    {
      error = "unknown TUI key binding:\n  key: " + token;
      return std::nullopt;
    }
    auto display = ava::tui::key_display(*key);
    if (display.empty())
    {
      error = "unsupported TUI key binding:\n  key: " + token;
      return std::nullopt;
    }
    if (std::ranges::find(displays, display) == displays.end())
      displays.push_back(std::move(display));
  }
  if (displays.empty())
  {
    error = "missing keybinding key";
    return std::nullopt;
  }
  return displays;
}

std::string keybinding_json_value(std::vector<std::string> const& key_displays)
{
  if (key_displays.size() == 1)
    return "\"" + ava::core::json::escape(key_displays.front()) + "\"";

  std::string output = "[";
  for (std::size_t index = 0; index < key_displays.size(); ++index)
  {
    if (index > 0)
      output += ", ";
    output += "\"" + ava::core::json::escape(key_displays[index]) + "\"";
  }
  output += "]";
  return output;
}

bool keybinding_entry_matches_action(JsonObjectEntry const& entry, ava::tui::TuiAction action)
{
  auto const entry_action = ava::tui::key_binding_action_from_name(entry.key);
  return entry_action && *entry_action == action;
}

std::string render_keybinding_object_setting_action(std::vector<JsonObjectEntry> const& entries, ava::tui::TuiAction action, std::string_view raw_key,
                                                    std::string_view raw_value)
{
  std::string output = "{\n";
  bool first = true;
  bool replaced = false;
  auto append_entry = [&](std::string_view key, std::string_view value) {
    if (!first)
      output += ",\n";
    first = false;
    output += "  ";
    output += key;
    output += ": ";
    output += value;
  };

  for (auto const& entry : entries)
  {
    if (keybinding_entry_matches_action(entry, action))
    {
      if (!replaced)
      {
        append_entry(raw_key, raw_value);
        replaced = true;
      }
      continue;
    }
    append_entry(entry.raw_key, entry.raw_value);
  }
  if (!replaced)
    append_entry(raw_key, raw_value);
  output += "\n}\n";
  return output;
}

std::string render_keybinding_object_without_action(std::vector<JsonObjectEntry> const& entries, ava::tui::TuiAction action)
{
  std::string output = "{\n";
  bool first = true;
  for (auto const& entry : entries)
  {
    if (keybinding_entry_matches_action(entry, action))
      continue;
    if (!first)
      output += ",\n";
    first = false;
    output += "  ";
    output += entry.raw_key;
    output += ": ";
    output += entry.raw_value;
  }
  output += "\n}\n";
  return output;
}

bool keybinding_object_has_action(std::vector<JsonObjectEntry> const& entries, ava::tui::TuiAction action)
{
  return std::ranges::any_of(entries, [action](auto const& entry) { return keybinding_entry_matches_action(entry, action); });
}

std::string join_display_list(std::vector<std::string> const& values)
{
  std::string output;
  for (std::size_t index = 0; index < values.size(); ++index)
  {
    if (index > 0)
      output += ", ";
    output += values[index];
  }
  return output;
}

std::string_view command_token(std::string_view line) noexcept
{
  auto const end = line.find_first_of(" \t\r\n");
  return line.substr(0, end == std::string_view::npos ? line.size() : end);
}

bool is_shell_helper_command(std::string_view line) noexcept
{
  return line.starts_with('!');
}

std::string shell_helper_argument(std::string_view line)
{
  if (!is_shell_helper_command(line))
    return {};
  line.remove_prefix(line.starts_with("!!") ? 2 : 1);
  while (!line.empty() && (line.front() == ' ' || line.front() == '\t')) line.remove_prefix(1);
  return std::string(line);
}

CommandResult handled_text(std::string text)
{
  CommandResult result;
  result.handled = true;
  add_output(result, std::move(text));
  return result;
}

CommandResult handled_prompt(std::string command, std::string source, std::string message)
{
  CommandResult result;
  result.handled = true;
  result.prompt_command = std::move(command);
  result.prompt_source = std::move(source);
  result.prompt_message = std::move(message);
  return result;
}

CommandResult run_keybindings_command(runtime::Session& session, std::string_view argument, std::vector<CommandHotkey> const& hotkeys)
{
  auto const args = split_command_arguments(argument);
  if (args.empty())
    return handled_text(command_hotkeys_text(hotkeys));

  auto const keybinds_file = session.paths.ava_config_dir / "keybinds.json";
  if (args.front() == "set")
  {
    if (args.size() < 3)
      return handled_text(missing_argument("/keybindings set <action> <key>[,<key>...]"));

    auto const action = args[1];
    auto const resolved_action = ava::tui::key_binding_action_from_name(action);
    if (!resolved_action)
    {
      return handled_text("keybindings assignment is invalid:\ninvalid_argument: unknown TUI keybinding action\n  action: " + action +
                          "\nTarget was not changed.");
    }
    std::string key_error;
    auto const key_displays = keybinding_key_displays(args, 2, key_error);
    if (!key_displays)
      return handled_text(key_error + "\nusage: /keybindings set <action> <key>[,<key>...]");

    auto const canonical_action = ava::tui::key_binding_config_action_id(*resolved_action);
    auto const raw_key = "\"" + ava::core::json::escape(canonical_action) + "\"";
    auto const raw_value = keybinding_json_value(*key_displays);
    if (auto parsed = ava::tui::parse_key_bindings_json("{" + raw_key + ":" + raw_value + "}"); !parsed)
    {
      return handled_text("keybindings assignment is invalid:\n" + parsed.error().format() + "\nTarget was not changed.");
    }

    std::string content = "{}\n";
    std::error_code exists_error;
    auto const exists = std::filesystem::exists(keybinds_file, exists_error);
    if (exists_error)
    {
      return handled_text("failed to inspect keybindings file: " + keybinds_file.string() + "\n  cause: " + exists_error.message());
    }
    if (exists)
    {
      std::error_code regular_error;
      auto const regular = std::filesystem::is_regular_file(keybinds_file, regular_error);
      if (regular_error)
      {
        return handled_text("failed to inspect keybindings file: " + keybinds_file.string() + "\n  cause: " + regular_error.message());
      }
      if (!regular)
        return handled_text("keybindings file is not a regular file:\n  " + keybinds_file.string());

      std::error_code size_error;
      auto const config_bytes = std::filesystem::file_size(keybinds_file, size_error);
      if (size_error)
      {
        return handled_text("failed to size keybindings file: " + keybinds_file.string() + "\n  cause: " + size_error.message());
      }
      if (config_bytes > kMaxKeybindingsImportBytes)
      {
        return handled_text("keybindings file is too large to edit safely:\n  " + keybinds_file.string() +
                            "\n  limit: " + std::to_string(kMaxKeybindingsImportBytes) + " bytes");
      }

      content.assign(static_cast<std::size_t>(config_bytes), '\0');
      std::ifstream input(keybinds_file, std::ios::binary);
      if (!input)
        return handled_text("failed to read keybindings file:\n  " + keybinds_file.string());
      if (!content.empty())
        input.read(content.data(), static_cast<std::streamsize>(content.size()));
      if (!input && static_cast<std::uintmax_t>(input.gcount()) != config_bytes)
      {
        return handled_text("failed to finish reading keybindings file:\n  " + keybinds_file.string());
      }
      if (trim_copy(content).empty())
        content = "{}\n";
    }

    auto const entries = top_level_json_object_entries(content);
    if (!entries)
    {
      return handled_text("keybindings file is not a valid JSON object:\n  " + keybinds_file.string() + "\nTarget was not changed.");
    }

    auto const candidate = render_keybinding_object_setting_action(*entries, *resolved_action, raw_key, raw_value);
    if (auto parsed = ava::tui::parse_key_bindings_json(candidate); !parsed)
    {
      return handled_text("keybindings assignment is invalid:\n" + parsed.error().format() + "\nTarget was not changed.");
    }

    if (auto written = ava::core::write_text_file_atomic(keybinds_file, candidate, "keybindings file"); !written)
      return handled_text(written.error().format());

    if (auto loaded = ava::tui::load_key_bindings(keybinds_file); !loaded)
    {
      return handled_text("wrote keybindings file, but validation failed:\n" + loaded.error().format());
    }

    return handled_text("Set keybinding:\n  action: " + action + "\n  keys: " + join_display_list(*key_displays) + "\n  target: " + keybinds_file.string() +
                        "\nRun /reload keybindings inside the interactive TUI to apply it.");
  }

  if (args.front() == "reset" || args.front() == "unset")
  {
    if (args.size() != 2)
      return handled_text(missing_argument("/keybindings reset <action>"));

    auto const action = args[1];
    auto const resolved_action = ava::tui::key_binding_action_from_name(action);
    if (!resolved_action)
    {
      return handled_text("keybindings reset target is invalid:\ninvalid_argument: unknown TUI keybinding action\n  action: " + action +
                          "\nTarget was not changed.");
    }

    std::error_code exists_error;
    auto const exists = std::filesystem::exists(keybinds_file, exists_error);
    if (exists_error)
    {
      return handled_text("failed to inspect keybindings file: " + keybinds_file.string() + "\n  cause: " + exists_error.message());
    }
    if (!exists)
    {
      return handled_text("No keybindings file found:\n  " + keybinds_file.string() + "\nNo override was reset for action: " + action);
    }

    std::error_code regular_error;
    auto const regular = std::filesystem::is_regular_file(keybinds_file, regular_error);
    if (regular_error)
    {
      return handled_text("failed to inspect keybindings file: " + keybinds_file.string() + "\n  cause: " + regular_error.message());
    }
    if (!regular)
      return handled_text("keybindings file is not a regular file:\n  " + keybinds_file.string());

    std::error_code size_error;
    auto const config_bytes = std::filesystem::file_size(keybinds_file, size_error);
    if (size_error)
    {
      return handled_text("failed to size keybindings file: " + keybinds_file.string() + "\n  cause: " + size_error.message());
    }
    if (config_bytes > kMaxKeybindingsImportBytes)
    {
      return handled_text("keybindings file is too large to edit safely:\n  " + keybinds_file.string() +
                          "\n  limit: " + std::to_string(kMaxKeybindingsImportBytes) + " bytes");
    }

    std::string content(static_cast<std::size_t>(config_bytes), '\0');
    {
      std::ifstream input(keybinds_file, std::ios::binary);
      if (!input)
        return handled_text("failed to read keybindings file:\n  " + keybinds_file.string());
      if (!content.empty())
        input.read(content.data(), static_cast<std::streamsize>(content.size()));
      if (!input && static_cast<std::uintmax_t>(input.gcount()) != config_bytes)
      {
        return handled_text("failed to finish reading keybindings file:\n  " + keybinds_file.string());
      }
    }
    if (trim_copy(content).empty())
      content = "{}\n";

    auto const entries = top_level_json_object_entries(content);
    if (!entries)
    {
      return handled_text("keybindings file is not a valid JSON object:\n  " + keybinds_file.string() + "\nTarget was not changed.");
    }
    if (!keybinding_object_has_action(*entries, *resolved_action))
    {
      return handled_text("No keybinding override found:\n  action: " + action + "\n  target: " + keybinds_file.string() + "\nTarget was not changed.");
    }

    auto const candidate = render_keybinding_object_without_action(*entries, *resolved_action);
    if (auto parsed = ava::tui::parse_key_bindings_json(candidate); !parsed)
    {
      return handled_text("keybindings reset is invalid:\n" + parsed.error().format() + "\nTarget was not changed.");
    }

    if (auto written = ava::core::write_text_file_atomic(keybinds_file, candidate, "keybindings file"); !written)
      return handled_text(written.error().format());

    if (auto loaded = ava::tui::load_key_bindings(keybinds_file); !loaded)
    {
      return handled_text("wrote keybindings file, but validation failed:\n" + loaded.error().format());
    }

    return handled_text("Reset keybinding override:\n  action: " + action + "\n  target: " + keybinds_file.string() +
                        "\nRun /reload keybindings inside the interactive TUI to apply it.");
  }

  if (args.front() == "import")
  {
    if (args.size() < 2)
      return handled_text(missing_argument("/keybindings import <path> [--force]"));
    bool force = false;
    for (std::size_t index = 2; index < args.size(); ++index)
    {
      if (args[index] == "--force")
      {
        force = true;
        continue;
      }
      return handled_text("unsupported keybindings import option: " + args[index] + "\nsupported: --force");
    }

    auto import_path = std::filesystem::path(args[1]);
    if (import_path.is_relative())
      import_path = session.current_dir / import_path;
    import_path = import_path.lexically_normal();

    std::error_code source_error;
    auto const source_exists = std::filesystem::exists(import_path, source_error);
    if (source_error)
    {
      return handled_text("failed to inspect keybindings import source: " + import_path.string() + "\n  cause: " + source_error.message());
    }
    if (!source_exists)
    {
      return handled_text("keybindings import source does not exist:\n  " + import_path.string());
    }
    auto const regular = std::filesystem::is_regular_file(import_path, source_error);
    if (source_error)
    {
      return handled_text("failed to inspect keybindings import source: " + import_path.string() + "\n  cause: " + source_error.message());
    }
    if (!regular)
    {
      return handled_text("keybindings import source is not a regular file:\n  " + import_path.string());
    }
    auto const import_bytes = std::filesystem::file_size(import_path, source_error);
    if (source_error)
    {
      return handled_text("failed to size keybindings import source: " + import_path.string() + "\n  cause: " + source_error.message());
    }
    if (import_bytes > kMaxKeybindingsImportBytes)
    {
      return handled_text("keybindings import source is too large:\n  " + import_path.string() + "\n  limit: " + std::to_string(kMaxKeybindingsImportBytes) +
                          " bytes");
    }

    if (auto loaded = ava::tui::load_key_bindings(import_path); !loaded)
    {
      return handled_text("keybindings import source is invalid:\n" + loaded.error().format() + "\nTarget was not changed.");
    }

    std::error_code exists_error;
    auto const target_exists = std::filesystem::exists(keybinds_file, exists_error);
    if (exists_error)
    {
      return handled_text("failed to inspect keybindings file: " + keybinds_file.string() + "\n  cause: " + exists_error.message());
    }
    if (target_exists && !force)
    {
      return handled_text("keybindings file already exists:\n  " + keybinds_file.string() +
                          "\nUse /keybindings import <path> --force to replace it explicitly.");
    }

    std::string content(static_cast<std::size_t>(import_bytes), '\0');
    {
      std::ifstream input(import_path, std::ios::binary);
      if (!input)
        return handled_text("failed to read keybindings import source:\n  " + import_path.string());
      if (!content.empty())
        input.read(content.data(), static_cast<std::streamsize>(content.size()));
      if (!input && static_cast<std::uintmax_t>(input.gcount()) != import_bytes)
      {
        return handled_text("failed to finish reading keybindings import source:\n  " + import_path.string());
      }
    }

    if (auto written = ava::core::write_text_file_atomic(keybinds_file, content, "keybindings file"); !written)
      return handled_text(written.error().format());

    if (auto loaded = ava::tui::load_key_bindings(keybinds_file); !loaded)
    {
      return handled_text("imported keybindings file, but validation failed:\n" + loaded.error().format());
    }

    return handled_text("Imported keybindings file:\n  source: " + import_path.string() + "\n  target: " + keybinds_file.string() +
                        "\nRun /reload keybindings inside the interactive TUI to apply it.");
  }

  if (args.front() == "validate")
  {
    if (args.size() > 1)
    {
      return handled_text("unsupported keybindings validate option: " + args[1] + "\nsupported: no options");
    }
    std::error_code exists_error;
    auto const exists = std::filesystem::exists(keybinds_file, exists_error);
    if (exists_error)
    {
      return handled_text("failed to inspect keybindings file: " + keybinds_file.string() + "\n  cause: " + exists_error.message());
    }
    if (!exists)
    {
      return handled_text("No keybindings file found:\n  " + keybinds_file.string() +
                          "\nAVA is using built-in defaults. Run /keybindings init to create a starter file.");
    }
    auto loaded = ava::tui::load_key_bindings(keybinds_file);
    if (!loaded)
    {
      return handled_text("keybindings file is invalid:\n" + loaded.error().format() +
                          "\nPrevious active bindings remain in use until a valid /reload keybindings.");
    }
    return handled_text("keybindings file is valid:\n  " + keybinds_file.string() + "\nRun /reload keybindings inside the interactive TUI to apply edits.");
  }

  if (args.front() != "init")
  {
    return handled_text("unsupported keybindings command: " + args.front() +
                        "\nsupported: init [--force], import <path> [--force], set <action> <key>[,<key>...], reset <action>, validate");
  }

  bool force = false;
  for (std::size_t index = 1; index < args.size(); ++index)
  {
    if (args[index] == "--force")
    {
      force = true;
      continue;
    }
    return handled_text("unsupported keybindings init option: " + args[index] + "\nsupported: --force");
  }

  std::error_code exists_error;
  auto const exists = std::filesystem::exists(keybinds_file, exists_error);
  if (exists_error)
  {
    return handled_text("failed to inspect keybindings file: " + keybinds_file.string() + "\n  cause: " + exists_error.message());
  }
  if (exists && !force)
  {
    return handled_text("keybindings file already exists:\n  " + keybinds_file.string() +
                        "\nUse /keybindings init --force to replace it, or edit it and run /reload keybindings in the TUI.");
  }

  auto const content = ava::tui::default_key_bindings_config_json();
  if (auto parsed = ava::tui::parse_key_bindings_json(content); !parsed)
  {
    return handled_text("failed to build default keybindings template:\n" + parsed.error().format());
  }

  if (auto written = ava::core::write_text_file_atomic(keybinds_file, content, "keybindings file"); !written)
    return handled_text(written.error().format());

  if (auto loaded = ava::tui::load_key_bindings(keybinds_file); !loaded)
  {
    return handled_text("wrote keybindings file, but validation failed:\n" + loaded.error().format());
  }

  return handled_text(std::string(force ? "Replaced" : "Created") + " keybindings starter file:\n  " + keybinds_file.string() +
                      "\nEdit it, then run /reload keybindings inside the interactive TUI.");
}

std::string active_theme_summary()
{
  auto const active = ava::tui::active_tui_theme();
  return active.name + " (" + active.badge + ")";
}

ava::core::Result<CommandResult> run_theme_command(runtime::Session& session, std::string_view argument)
{
  auto const args = split_command_arguments(argument);
  if (args.size() > 1)
    return handled_text("unsupported theme options: " + std::string(argument) + "\n" + tui_theme_setting_usage());

  auto settings = load_tui_display_settings(session.paths);
  if (!settings)
    return std::unexpected(std::move(settings.error()));

  if (args.empty())
  {
    ava::tui::set_tui_config_theme(settings->theme, settings->custom_theme);
    return handled_text("TUI theme:\n  config: " + settings->path.string() +
                        "\n  configured: " + (settings->theme ? *settings->theme : std::string("built-in default")) + "\n  active: " + active_theme_summary() +
                        "\n" + tui_theme_setting_usage());
  }

  if (is_tui_theme_reset_value(args.front()))
  {
    auto stored = store_tui_theme_setting(session.paths, std::nullopt);
    if (!stored)
      return std::unexpected(std::move(stored.error()));
    ava::tui::set_tui_config_theme(std::nullopt);
    return handled_text("Reset TUI theme to the built-in default.\n  config: " + tui_display_settings_file(session.paths).string() +
                        "\n  active: " + active_theme_summary());
  }

  if (!normalize_tui_theme_setting(args.front()))
  {
    auto custom_theme = load_tui_custom_theme(session.paths, args.front());
    if (!custom_theme && custom_theme.error().category() == ava::core::ErrorCategory::NotFound)
      return handled_text("unsupported theme: " + args.front() + "\n" + tui_theme_setting_usage());
    if (!custom_theme)
      return std::unexpected(std::move(custom_theme.error()));
  }

  auto stored = store_tui_theme_setting(session.paths, args.front());
  if (!stored)
    return std::unexpected(std::move(stored.error()));
  auto settings_after_store = load_tui_display_settings(session.paths);
  if (!settings_after_store)
    return std::unexpected(std::move(settings_after_store.error()));
  ava::tui::set_tui_config_theme(settings_after_store->theme, settings_after_store->custom_theme);
  return handled_text("Stored TUI theme " + *settings_after_store->theme + ".\n  config: " + tui_display_settings_file(session.paths).string() +
                      "\n  active: " + active_theme_summary());
}

struct ReloadReportRow
{
  std::string name;
  std::string status;
  std::vector<std::pair<std::string, std::string>> details;
};

std::string reload_supported_targets()
{
  return "all, theme, models, prompts, trust, compaction, keybindings, auth, permissions, lsp, mcp, plugins";
}

void append_reload_detail(ReloadReportRow& row, std::string key, std::string value)
{
  row.details.push_back({std::move(key), sanitize_inline_text(std::move(value))});
}

ReloadReportRow reload_error_row(std::string name, ava::core::Error const& error)
{
  ReloadReportRow row{.name = std::move(name), .status = "error", .details = {}};
  append_reload_detail(row, "error", error.format());
  return row;
}

std::string format_reload_report(std::vector<ReloadReportRow> const& rows)
{
  std::string output = "Reload report:";
  for (auto const& row : rows)
  {
    output += "\n  " + row.name + ": " + row.status;
    for (auto const& detail : row.details) output += "\n    " + detail.first + ": " + detail.second;
  }
  return output;
}

std::string normalize_reload_target(std::string_view target)
{
  if (target.empty() || target == "all")
    return "all";
  if (target == "theme" || target == "themes" || target == "display")
    return "display";
  if (target == "model" || target == "models")
    return "models";
  if (target == "prompt" || target == "prompts" || target == "context" || target == "contexts")
    return "prompts";
  if (target == "trust" || target == "project" || target == "projects")
    return "trust";
  if (target == "compact" || target == "compaction")
    return "compaction";
  if (target == "keybindings" || target == "keybinds" || target == "keys")
    return "keybindings";
  if (target == "auth" || target == "credentials")
    return "auth";
  if (target == "permission" || target == "permissions")
    return "permissions";
  if (target == "lsp" || target == "language-server" || target == "language-servers")
    return "lsp";
  if (target == "mcp")
    return "mcp";
  if (target == "plugin" || target == "plugins")
    return "plugins";
  return {};
}

ReloadReportRow reload_display_settings(runtime::Session& session)
{
  auto settings = apply_tui_display_settings(session.paths);
  if (!settings)
    return reload_error_row("display", settings.error());
  ReloadReportRow row{.name = "display", .status = "loaded", .details = {}};
  append_reload_detail(row, "config", settings->path.string());
  append_reload_detail(row, "configured", settings->theme ? *settings->theme : std::string("built-in default"));
  append_reload_detail(row, "active", active_theme_summary());
  return row;
}

ReloadReportRow reload_model_settings(runtime::Session& session)
{
  auto registry = ava::config::load_model_registry(session.paths);
  if (!registry)
    return reload_error_row("models", registry.error());
  session.scoped_model_cycle = registry->scoped_model_cycle;
  if (auto refreshed = refresh_runtime_parent_configuration(session); !refreshed)
    return reload_error_row("models", refreshed.error());
  ReloadReportRow row{.name = "models", .status = "loaded", .details = {}};
  append_reload_detail(row, "config", session.paths.models_file.string());
  append_reload_detail(row, "models", std::to_string(registry->models.size()));
  append_reload_detail(row, "scoped_cycle", session.scoped_model_cycle ? "configured" : "not configured");
  append_reload_detail(row, "active_model", session.model.provider_id + "/" + session.model.model_id + " (unchanged)");
  return row;
}

ReloadReportRow reload_prompt_settings(runtime::Session& session)
{
  auto prompt_state = runtime::load_runtime_prompt_state(session.paths, session.model, session.mode, session.workspace_dir, session.current_dir,
                                                         project_resources_trusted(session.project_trust), session.prompt_overrides);
  if (!prompt_state)
    return reload_error_row("prompts", prompt_state.error());
  if (auto refreshed = apply_runtime_prompt_state(session, std::move(*prompt_state)); !refreshed)
    return reload_error_row("prompts", refreshed.error());
  ReloadReportRow row{.name = "prompts", .status = "loaded", .details = {}};
  append_reload_detail(row, "project_resources", project_resources_trusted(session.project_trust) ? "enabled" : "skipped");
  append_reload_detail(row, "context_sources", std::to_string(session.context_sources.size()));
  append_reload_detail(row, "freshness_sources", std::to_string(session.freshness_sources.size()));
  append_reload_detail(row, "base_prompt",
                       session.base_prompt.from_override ? std::string("override")
                       : session.base_prompt.source_path ? session.base_prompt.source_path->string()
                                                         : std::string("built-in"));
  return row;
}

ReloadReportRow reload_trust_settings(runtime::Session& session)
{
  auto next_trust = load_project_trust_state(session.paths, session.workspace_dir);
  auto prompt_state = runtime::load_runtime_prompt_state(session.paths, session.model, session.mode, session.workspace_dir, session.current_dir,
                                                         project_resources_trusted(next_trust), session.prompt_overrides);
  if (!prompt_state)
  {
    auto row = reload_error_row("trust", prompt_state.error());
    append_reload_detail(row, "trust_file", next_trust.trust_file.string());
    return row;
  }
  session.project_trust = std::move(next_trust);
  if (auto refreshed = apply_runtime_prompt_state(session, std::move(*prompt_state)); !refreshed)
    return reload_error_row("trust", refreshed.error());
  ReloadReportRow row{.name = "trust", .status = "loaded", .details = {}};
  append_reload_detail(row, "trust_file", session.project_trust.trust_file.string());
  append_reload_detail(row, "decision", std::string(to_string(session.project_trust.decision)));
  append_reload_detail(row, "project_resources", project_resources_trusted(session.project_trust) ? "enabled" : "skipped");
  if (!session.project_trust.diagnostic.empty())
    append_reload_detail(row, "diagnostic", session.project_trust.diagnostic);
  return row;
}

ReloadReportRow reload_compaction_settings(runtime::Session& session)
{
  auto loaded_config = ava::session::load_compaction_config(session.paths);
  if (!loaded_config)
    return reload_error_row("compaction", loaded_config.error());
  auto config = resolve_compaction_config(session, std::move(*loaded_config));
  if (!config)
    return reload_error_row("compaction", config.error());
  ReloadReportRow row{.name = "compaction", .status = "validated", .details = {}};
  append_reload_detail(row, "config", session.paths.compaction_file.string());
  append_reload_detail(row, "provider", config->provider_id);
  append_reload_detail(row, "model", config->model_id);
  append_reload_detail(row, "auto_threshold_tokens", std::to_string(config->auto_threshold_tokens));
  append_reload_detail(row, "auto_threshold_percent", std::to_string(config->auto_threshold_percent));
  append_reload_detail(row, "effective_threshold_tokens",
                       std::to_string(ava::session::effective_auto_threshold_tokens(*config, session.model.context_window_tokens)));
  append_reload_detail(row, "keep_recent_tokens", std::to_string(config->keep_recent_tokens));
  append_reload_detail(row, "keep_recent_turns", std::to_string(config->keep_recent_turns));
  append_reload_detail(row, "keep_recent_messages", std::to_string(config->keep_recent_messages));
  append_reload_detail(row, "max_summary_bytes", std::to_string(config->max_summary_bytes));
  return row;
}

ReloadReportRow keybindings_reload_row(runtime::Session const& session)
{
  ReloadReportRow row{.name = "keybindings", .status = "tui-runtime", .details = {}};
  append_reload_detail(row, "config", (session.paths.ava_config_dir / "keybinds.json").string());
  append_reload_detail(row, "note", "interactive TUI reloads keybindings live; restart non-TTY sessions after edits");
  return row;
}

ReloadReportRow restart_required_reload_row(std::string name, std::string reason, std::vector<std::pair<std::string, std::filesystem::path>> paths)
{
  ReloadReportRow row{.name = std::move(name), .status = "restart-required", .details = {}};
  append_reload_detail(row, "reason", std::move(reason));
  for (auto const& path : paths) append_reload_detail(row, path.first, path.second.string());
  return row;
}

std::vector<ReloadReportRow> reload_report_rows_for_target(runtime::Session& session, std::string const& target)
{
  auto one = [&](std::string const& normalized) -> ReloadReportRow {
    if (normalized == "display")
      return reload_display_settings(session);
    if (normalized == "models")
      return reload_model_settings(session);
    if (normalized == "trust")
      return reload_trust_settings(session);
    if (normalized == "prompts")
      return reload_prompt_settings(session);
    if (normalized == "compaction")
      return reload_compaction_settings(session);
    if (normalized == "keybindings")
      return keybindings_reload_row(session);
    if (normalized == "auth")
    {
      return restart_required_reload_row("auth", "active provider credentials are resolved when a run starts", {{"config", session.paths.auth_file}});
    }
    if (normalized == "permissions")
    {
      return restart_required_reload_row(
          "permissions", "active permission policy and session grants are not hot-reloaded",
          {{"global", session.paths.ava_config_dir / "permission-rules.json"}, {"project", session.workspace_dir / ".ava" / "permission-rules.json"}});
    }
    if (normalized == "lsp")
    {
      return restart_required_reload_row("lsp", "language-server clients are created for tool calls and should restart with config changes",
                                         {{"global", session.paths.ava_config_dir / "lsp.json"}, {"project", session.workspace_dir / ".ava" / "lsp.json"}});
    }
    if (normalized == "mcp")
    {
      return restart_required_reload_row("mcp", "running MCP server processes are not restarted by /reload",
                                         {{"global", session.paths.ava_config_dir / "mcp.json"}, {"project", session.workspace_dir / ".ava" / "mcp.json"}});
    }
    return restart_required_reload_row("plugins", "plugin discovery and process state are not hot-reloaded",
                                       {{"global", session.paths.ava_config_dir / "plugins"},
                                        {"project", session.workspace_dir / ".ava" / "plugins"},
                                        {"state", session.paths.ava_state_dir / "plugin-enablement.json"}});
  };

  if (target != "all")
    return {one(target)};
  return {one("display"), one("models"),      one("trust"), one("prompts"), one("compaction"), one("keybindings"),
          one("auth"),    one("permissions"), one("lsp"),   one("mcp"),     one("plugins")};
}

ava::core::Result<CommandResult> run_reload_command(runtime::Session& session, std::string_view argument)
{
  auto const args = split_command_arguments(argument);
  if (args.size() > 1)
    return handled_text("unsupported reload target: " + std::string(argument) + "\nsupported: " + reload_supported_targets());

  auto const raw_target = args.empty() ? std::string{} : args.front();
  auto const target = normalize_reload_target(raw_target);
  if (target.empty())
    return handled_text("unsupported reload target: " + raw_target + "\nsupported: " + reload_supported_targets());
  return handled_text(format_reload_report(reload_report_rows_for_target(session, target)));
}

std::string project_trust_summary(ProjectTrustState const& state)
{
  std::string output = "Project trust:\n";
  output += "  workspace=" + state.workspace_dir.string() + "\n";
  output += "  decision=" + std::string(to_string(state.decision)) + "\n";
  if (!state.matched_path.empty())
    output += "  matched=" + state.matched_path.string() + "\n";
  output += "  trust_file=" + state.trust_file.string() + "\n";
  output += "  project_resources=" + std::string(project_resources_trusted(state) ? "enabled" : "skipped") + "\n";
  if (!state.diagnostic.empty())
    output += "  diagnostic=" + sanitize_inline_text(state.diagnostic) + "\n";
  if (state.protected_resources.empty())
  {
    output += "  protected_resources=none";
    return output;
  }
  output += "  protected_resources=" + std::to_string(state.protected_resources.size()) + "\n";
  for (auto const& resource : state.protected_resources)
  {
    output += "    " + sanitize_inline_text(resource.kind) + "  " + resource.path.string() + "\n";
  }
  if (!output.empty() && output.back() == '\n')
    output.pop_back();
  return output;
}

ava::core::Result<CommandResult> reload_project_trust_state(runtime::Session& session, std::string prefix)
{
  auto next_trust = load_project_trust_state(session.paths, session.workspace_dir);
  auto prompt_state = runtime::load_runtime_prompt_state(session.paths, session.model, session.mode, session.workspace_dir, session.current_dir,
                                                         project_resources_trusted(next_trust), session.prompt_overrides);
  if (!prompt_state)
    return std::unexpected(std::move(prompt_state.error()));
  session.project_trust = std::move(next_trust);
  if (auto refreshed = apply_runtime_prompt_state(session, std::move(*prompt_state)); !refreshed)
    return std::unexpected(std::move(refreshed.error()));
  return handled_text(std::move(prefix) + "\n" + project_trust_summary(session.project_trust));
}

ava::core::Result<CommandResult> run_trust_command(runtime::Session& session, std::string_view argument)
{
  auto const args = split_command_arguments(argument);
  auto const action = args.empty() ? std::string("status") : args.front();
  if (action == "status")
    return handled_text(project_trust_summary(session.project_trust));
  if (action == "project" || action == "trust" || action == "approve")
  {
    auto saved = set_project_trust_decision(session.paths, session.workspace_dir, true);
    if (!saved)
      return std::unexpected(std::move(saved.error()));
    return reload_project_trust_state(session, "trusted project resources");
  }
  if (action == "deny" || action == "untrust")
  {
    auto saved = set_project_trust_decision(session.paths, session.workspace_dir, false);
    if (!saved)
      return std::unexpected(std::move(saved.error()));
    return reload_project_trust_state(session, "denied project resources");
  }
  if (action == "clear")
  {
    auto cleared = clear_project_trust_decision(session.paths, session.workspace_dir);
    if (!cleared)
      return std::unexpected(std::move(cleared.error()));
    return reload_project_trust_state(session, "cleared project trust decision");
  }
  return handled_text("unsupported trust action: " + action + "\nsupported: status, project, deny, clear");
}

std::string dynamic_command_argument(std::string_view line)
{
  auto const token = command_token(line);
  if (line.size() <= token.size())
    return {};
  auto rest = line.substr(token.size());
  while (!rest.empty() && (rest.front() == ' ' || rest.front() == '\t')) rest.remove_prefix(1);
  return std::string(rest);
}

ava::plugin::PluginDiscoveryOptions skill_plugin_discovery_options(runtime::Session const& session)
{
  return ava::plugin::PluginDiscoveryOptions{
      .global_plugins_dir = session.paths.ava_config_dir / "plugins",
      .project_plugins_dir = project_resources_trusted(session.project_trust) ? session.workspace_dir / ".ava" / "plugins" : std::filesystem::path{}};
}

std::vector<ava::context::DeclaredSkillFileOptions> declared_plugin_skill_files(ava::plugin::PluginDiagnostics const& diagnostics)
{
  std::vector<ava::context::DeclaredSkillFileOptions> files;
  for (auto const& skill : ava::plugin::enabled_plugin_static_skill_files(diagnostics))
  {
    files.push_back(ava::context::DeclaredSkillFileOptions{.path = skill.path,
                                                           .name = skill.name,
                                                           .description = skill.description,
                                                           .source_type = ava::context::SkillSourceType::Plugin,
                                                           .preloaded_content = skill.content});
  }
  return files;
}

ava::core::Result<std::string> skill_prompt_message(runtime::Session& session, CommandRequest const& request, CommandRegistryEntry const& entry)
{
  auto plugin_diagnostics = ava::plugin::collect_plugin_diagnostics(skill_plugin_discovery_options(session),
                                                                    session.paths.ava_state_dir / "plugin-enablement.json", session.workspace_dir);
  auto loaded = ava::context::load_skills(ava::context::SkillLoadOptions{
      .workspace_root = session.workspace_dir,
      .declared_skill_files = declared_plugin_skill_files(plugin_diagnostics),
      .include_project_skills = project_resources_trusted(session.project_trust),
  });
  auto const match = std::ranges::find_if(loaded.skills, [&](ava::context::LoadedSkill const& skill) { return skill.name == entry.skill_name; });
  if (match == loaded.skills.end())
  {
    auto error = ava::core::Error(ava::core::ErrorCategory::InvalidArgument, "skill not found");
    error.with_context("skill", entry.skill_name);
    return std::unexpected(std::move(error));
  }

  auto context = make_tool_context(session, request.permission_resolver);
  context.permission_tool_name = "skill";
  context.current_tool_name = "skill";
  if (auto permission = ava::tools::ensure_permission(context, ava::permissions::Operation::SkillLoad, match->path, match->name, "skill",
                                                      "skill loading requires permission");
      !permission)
  {
    return std::unexpected(std::move(permission.error()));
  }
  auto sampled_files = ava::context::sample_skill_files(match->directory);
  return ava::context::format_loaded_skill_for_tool(*match, sampled_files);
}

std::string mcp_command_text(ava::mcp::McpServerConfig const& server)
{
  std::string text = server.command;
  for (auto const& arg : server.args) text += " " + arg;
  return text;
}

ava::core::VoidResult ensure_mcp_prompt_permissions(ava::tools::ToolContext const& context, ava::mcp::McpServerConfig const& server,
                                                    CommandRegistryEntry const& entry)
{
  if (auto permission = ava::tools::ensure_permission(context, ava::permissions::Operation::McpServerLaunch, server.source_path, mcp_command_text(server),
                                                      "mcp_prompt", "MCP server launch requires permission");
      !permission)
  {
    return std::unexpected(std::move(permission.error()));
  }
  if (auto permission = ava::tools::ensure_permission(context, ava::permissions::Operation::McpServerConnect, server.source_path, server.id, "mcp_prompt",
                                                      "MCP server connection requires permission");
      !permission)
  {
    return std::unexpected(std::move(permission.error()));
  }
  auto const command = entry.mcp_server_id + ":" + entry.mcp_prompt_name;
  if (auto permission = ava::tools::ensure_permission(context, ava::permissions::Operation::McpToolCall, server.source_path, command, "mcp_prompt",
                                                      "MCP prompt retrieval requires permission");
      !permission)
  {
    return std::unexpected(std::move(permission.error()));
  }
  return {};
}

ava::core::Result<std::string> mcp_prompt_message(runtime::Session& session, CommandRequest const& request, CommandRegistryEntry const& entry,
                                                  std::string_view argument_text)
{
  auto config_options = ava::mcp::default_mcp_config_options(session.workspace_dir);
  config_options.global_config_file = session.paths.ava_config_dir / "mcp.json";
  config_options.project_config_file = project_resources_trusted(session.project_trust) ? session.workspace_dir / ".ava" / "mcp.json" : std::filesystem::path{};
  auto config = ava::mcp::load_mcp_config(config_options);
  if (!config)
    return std::unexpected(std::move(config.error()));
  auto const server = std::ranges::find_if(config->servers, [&](ava::mcp::McpServerConfig const& item) { return item.id == entry.mcp_server_id; });
  if (server == config->servers.end() || !server->enabled)
  {
    auto error = ava::core::Error(ava::core::ErrorCategory::InvalidArgument, "MCP server not found");
    error.with_context("mcp_server", entry.mcp_server_id);
    return std::unexpected(std::move(error));
  }

  auto arguments = mcp_prompt_arguments_json(entry, argument_text);
  if (!arguments)
    return std::unexpected(std::move(arguments.error()));

  auto context = make_tool_context(session, request.permission_resolver);
  context.permission_tool_name = "mcp_prompt";
  context.current_tool_name = "mcp_prompt";
  context.cancel_requested = request.cancel_requested;
  if (auto permission = ensure_mcp_prompt_permissions(context, *server, entry); !permission)
  {
    return std::unexpected(std::move(permission.error()));
  }

  auto client = ava::mcp::McpStdioClient::start(*server, ava::mcp::McpStdioClientOptions{.workspace_dir = session.workspace_dir}, request.cancel_requested);
  if (!client)
    return std::unexpected(std::move(client.error()));
  auto prompt = (*client)->get_prompt(entry.mcp_prompt_name, *arguments, request.cancel_requested);
  auto shutdown = (*client)->shutdown();
  if (!prompt)
    return std::unexpected(std::move(prompt.error()));
  if (!shutdown)
    return std::unexpected(std::move(shutdown.error()));
  if (prompt->content.empty())
  {
    auto error = ava::core::Error(ava::core::ErrorCategory::Tool, "MCP prompt returned no text");
    error.with_context("mcp_server", entry.mcp_server_id);
    error.with_context("mcp_prompt", entry.mcp_prompt_name);
    return std::unexpected(std::move(error));
  }
  return std::move(prompt->content);
}

ava::core::Result<CommandResult> run_registry_command(runtime::Session& session, CommandRequest request, CommandRegistryEntry const& entry)
{
  if (!entry.enabled)
    return handled_text(entry.command + " is disabled: " + entry.disabled_reason);
  auto const argument = dynamic_command_argument(request.command);
  switch (entry.kind)
  {
    case UnifiedCommandKind::Backend:
      return CommandResult{};
    case UnifiedCommandKind::PromptTemplate: {
      auto prompt = expand_prompt_command_template(entry.template_text, argument);
      if (!prompt)
        return std::unexpected(std::move(prompt.error()));
      return handled_prompt(entry.command, to_string(entry.source), std::move(*prompt));
    }
    case UnifiedCommandKind::SkillPrompt: {
      auto prompt = skill_prompt_message(session, request, entry);
      if (!prompt)
        return std::unexpected(std::move(prompt.error()));
      return handled_prompt(entry.command, to_string(entry.source), std::move(*prompt));
    }
    case UnifiedCommandKind::McpPrompt: {
      auto prompt = mcp_prompt_message(session, request, entry, argument);
      if (!prompt)
        return std::unexpected(std::move(prompt.error()));
      return handled_prompt(entry.command, to_string(entry.source), std::move(*prompt));
    }
    case UnifiedCommandKind::PluginCommand: {
      auto delegated = request;
      auto args = argument.empty() ? std::string("{}") : argument;
      delegated.command = "/plugin run " + entry.plugin_id + " " + entry.plugin_command_name + " " + args;
      return run_plugin_command(session, delegated);
    }
  }
  return CommandResult{};
}

}  // namespace

bool is_backend_command(std::string_view line) noexcept
{
  if (is_shell_helper_command(line))
    return true;
  return find_command_catalog_entry(line) != nullptr;
}

bool is_backend_command(std::string_view line, runtime::Session& session)
{
  if (is_backend_command(line))
    return true;
  return command_registry_contains(session, line);
}

ava::core::Result<CommandResult> run_command(runtime::Session& session, CommandRequest request)
{
  CommandResult result;
  if (request.command.empty())
    return result;

  if (is_shell_helper_command(request.command))
  {
    auto const shell_command = shell_helper_argument(request.command);
    if (shell_command.empty())
      return handled_text(missing_argument("!<command> or !!<command>"));
    request.command = "/bash " + shell_command;
  }

  auto const* entry = find_command_catalog_entry(request.command);
  if (!entry)
  {
    auto const token = command_token(request.command);
    auto registry = session.load_command_registry(CommandRegistryOptions{.include_builtins = false,
                                                                          .include_prompt_commands = true,
                                                                          .include_skills = true,
                                                                          .include_plugin_commands = true,
                                                                          .include_mcp_prompts = token.starts_with("/mcp:"),
                                                                          .permission_resolver = request.permission_resolver,
                                                                          .cancel_requested = request.cancel_requested});
    if (auto const* registry_entry = find_command_registry_entry(registry, request.command))
    {
      return run_registry_command(session, std::move(request), *registry_entry);
    }
    if (!token.starts_with("/skill:") && !token.starts_with("/mcp:") && !token.starts_with("/plugin:"))
    {
      registry = session.load_command_registry(CommandRegistryOptions{.include_builtins = false,
                                                                       .include_prompt_commands = false,
                                                                       .include_skills = false,
                                                                       .include_plugin_commands = false,
                                                                       .include_mcp_prompts = true,
                                                                       .permission_resolver = request.permission_resolver,
                                                                       .cancel_requested = request.cancel_requested});
      if (auto const* registry_entry = find_command_registry_entry(registry, request.command))
      {
        return run_registry_command(session, std::move(request), *registry_entry);
      }
    }
    if (token.starts_with("/skill:") || token.starts_with("/mcp:") || token.starts_with("/plugin:"))
    {
      if (!registry.diagnostics.empty())
        return handled_text(registry.diagnostics.front().message);
      return handled_text("command not found: " + std::string(token));
    }
    return result;
  }
  request.command = normalize_command_line(request.command, *entry);

  if (!entry->enabled)
  {
    return handled_text(entry->command + " is disabled: " + entry->disabled_reason);
  }

  // RPC command execution already serializes session-store access around run_command; reacquiring
  // the same mutex from event-hook permission audits would deadlock nested command events.
  auto plugin_observer_options = plugin_event_observer_options(session, request.permission_resolver, nullptr);
  plugin_observer_options.cancel_requested = request.cancel_requested;
  request.event_sink = make_plugin_event_observer_sink(std::move(plugin_observer_options), std::move(request.event_sink));

  if (request.command == "/quit" || request.command == "/exit")
  {
    result.handled = true;
    result.quit = true;
    return result;
  }
  if (request.command == "/help")
  {
    return handled_text(command_help_text(request.hotkeys));
  }
  if (starts_with_command(request.command, "/hotkeys"))
  {
    return run_keybindings_command(session, command_argument(request.command, "/hotkeys"), request.hotkeys);
  }
  if (starts_with_command(request.command, "/theme"))
  {
    return run_theme_command(session, command_argument(request.command, "/theme"));
  }
  if (request.command == "/settings")
  {
    return handled_text("Settings are shown as a TUI view. Use /theme dark|light|plain|custom-name|reset to persist the display theme.");
  }
  if (request.command == "/details")
  {
    return handled_text("Tool details are a TUI display toggle. Use /details inside the TUI to switch views.");
  }
  if (starts_with_command(request.command, "/tool"))
  {
    return handled_text(
        "Tool detail inspection is available inside the interactive TUI. Use /tool [query] to show the latest or matching expanded tool card, /details to "
        "toggle all tool cards, or /copy tool [query] to copy details.");
  }
  if (starts_with_command(request.command, "/diff"))
  {
    return handled_text(
        "Tool diff inspection is available inside the interactive TUI. Use /diff [query] to show the latest or matching unified diff, or /copy diff [query] to "
        "copy it.");
  }
  if (starts_with_command(request.command, "/copy"))
  {
    auto const argument = command_argument(request.command, "/copy");
    auto const copy_args = split_command_arguments(argument);
    auto const target = copy_args.empty() ? std::string{} : copy_args.front();
    if (!target.empty() && target != "tool" && target != "tools" && target != "diff" && target != "diffs" && target != "permission" && target != "permissions")
    {
      return handled_text("unsupported copy target: " + target + "\nsupported: tool [query], diff [query], permission [query]");
    }
    return handled_text(
        "Clipboard copy is available inside the interactive TUI. Use /copy for the latest AVA message, /copy tool [query] for tool details, /copy diff [query] "
        "for unified diffs, or /copy permission [query] for permission audit details.");
  }
  if (request.command == "/thinking")
  {
    return handled_text("Thinking visibility is a TUI display toggle. It does not change provider reasoning mode.");
  }
  if (starts_with_command(request.command, "/attach"))
  {
    return handled_text(
        "Image attachment import is available inside the interactive TUI with /attach <path>. In headless RPC, send prompt attachments with the attachments "
        "array.");
  }
  if (starts_with_command(request.command, "/reload"))
  {
    return run_reload_command(session, command_argument(request.command, "/reload"));
  }
  if (starts_with_command(request.command, "/models"))
  {
    return run_models_command(session, command_argument(request.command, "/models"));
  }
  if (starts_with_command(request.command, "/providers"))
  {
    return run_providers_command(session, command_argument(request.command, "/providers"));
  }
  if (request.command == "/scoped-models")
  {
    return handled_text("Scoped model cycling is a TUI selector. In the TUI, /scoped-models opens the model cycle list and Ctrl+S persists it to models.json.");
  }
  if (starts_with_command(request.command, "/connect"))
  {
    return run_connect_command(session, request);
  }
  if (starts_with_command(request.command, "/mcp"))
  {
    return run_mcp_command(session, request);
  }
  if (starts_with_command(request.command, "/plugins"))
  {
    return run_plugins_command(session, request);
  }
  if (starts_with_command(request.command, "/plugin"))
  {
    return run_plugin_command(session, request);
  }
  if (starts_with_command(request.command, "/trust"))
  {
    return run_trust_command(session, command_argument(request.command, "/trust"));
  }
  if (starts_with_command(request.command, "/permissions"))
  {
    return run_permissions_command(session, request);
  }
  if (starts_with_command(request.command, "/sessions"))
  {
    return run_sessions_command(session, command_argument(request.command, "/sessions"));
  }
  if (starts_with_command(request.command, "/jobs"))
  {
    return run_jobs_command(session, command_argument(request.command, "/jobs"));
  }
  if (request.command == "/recover-persistence")
  {
    return run_recover_persistence_command(session);
  }
  if (starts_with_command(request.command, "/fork"))
  {
    return run_fork_command(session, command_argument(request.command, "/fork"));
  }
  if (starts_with_command(request.command, "/clone"))
  {
    return run_clone_command(session, command_argument(request.command, "/clone"));
  }
  if (starts_with_command(request.command, "/new"))
  {
    return run_new_session_command(session, command_argument(request.command, "/new"));
  }
  if (starts_with_command(request.command, "/resume"))
  {
    return run_resume_command(session, command_argument(request.command, "/resume"));
  }
  if (starts_with_command(request.command, "/name"))
  {
    return run_name_command(session, command_argument(request.command, "/name"));
  }
  if (starts_with_command(request.command, "/labels"))
  {
    return run_labels_command(session, command_argument(request.command, "/labels"));
  }
  if (request.command == "/mode")
  {
    return run_mode_command(session);
  }
  if (starts_with_command(request.command, "/context"))
  {
    return run_context_command(session, command_argument(request.command, "/context"));
  }
  if (request.command == "/stats" || request.command == "/status")
  {
    return run_stats_command(session);
  }
  if (starts_with_command(request.command, "/compact"))
  {
    return run_compact_command(session, request);
  }
  if (starts_with_command(request.command, "/import"))
  {
    return run_import_command(session, command_argument(request.command, "/import"));
  }
  if (starts_with_command(request.command, "/export"))
  {
    return run_export_command(session, request);
  }

  if (entry->hint.empty() && starts_with_command(request.command, entry->command))
  {
    return handled_text(missing_argument(entry->command));
  }

  if (request.command == "/glob")
  {
    return handled_text(missing_argument("/glob <pattern>"));
  }
  if (request.command == "/find")
  {
    return handled_text(missing_argument("/find <pattern>"));
  }
  if (request.command == "/grep")
  {
    return handled_text(missing_argument("/grep <text> [glob]"));
  }
  if (request.command == "/read")
  {
    return handled_text(missing_argument("/read <path>"));
  }
  if (request.command == "/write")
  {
    return handled_text(missing_argument("/write <path> <text>"));
  }
  if (request.command == "/bash")
  {
    return handled_text(missing_argument("/bash <command>"));
  }

  return run_tool_command(session, request);
}

}  // namespace ava::app
