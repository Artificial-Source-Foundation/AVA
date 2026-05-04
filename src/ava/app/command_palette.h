#pragma once

#include <vector>

#include "ava/app/command_catalog.h"
#include "ava/tui/composer.h"

namespace ava::app {

struct RuntimeSession;

[[nodiscard]] std::vector<tui::SlashCommandItem> command_catalog_slash_items(
    std::vector<CommandHotkey> const& hotkeys = {});
[[nodiscard]] std::vector<tui::SlashCommandItem> command_catalog_slash_items(
    RuntimeSession const& session, std::vector<CommandHotkey> const& hotkeys = {});

}  // namespace ava::app
