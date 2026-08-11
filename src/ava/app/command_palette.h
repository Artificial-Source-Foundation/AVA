#include <memory>
#pragma once

#include "ava/app/command_catalog.h"
#include "ava/app/session_user_turns.h"
#include "ava/tui/composer.h"
#include "ava/config/model_config.h"
#include "ava/session/session_store.h"
#include "ava/session/session_tree.h"
#include "ava/core/result.h"

#include <functional>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <vector>
#include "debug.h"

namespace ava::provider {
class ProviderCatalog;
}
namespace ava::app {

struct SessionTitleCatalogChanges;

namespace runtime {
class Session;
} // namespace runtime

enum class SessionSelectorSort
{
  Recent,
  Name,
  Path,
};

struct WorkspacePathCandidate
{
  std::string value = {};
  std::string description = {};
  bool directory = false;

  AVA_DEBUG_PRINT_MEMBERS_ON
};

struct ApplicationCatalogOperationCounts
{
  std::size_t workspace_walks = 0;
  std::size_t session_tree_builds = 0;
  std::size_t session_node_refreshes = 0;
  std::size_t value_refreshes = 0;

  AVA_DEBUG_PRINT_MEMBERS_ON
};

struct ApplicationCatalogCache
{
  std::vector<WorkspacePathCandidate> workspace_path_candidates = {};
  std::vector<tui::FileReferenceItem> file_references = {};
  std::size_t workspace_catalog_generation = 0;
  std::optional<ava::session::SessionTreeIndex> session_tree = std::nullopt;
  std::string session_tree_error = {};
  std::vector<tui::SlashCommandItem> slash_commands = {};
  std::size_t slash_catalog_generation = 0;
  ApplicationCatalogOperationCounts operations = {};

  AVA_DEBUG_PRINT_MEMBERS_ON
};

struct ApplicationCatalogDelivery
{
  std::vector<tui::SlashCommandItem> slash_commands = {};
  std::size_t slash_catalog_generation = 0;
  std::vector<tui::FileReferenceItem> file_references = {};
  std::size_t workspace_catalog_generation = 0;

  AVA_DEBUG_PRINT_MEMBERS_ON
};

using WorkspaceCatalogWalker = std::function<std::vector<WorkspacePathCandidate>(runtime::session_ts const&)>;
using SessionTreeIndexBuilder = std::function<ava::core::Result<ava::session::SessionTreeIndex>(runtime::session_ts const&)>;

// Application-lifetime serialization boundary for catalog cache values. One
// mutex intentionally serializes owned values, discovery, authoritative session
// reads, operation counters, and published generations.
class ApplicationCatalogCoordinator final
{
 public:
  explicit ApplicationCatalogCoordinator(ApplicationCatalogCache cache, std::size_t title_catalog_cursor = 0);

  ApplicationCatalogCoordinator(ApplicationCatalogCoordinator const&) = delete;
  ApplicationCatalogCoordinator& operator=(ApplicationCatalogCoordinator const&) = delete;

  [[nodiscard]] ApplicationCatalogCache snapshot() const;
  [[nodiscard]] ApplicationCatalogDelivery delivery_snapshot();
  void refresh_values(runtime::session_ts const& unlocked_session, std::vector<CommandHotkey> const& hotkeys = {});
  void refresh_workspace(runtime::session_ts const& unlocked_session, std::vector<CommandHotkey> const& hotkeys = {}, WorkspaceCatalogWalker workspace_walker = {});
  [[nodiscard]] ava::core::Result<bool> refresh_session_tree_and_consume_title_changes(runtime::session_ts const& unlocked_session,
                                                                                       SessionTitleCatalogChanges const& captured_changes,
                                                                                       std::vector<CommandHotkey> const& hotkeys = {},
                                                                                       SessionTreeIndexBuilder session_tree_builder = {});
  [[nodiscard]] ava::core::Result<bool> refresh_current_session(runtime::session_ts const& unlocked_session, std::vector<CommandHotkey> const& hotkeys = {});
  [[nodiscard]] ava::core::Result<bool> refresh_title_changes(runtime::session_ts const& unlocked_session, SessionTitleCatalogChanges const& changes,
                                                              std::vector<CommandHotkey> const& hotkeys = {},
                                                              SessionTreeIndexBuilder session_tree_builder = {});
  void retarget_session(std::string_view current_session_id);
  [[nodiscard]] std::size_t title_catalog_cursor() const;

  [[nodiscard]] tui::SelectListView session_view(SessionSelectorSort sort = SessionSelectorSort::Recent, std::string footer_hint = {}, bool named_only = false,
                                                 bool show_paths = false, bool show_archived = false, bool show_label_time = false) const;
  [[nodiscard]] ava::core::Result<std::optional<std::string>> parent_target(std::string_view session_id) const;
  [[nodiscard]] ava::core::Result<std::optional<std::string>> child_target(std::string_view session_id, SessionSelectorSort sort = SessionSelectorSort::Recent,
                                                                           bool include_archived = false) const;

  AVA_DEBUG_PRINT_MEMBERS_OPT_OUT

 private:
  void refresh_values_during_operation(runtime::session_ts const& unlocked_session, std::vector<CommandHotkey> const& hotkeys);
  [[nodiscard]] ava::core::Result<bool> refresh_session_tree_during_operation(runtime::session_ts const& unlocked_session, std::vector<CommandHotkey> const& hotkeys,
                                                                              SessionTreeIndexBuilder session_tree_builder);
  [[nodiscard]] ava::core::Result<bool> refresh_current_session_during_operation(runtime::session_ts const& unlocked_session, std::vector<CommandHotkey> const& hotkeys);

  mutable std::mutex mutex_;
  ApplicationCatalogCache cache_;
  std::size_t delivered_slash_catalog_generation_ = 0;
  std::size_t delivered_workspace_catalog_generation_ = 0;
  std::size_t title_catalog_cursor_ = 0;
};

[[nodiscard]] ApplicationCatalogCache build_application_catalog_cache(runtime::session_ts const& unlocked_session, std::vector<CommandHotkey> const& hotkeys = {},
                                                                      WorkspaceCatalogWalker workspace_walker = {},
                                                                      SessionTreeIndexBuilder session_tree_builder = {});
void refresh_application_catalog_values(ApplicationCatalogCache& cache, runtime::session_ts const& unlocked_session, std::vector<CommandHotkey> const& hotkeys = {});
void refresh_application_workspace_catalog(ApplicationCatalogCache& cache, runtime::session_ts const& unlocked_session, std::vector<CommandHotkey> const& hotkeys = {},
                                           WorkspaceCatalogWalker workspace_walker = {});
void refresh_application_session_tree(ApplicationCatalogCache& cache, runtime::session_ts const& unlocked_session, std::vector<CommandHotkey> const& hotkeys = {},
                                      SessionTreeIndexBuilder session_tree_builder = {});
void retarget_application_session(ApplicationCatalogCache& cache, std::string_view current_session_id);

[[nodiscard]] SessionSelectorSort next_session_selector_sort(SessionSelectorSort sort) noexcept;
[[nodiscard]] std::string session_selector_sort_label(SessionSelectorSort sort);
[[nodiscard]] std::vector<tui::SlashCommandItem> command_catalog_slash_items(std::vector<CommandHotkey> const& hotkeys = {});
[[nodiscard]] std::vector<tui::SlashCommandItem> command_catalog_slash_items_1(runtime::session_ts const& unlocked_session, std::vector<CommandHotkey> const& hotkeys = {});
[[nodiscard]] std::vector<tui::FileReferenceItem> file_reference_items(runtime::session_ts const& unlocked_session);
[[nodiscard]] tui::SelectListView model_selector_view(ava::config::ModelRegistry const& registry, ava::config::ModelInfo const& current_model,
                                                      std::shared_ptr<ava::provider::ProviderCatalog const> ensured_provider_catalog, std::string footer_hint = {});
[[nodiscard]] tui::SelectListView model_selector_view_1(runtime::session_ts const& unlocked_session, std::string footer_hint = {});
[[nodiscard]] tui::SelectListView scoped_model_selector_view(ava::config::ModelRegistry const& registry, ava::config::ModelInfo const& current_model,
                                                             std::optional<std::vector<std::string>> const& scoped_model_cycle,
                                                             std::shared_ptr<ava::provider::ProviderCatalog const> ensured_provider_catalog,
                                                             std::string footer_hint = {});
[[nodiscard]] tui::SelectListView scoped_model_selector_view_1(runtime::session_ts const& unlocked_session, std::string footer_hint = {});
[[nodiscard]] tui::SelectListView session_selector_view(std::vector<ava::session::SessionSummary> summaries, std::string current_session_id = {},
                                                        SessionSelectorSort sort = SessionSelectorSort::Recent, std::string footer_hint = {},
                                                        bool show_paths = false);
[[nodiscard]] tui::SelectListView session_selector_view(ava::session::SessionTreeIndex const& tree, SessionSelectorSort sort = SessionSelectorSort::Recent,
                                                        std::string footer_hint = {}, bool named_only = false, bool show_paths = false,
                                                        bool show_archived = false, bool show_label_time = false);
[[nodiscard]] tui::SelectListView session_selector_view(ApplicationCatalogCache const& cache, SessionSelectorSort sort = SessionSelectorSort::Recent,
                                                        std::string footer_hint = {}, bool named_only = false, bool show_paths = false,
                                                        bool show_archived = false, bool show_label_time = false);
#if 0 // Nothing is calling this function.
[[nodiscard]] tui::SelectListView session_selector_view(runtime::session_ts const& unlocked_session, SessionSelectorSort sort = SessionSelectorSort::Recent,
                                                        std::string footer_hint = {}, bool named_only = false, bool show_paths = false,
                                                        bool show_archived = false, bool show_label_time = false);
#endif
// Newest public user turns first. Item values are stable session entry ids;
// rows keep only the backend-bounded preview/timestamp fields.
[[nodiscard]] tui::SelectListView user_turn_selector_view(std::vector<SessionUserTurn> turns, std::string title, std::string footer_hint = {},
                                                          std::string initial_query = {}, bool truncated_before = false);
[[nodiscard]] ava::core::Result<tui::SelectListView> user_turn_selector_view(runtime::session_ts const& unlocked_session, std::string title, std::string footer_hint = {},
                                                                             std::string initial_query = {});
[[nodiscard]] std::optional<std::string> session_selector_parent_target(ava::session::SessionTreeIndex const& tree, std::string_view session_id);
[[nodiscard]] std::optional<std::string> session_selector_child_target(ava::session::SessionTreeIndex const& tree, std::string_view session_id,
                                                                       SessionSelectorSort sort = SessionSelectorSort::Recent, bool include_archived = false);

}  // namespace ava::app
