#pragma once

#include "ava/app/command_catalog.h"

#include <string>
#include <vector>

namespace ava::app {

[[nodiscard]] std::string command_help_text(std::vector<CommandHotkey> const& hotkeys);
[[nodiscard]] std::string command_hotkeys_text(std::vector<CommandHotkey> const& hotkeys);

}  // namespace ava::app
