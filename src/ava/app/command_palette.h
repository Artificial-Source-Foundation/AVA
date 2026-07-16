#pragma once

#include "ava/app/command_catalog.h"
#include "ava/tui/composer.h"
#include "ava/config/model_config.h"
#include "ava/session/session_store.h"
#include "ava/session/session_tree.h"

#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace ava::app {

namespace runtime {
struct Session;
} // namespace runtime

enum class SessionSelectorSort
{
  Recent,
  Name,
  Path,
};

[[nodiscard]] SessionSelectorSort next_session_selector_sort(SessionSelectorSort sort) noexcept;
[[nodiscard]] std::string session_selector_sort_label(SessionSelectorSort sort);
[[nodiscard]] std::vector<tui::SlashCommandItem> command_catalog_slash_items(std::vector<CommandHotkey> const& hotkeys = {});
[[nodiscard]] std::vector<tui::SlashCommandItem> command_catalog_slash_items(runtime::Session const& session, std::vector<CommandHotkey> const& hotkeys = {});
[[nodiscard]] std::vector<tui::FileReferenceItem> file_reference_items(runtime::Session const& session);
[[nodiscard]] tui::SelectListView model_selector_view(ava::config::ModelRegistry const& registry, ava::config::ModelInfo const& current_model,
                                                      std::string footer_hint = {});
[[nodiscard]] tui::SelectListView model_selector_view(runtime::Session const& session, std::string footer_hint = {});
[[nodiscard]] tui::SelectListView scoped_model_selector_view(ava::config::ModelRegistry const& registry, ava::config::ModelInfo const& current_model,
                                                             std::optional<std::vector<std::string>> const& scoped_model_cycle, std::string footer_hint = {});
[[nodiscard]] tui::SelectListView scoped_model_selector_view(runtime::Session const& session, std::string footer_hint = {});
[[nodiscard]] tui::SelectListView session_selector_view(std::vector<ava::session::SessionSummary> summaries, std::string current_session_id = {},
                                                        SessionSelectorSort sort = SessionSelectorSort::Recent, std::string footer_hint = {},
                                                        bool show_paths = true);
[[nodiscard]] tui::SelectListView session_selector_view(ava::session::SessionTreeIndex tree, SessionSelectorSort sort = SessionSelectorSort::Recent,
                                                        std::string footer_hint = {}, bool named_only = false, bool show_paths = true,
                                                        bool show_archived = false, bool show_label_time = false);
[[nodiscard]] tui::SelectListView session_selector_view(runtime::Session const& session, SessionSelectorSort sort = SessionSelectorSort::Recent,
                                                        std::string footer_hint = {}, bool named_only = false, bool show_paths = true,
                                                        bool show_archived = false, bool show_label_time = false);
[[nodiscard]] std::optional<std::string> session_selector_parent_target(ava::session::SessionTreeIndex const& tree, std::string_view session_id);
[[nodiscard]] std::optional<std::string> session_selector_child_target(ava::session::SessionTreeIndex const& tree, std::string_view session_id,
                                                                       SessionSelectorSort sort = SessionSelectorSort::Recent, bool include_archived = false);

}  // namespace ava::app
