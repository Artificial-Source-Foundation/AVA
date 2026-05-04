#pragma once

#include <string>
#include <vector>

#include "ava/app/command_catalog.h"

namespace ava::app {

[[nodiscard]] std::string command_help_text(const std::vector<CommandHotkey>& hotkeys);
[[nodiscard]] std::string command_hotkeys_text(const std::vector<CommandHotkey>& hotkeys);

}  // namespace ava::app
