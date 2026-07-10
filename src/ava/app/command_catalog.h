#pragma once

#include "ava/debug/print_members_on.h"

#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace ava::app {

struct CommandCatalogEntry
{
  std::string command;
  std::vector<std::string> aliases = {};
  std::string description;
  std::string hint = "";
  std::string category = "";
  bool enabled = true;
  std::string disabled_reason = "";

  AVA_DEBUG_PRINT_MEMBERS_ON
};

struct CommandHotkey
{
  std::string action;
  std::string description;
  std::string keys;

  AVA_DEBUG_PRINT_MEMBERS_ON
};

[[nodiscard]] std::vector<CommandCatalogEntry> const& command_catalog();
[[nodiscard]] CommandCatalogEntry const* find_command_catalog_entry(std::string_view line) noexcept;
[[nodiscard]] std::string normalize_command_line(std::string_view line, CommandCatalogEntry const& entry);

}  // namespace ava::app
