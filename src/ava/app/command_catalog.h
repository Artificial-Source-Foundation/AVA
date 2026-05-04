#pragma once

#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace ava::app {

struct CommandCatalogEntry {
  std::string command;
  std::vector<std::string> aliases = {};
  std::string description;
  std::string hint = "";
  std::string category = "";
  bool enabled = true;
  std::string disabled_reason = "";
};

struct CommandHotkey {
  std::string action;
  std::string description;
  std::string keys;
};

[[nodiscard]] const std::vector<CommandCatalogEntry>& command_catalog();
[[nodiscard]] const CommandCatalogEntry* find_command_catalog_entry(std::string_view line) noexcept;
[[nodiscard]] std::string normalize_command_line(std::string_view line, const CommandCatalogEntry& entry);

}  // namespace ava::app
