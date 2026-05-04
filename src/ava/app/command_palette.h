#pragma once

#include <vector>

#include "ava/app/command_catalog.h"
#include "ava/tui/composer.h"

namespace ava::app {

struct RuntimeSession;

[[nodiscard]] std::vector<tui::SlashCommandItem> command_catalog_slash_items(
    const std::vector<CommandHotkey>& hotkeys = {});
[[nodiscard]] std::vector<tui::SlashCommandItem> command_catalog_slash_items(
    const RuntimeSession& session, const std::vector<CommandHotkey>& hotkeys = {});

}  // namespace ava::app
