#pragma once

#include "ava/app/command_catalog.h"
#include "ava/tui/composer.h"
#include "ava/config/model_config.h"
#include "ava/session/session_store.h"

#include <string>
#include <vector>

namespace ava::app {

struct RuntimeSession;

enum class SessionSelectorSort
{
  Recent,
  Name,
  Path,
};

[[nodiscard]] std::vector<tui::SlashCommandItem> command_catalog_slash_items(std::vector<CommandHotkey> const& hotkeys = {});
[[nodiscard]] std::vector<tui::SlashCommandItem> command_catalog_slash_items(RuntimeSession const& session, std::vector<CommandHotkey> const& hotkeys = {});
[[nodiscard]] tui::SelectListView model_selector_view(ava::config::ModelRegistry const& registry, ava::config::ModelInfo const& current_model,
                                                      std::string footer_hint = {});
[[nodiscard]] tui::SelectListView model_selector_view(RuntimeSession const& session, std::string footer_hint = {});
[[nodiscard]] tui::SelectListView session_selector_view(std::vector<ava::session::SessionSummary> summaries, std::string current_session_id = {},
                                                        SessionSelectorSort sort = SessionSelectorSort::Recent, std::string footer_hint = {});
[[nodiscard]] tui::SelectListView session_selector_view(RuntimeSession const& session, SessionSelectorSort sort = SessionSelectorSort::Recent,
                                                        std::string footer_hint = {});

}  // namespace ava::app
