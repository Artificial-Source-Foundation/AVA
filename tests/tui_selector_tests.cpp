#include "sys.h"
#include "tests/support/test_harness.h"
#include "tests/support/tui_test_support.h"
#include "ava/app/command_palette.h"
#include "ava/tui/composer.h"
#include "ava/tui/keybindings.h"
#include "ava/tui/runtime.h"
#include "ava/tui/runtime_user_turn_selection_internal.h"
#include "ava/tui/theme.h"
#include "ava/config/model_config.h"
#include "ava/session/session_store.h"
#include "ava/session/session_tree.h"
#include "ava/core/error.h"

#include <algorithm>
#include <cstddef>
#include <optional>
#include <string>
#include <utility>
#include <vector>

void run_tui_selector_tests()
{
  auto make_model = [](std::string provider, std::string id, std::string name, std::string family, std::optional<bool> reasoning,
                       std::vector<std::string> levels = {}) {
    ava::config::ModelInfo model;
    model.provider_id = std::move(provider);
    model.model_id = std::move(id);
    model.display_name = std::move(name);
    model.family = std::move(family);
    model.context_window_tokens = 200000;
    model.supports_tools = true;
    model.supports_streaming = true;
    model.supports_reasoning = reasoning;
    model.reasoning_levels = std::move(levels);
    return model;
  };
  ava::config::ModelRegistry model_registry{
      .default_provider_id = "openai",
      .default_model_id = "gpt-5.5",
      .models = {
          make_model("openai", "gpt-5.5", "GPT-5.5", "gpt-5", true, {"low", "medium", "high"}),
          make_model("anthropic", "claude-sonnet-4-5", "Claude Sonnet 4.5", "claude-sonnet", false),
          ava::config::ModelInfo{
              .provider_id = "openai", .model_id = "diagnostic-local", .display_name = "Diagnostic Local", .family = "custom", .supports_reasoning = true},
          make_model("unregistered", "remote-model", "Remote Model", "remote", std::nullopt)}};
  auto const model_picker = ava::app::model_selector_view(model_registry, model_registry.models.front(), "Enter choose · Esc cancel");
  expect(model_picker.title == "Select model" && model_picker.selected_item_index == 1 && model_picker.items.size() == 4 && model_picker.items[1].current &&
             model_picker.items[0].enabled && model_picker.items[0].group == "Anthropic" && model_picker.items[1].group == "OpenAI" &&
             model_picker.items[2].group == "OpenAI" && model_picker.items[2].enabled && model_picker.items[3].group == "Unregistered" &&
             !model_picker.items[3].enabled && model_picker.items[3].disabled_reason.find("provider unavailable") != std::string::npos,
         "model picker view groups human labels by provider and preserves current and disabled semantics");
  expect(model_picker.items[1].value == "openai/gpt-5.5" && model_picker.items[1].description.empty() && model_picker.items[1].detail.empty() &&
             model_picker.items[1].badge.empty() && model_picker.subtitle.empty() &&
             std::ranges::all_of(model_picker.items,
                                 [](ava::tui::SelectListItemView const& item) {
                                   auto const resting = item.description + item.detail + item.badge;
                                   return resting.find("reasoning") == std::string::npos && resting.find("tools") == std::string::npos &&
                                          resting.find("diagnostics") == std::string::npos && resting.find("context") == std::string::npos;
                                 }),
         "model picker keeps canonical values while resting rows omit backend capability and validation prose");
  for (auto const& [width, height] : std::vector<std::pair<std::size_t, std::size_t>>{{120, 36}, {80, 24}, {100, 12}})
  {
    auto visible_model_picker = model_picker;
    visible_model_picker.selected_item_index = 2;
    auto model_snapshot = ava::tui::ComposerSnapshot{.mode = "build",
                                                     .provider = "openai",
                                                     .model = "gpt-5.5",
                                                     .session_id = "session_test",
                                                     .input = "",
                                                     .status = "selecting model",
                                                     .transcript = {},
                                                     .select_list = visible_model_picker,
                                                     .width = width,
                                                     .height = height};
    auto const frame = ava::tui::render_composer(model_snapshot);
    auto const selected_line = std::ranges::find_if(frame, [](std::string const& line) {
      auto const visible = strip_sgr(line);
      return visible.find("›") != std::string::npos && visible.find("Diagnostic Local") != std::string::npos;
    });
    auto const resting = tui_test_support::join_visible_lines(frame);
    expect(selected_line != frame.end() && resting.find("openai/gpt-5.5") == std::string::npos && resting.find("reasoning") == std::string::npos &&
               resting.find("tools") == std::string::npos && resting.find("diagnostics") == std::string::npos &&
               std::ranges::none_of(frame, [](std::string const& line) { return line.find("\x1b[7m") != std::string::npos; }),
           "model selector keeps its selected quiet row visible without canonical or capability dumps at required terminal sizes");
    if (selected_line != frame.end())
    {
      auto const hit =
          ava::tui::select_list_selection_for_screen_position(model_snapshot, static_cast<std::size_t>(selected_line - frame.begin()) + 1, width / 2);
      expect(hit && *hit == 2, "model selector mouse hit-testing shares the exact required-size rendered row window");
    }
  }
  auto const scoped_all_models =
      ava::app::scoped_model_selector_view(model_registry, model_registry.models.front(), std::nullopt, "Enter toggle · Ctrl+X clear");
  auto const scoped_ordered_models = ava::app::scoped_model_selector_view(
      model_registry, model_registry.models.front(),
      std::optional<std::vector<std::string>>{std::vector<std::string>{"anthropic/claude-sonnet-4-5", "openai/gpt-5.5"}}, "Enter toggle · Ctrl+X clear");
  expect(scoped_all_models.title == "Scoped model cycle" && scoped_all_models.subtitle.find("All registered models enabled") != std::string::npos &&
             scoped_all_models.items.size() == 4 && scoped_all_models.items[0].badge == "enabled" && scoped_all_models.items[0].description.empty() &&
             !scoped_all_models.items[3].enabled && scoped_all_models.items[3].disabled_reason.find("provider unavailable") != std::string::npos &&
             scoped_ordered_models.items[0].value == "anthropic/claude-sonnet-4-5" && scoped_ordered_models.items[0].badge == "enabled" &&
             scoped_ordered_models.items[1].value == "openai/gpt-5.5" && scoped_ordered_models.items[2].value == "openai/diagnostic-local" &&
             scoped_ordered_models.items[2].badge == "disabled" && scoped_ordered_models.footer_hint.find("Ctrl+X") != std::string::npos,
         "scoped model selector view marks session cycle scope and preserves explicit enabled order before disabled rows");

  std::vector<ava::session::SessionSummary> session_summaries{
      ava::session::SessionSummary{
          .session_id = "session_beta", .path = "/tmp/ava/sessions/beta.jsonl", .last_updated = "2026-05-06T10:00:00Z", .entry_count = 12},
      ava::session::SessionSummary{
          .session_id = "session_alpha", .path = "/tmp/ava/sessions/alpha.jsonl", .last_updated = "2026-05-06T09:00:00Z", .entry_count = 4},
      ava::session::SessionSummary{
          .session_id = "session_gamma", .path = "/var/tmp/ava/gamma.jsonl", .last_updated = "2026-05-06T11:00:00Z", .entry_count = 20}};
  auto const recent_sessions =
      ava::app::session_selector_view(session_summaries, "session_beta", ava::app::SessionSelectorSort::Recent, "Enter choose · Esc cancel");
  auto by_name_sessions = ava::app::session_selector_view(session_summaries, "session_beta", ava::app::SessionSelectorSort::Name, {});
  auto by_path_sessions = ava::app::session_selector_view(session_summaries, "session_beta", ava::app::SessionSelectorSort::Path, {}, true);
  by_path_sessions.query = "gamma.jsonl";
  auto const path_matches = ava::tui::filter_select_list_items(by_path_sessions);
  expect(ava::app::session_selector_sort_label(ava::app::SessionSelectorSort::Recent) == "recent" &&
             ava::app::next_session_selector_sort(ava::app::SessionSelectorSort::Recent) == ava::app::SessionSelectorSort::Name &&
             ava::app::next_session_selector_sort(ava::app::SessionSelectorSort::Name) == ava::app::SessionSelectorSort::Path &&
             ava::app::next_session_selector_sort(ava::app::SessionSelectorSort::Path) == ava::app::SessionSelectorSort::Recent &&
             recent_sessions.title == "Select session" && recent_sessions.subtitle.find("sort recent") != std::string::npos &&
             recent_sessions.items.size() == 3 && recent_sessions.items[0].value == "session_gamma" && recent_sessions.items[1].current &&
             recent_sessions.selected_item_index == 1 && recent_sessions.items[1].label == "Untitled session" && recent_sessions.items[1].description.empty() &&
             recent_sessions.items[1].detail.empty() && by_name_sessions.items[0].value == "session_alpha" &&
             by_path_sessions.items[0].description.find("/tmp/ava/sessions/alpha.jsonl") != std::string::npos && path_matches.size() == 1 &&
             by_path_sessions.items[path_matches.front()].value == "session_gamma",
         "session selector view sorts by recent/name/path while disclosing paths only in explicit path mode");

  ava::session::SessionTreeIndex session_tree;
  session_tree.current_session_id = "session_child";
  session_tree.roots = {"session_parent"};
  session_tree.leaves = {"session_child"};
  session_tree.current_path = {"session_parent", "session_child"};
  session_tree.sessions = {ava::session::SessionTreeNode{.summary = ava::session::SessionSummary{.session_id = "session_parent",
                                                                                                 .path = "/tmp/ava/sessions/parent.jsonl",
                                                                                                 .last_updated = "2026-05-06T08:00:00Z",
                                                                                                 .entry_count = 6},
                                                         .metadata = ava::session::SessionMetadataView{.session_id = "session_parent",
                                                                                                       .name = "Parent session",
                                                                                                       .labels = {"root"},
                                                                                                       .labels_updated = "2026-05-06T08:05:00Z",
                                                                                                       .parent_session_id = {},
                                                                                                       .source_session_id = {},
                                                                                                       .branch_from_entry_id = {},
                                                                                                       .branch_origin = "root",
                                                                                                       .actor = "test"},
                                                         .children = {"session_child"},
                                                         .current = false},
                           ava::session::SessionTreeNode{.summary = ava::session::SessionSummary{.session_id = "session_child",
                                                                                                 .path = "/tmp/ava/sessions/child.jsonl",
                                                                                                 .last_updated = "2026-05-06T10:00:00Z",
                                                                                                 .entry_count = 11},
                                                         .metadata = ava::session::SessionMetadataView{.session_id = "session_child",
                                                                                                       .name = "Review branch",
                                                                                                       .labels = {"review", "ui"},
                                                                                                       .labels_updated = "2026-05-06T10:05:00Z",
                                                                                                       .parent_session_id = "session_parent",
                                                                                                       .source_session_id = "session_parent",
                                                                                                       .branch_from_entry_id = "entry_1",
                                                                                                       .branch_origin = "fork",
                                                                                                       .actor = "rpc"},
                                                         .children = {},
                                                         .current = true}};
  auto tree_sessions = ava::app::session_selector_view(session_tree, ava::app::SessionSelectorSort::Name, {});
  auto tree_sessions_with_label_time = ava::app::session_selector_view(session_tree, ava::app::SessionSelectorSort::Name, {}, false, true, false, true);
  tree_sessions.query = "review";
  auto const tree_matches = ava::tui::filter_select_list_items(tree_sessions);
  expect(tree_sessions.subtitle.find("sort name") != std::string::npos && tree_sessions.items.size() == 2 &&
             tree_sessions.subtitle.find("label time") == std::string::npos && tree_sessions.items[0].label == "Parent session" &&
             tree_sessions.items[0].detail.empty() && tree_sessions.items[0].badge.empty() &&
             tree_sessions.items[1].label.find("↳ Review branch") != std::string::npos && tree_sessions.items[1].current &&
             tree_sessions.items[1].badge.empty() && tree_sessions.items[1].description == "review, ui" && tree_sessions.items[1].detail.empty() &&
             tree_sessions_with_label_time.subtitle.find("label times") != std::string::npos &&
             tree_sessions_with_label_time.items[1].description.find("review, ui · labels updated 2026-05-06T10:05:00Z") != std::string::npos &&
             tree_matches.size() == 1 && tree_sessions.items[tree_matches.front()].value == "session_child",
         "session selector view presents title hierarchy, current marker, labels, optional label time, and searchable canonical values without raw metadata");
  for (auto const& [width, height] : std::vector<std::pair<std::size_t, std::size_t>>{{120, 36}, {80, 24}, {100, 12}})
  {
    auto visible_sessions = tree_sessions;
    visible_sessions.query.clear();
    auto session_snapshot = ava::tui::ComposerSnapshot{.mode = "build",
                                                       .provider = "openai",
                                                       .model = "gpt-5.5",
                                                       .session_id = "session_child",
                                                       .input = "",
                                                       .status = "selecting session",
                                                       .transcript = {},
                                                       .select_list = visible_sessions,
                                                       .width = width,
                                                       .height = height};
    auto const frame = ava::tui::render_composer(session_snapshot);
    auto const selected_line = std::ranges::find_if(frame, [](std::string const& line) {
      auto const visible = strip_sgr(line);
      return visible.find("›") != std::string::npos && visible.find("Review branch") != std::string::npos;
    });
    auto const resting = tui_test_support::join_visible_lines(frame);
    expect(selected_line != frame.end() && resting.find("session_child") == std::string::npos && resting.find("child.jsonl") == std::string::npos &&
               resting.find("current current") == std::string::npos && resting.find("Ctrl+D archive") != std::string::npos &&
               std::ranges::none_of(frame, [](std::string const& line) { return line.find("\x1b[7m") != std::string::npos; }),
           "session selector keeps its selected title row visible without ids, default paths, duplicate current text, or reverse video at required sizes");
    if (selected_line != frame.end())
    {
      auto const hit =
          ava::tui::select_list_selection_for_screen_position(session_snapshot, static_cast<std::size_t>(selected_line - frame.begin()) + 1, width / 2);
      expect(hit && *hit == 1, "session selector mouse hit-testing shares the exact required-size rendered row window");
    }
  }
  auto const parent_target = ava::app::session_selector_parent_target(session_tree, "session_child");
  auto const child_target = ava::app::session_selector_child_target(session_tree, "session_parent", ava::app::SessionSelectorSort::Name);
  expect(parent_target && *parent_target == "session_parent" && child_target && *child_target == "session_child" &&
             !ava::app::session_selector_parent_target(session_tree, "session_parent") &&
             !ava::app::session_selector_child_target(session_tree, "session_child", ava::app::SessionSelectorSort::Name),
         "session selector branch targets resolve parent and first visible child links from tree metadata");

  auto tree_with_unnamed = session_tree;
  tree_with_unnamed.roots.push_back("session_unnamed");
  tree_with_unnamed.sessions.push_back(ava::session::SessionTreeNode{
      .summary =
          ava::session::SessionSummary{
              .session_id = "session_unnamed", .path = "/tmp/ava/sessions/unnamed.jsonl", .last_updated = "2026-05-06T11:00:00Z", .entry_count = 2},
      .metadata = ava::session::SessionMetadataView{.session_id = "session_unnamed",
                                                    .name = {},
                                                    .labels = {},
                                                    .parent_session_id = {},
                                                    .source_session_id = {},
                                                    .branch_from_entry_id = {},
                                                    .branch_origin = "root",
                                                    .actor = "test"},
      .children = {},
      .current = false});
  auto named_only_sessions = ava::app::session_selector_view(tree_with_unnamed, ava::app::SessionSelectorSort::Name, "Ctrl+N show all", true);
  named_only_sessions.query = "unnamed";
  auto const named_only_matches = ava::tui::filter_select_list_items(named_only_sessions);
  expect(named_only_sessions.subtitle.find("named") != std::string::npos && named_only_sessions.footer_hint.find("Ctrl+N show all") != std::string::npos &&
             named_only_sessions.items.size() == 2 &&
             std::ranges::none_of(named_only_sessions.items, [](ava::tui::SelectListItemView const& item) { return item.value == "session_unnamed"; }) &&
             named_only_matches.empty(),
         "session selector named-only filter hides unnamed sessions while preserving named branch rows");

  auto path_hidden_sessions = ava::app::session_selector_view(tree_with_unnamed, ava::app::SessionSelectorSort::Name, "Ctrl+P show paths", false, false);
  expect(path_hidden_sessions.subtitle.find("paths") == std::string::npos && path_hidden_sessions.footer_hint.find("Ctrl+P show paths") != std::string::npos &&
             path_hidden_sessions.items.size() == 3 &&
             std::ranges::all_of(path_hidden_sessions.items,
                                 [](ava::tui::SelectListItemView const& item) {
                                   return item.description.find(".jsonl") == std::string::npos && item.description.find("/tmp/") == std::string::npos &&
                                          item.label.find("session_") == std::string::npos;
                                 }),
         "session selector hides session ids and file paths until explicit path disclosure");

  auto tree_with_archived = tree_with_unnamed;
  tree_with_archived.roots.push_back("session_archived");
  tree_with_archived.sessions.push_back(ava::session::SessionTreeNode{
      .summary =
          ava::session::SessionSummary{
              .session_id = "session_archived", .path = "/tmp/ava/sessions/archived.jsonl", .last_updated = "2026-05-06T12:00:00Z", .entry_count = 3},
      .metadata = ava::session::SessionMetadataView{.session_id = "session_archived",
                                                    .name = "Archived branch",
                                                    .labels = {"old"},
                                                    .archived = true,
                                                    .parent_session_id = {},
                                                    .source_session_id = {},
                                                    .branch_from_entry_id = {},
                                                    .branch_origin = "manual",
                                                    .actor = "test"},
      .children = {},
      .current = false});
  auto active_session_selector = ava::app::session_selector_view(tree_with_archived, ava::app::SessionSelectorSort::Name, {}, false, true);
  auto archived_session_selector = ava::app::session_selector_view(tree_with_archived, ava::app::SessionSelectorSort::Name, {}, false, true, true);
  expect(std::ranges::none_of(active_session_selector.items, [](ava::tui::SelectListItemView const& item) { return item.value == "session_archived"; }) &&
             std::ranges::any_of(archived_session_selector.items,
                                 [](ava::tui::SelectListItemView const& item) {
                                   return item.value == "session_archived" && item.badge.find("archived") != std::string::npos && item.detail.empty();
                                 }) &&
             archived_session_selector.subtitle.find("archived") != std::string::npos,
         "session selector hides archived sessions by default and can render them when explicitly requested");

  auto const session_selector_frame = ava::tui::render_composer(ava::tui::ComposerSnapshot{.mode = "build",
                                                                                           .provider = "openai",
                                                                                           .model = "gpt-5.5",
                                                                                           .session_id = "session_beta",
                                                                                           .input = "composer behind session selector",
                                                                                           .status = "selecting session",
                                                                                           .transcript = {},
                                                                                           .select_list = recent_sessions,
                                                                                           .width = 96,
                                                                                           .height = 18});
  expect(std::ranges::any_of(session_selector_frame, [](std::string const& line) { return strip_sgr(line).find("Select session") != std::string::npos; }) &&
             std::ranges::any_of(session_selector_frame,
                                 [](std::string const& line) {
                                   auto const visible = strip_sgr(line);
                                   return visible.find("● Untitled session") != std::string::npos && visible.find("session_beta") == std::string::npos &&
                                          visible.find("beta.jsonl") == std::string::npos;
                                 }),
         "session selector renders a calm current-title row without raw session ids or default paths");

  auto hotkeys_view = ava::tui::hotkeys_select_list_view(ava::tui::default_key_bindings());
  hotkeys_view.query = "mode";
  auto const hotkeys_frame = ava::tui::render_composer(ava::tui::ComposerSnapshot{.mode = "build",
                                                                                  .provider = "openai",
                                                                                  .model = "gpt-5.5",
                                                                                  .session_id = "session_test",
                                                                                  .input = "composer behind hotkeys",
                                                                                  .status = "hotkeys opened",
                                                                                  .transcript = {},
                                                                                  .select_list = hotkeys_view,
                                                                                  .width = 92,
                                                                                  .height = 18});
  expect(hotkeys_view.title == "Keybindings" && hotkeys_view.subtitle.find("$XDG_CONFIG_HOME/ava/keybinds.json") != std::string::npos &&
             hotkeys_view.subtitle.find("Enter drafts /keybindings set") != std::string::npos &&
             hotkeys_view.subtitle.find("/reload keybindings") != std::string::npos && hotkeys_view.footer_hint.find("Enter draft edit") != std::string::npos &&
             std::ranges::any_of(hotkeys_view.items,
                                 [](ava::tui::SelectListItemView const& item) {
                                   return item.value == "mode_toggle" && item.detail.find("Tab") != std::string::npos && item.badge == "shared key";
                                 }) &&
             std::ranges::any_of(hotkeys_frame, [](std::string const& line) { return strip_sgr(line).find("Keybindings") != std::string::npos; }) &&
             std::ranges::any_of(hotkeys_frame, [](std::string const& line) { return strip_sgr(line).find("mode_toggle") != std::string::npos; }) &&
             std::ranges::none_of(hotkeys_frame, [](std::string const& line) { return line.find("\x1b[7m") != std::string::npos; }),
         "keybindings view exposes active bindings, edit drafting, config/reload guidance, and context-shared keys");

  ava::tui::set_tui_config_theme(std::nullopt);
  auto settings_key_bindings = ava::tui::default_key_bindings();
  auto const parsed_settings_key_bindings = ava::tui::parse_key_bindings_json("{\"tui.editor.cursorLeft\":\"Alt+H\",\"app.tools.expand\":\"Ctrl+O\"}");
  expect(parsed_settings_key_bindings.has_value(), "settings view test keybindings parse through the production loader");
  if (parsed_settings_key_bindings)
    settings_key_bindings = *parsed_settings_key_bindings;
  auto const settings_view = ava::tui::settings_select_list_view(
      ava::tui::ComposerSnapshot{.mode = "build",
                                 .provider = "openai",
                                 .model = "gpt-5.5",
                                 .session_id = "session_test",
                                 .input = "",
                                 .status = "ready",
                                 .token_status = "1.3k (0.7%)",
                                 .reasoning_status = "low",
                                 .transcript = {},
                                 .sidebar = ava::tui::SidebarSnapshot{.session_id = "session_test",
                                                                      .mode = "build",
                                                                      .provider = "openai",
                                                                      .model = "gpt-5.5",
                                                                      .workspace = "/workspace/project",
                                                                      .git_branch = "develop",
                                                                      .version = "0.32",
                                                                      .token_status = "1.3k (0.7%)",
                                                                      .reasoning_status = "low",
                                                                      .context_source_count = 2,
                                                                      .session_path = "/tmp/ava/session_test.jsonl",
                                                                      .session_entry_count = 42},
                                 .project_trust = ava::tui::ProjectTrustSnapshot{.decision = "trusted",
                                                                                 .project_resources = "enabled",
                                                                                 .workspace = "/workspace/project",
                                                                                 .matched_path = "/workspace/project",
                                                                                 .trust_file = "/tmp/ava/project-trust.json",
                                                                                 .protected_resource_count = 3}},
      settings_key_bindings);
  auto const settings_frame = ava::tui::render_composer(ava::tui::ComposerSnapshot{.mode = "build",
                                                                                   .provider = "openai",
                                                                                   .model = "gpt-5.5",
                                                                                   .session_id = "session_test",
                                                                                   .input = "",
                                                                                   .status = "settings opened",
                                                                                   .transcript = {},
                                                                                   .select_list = settings_view,
                                                                                   .width = 96,
                                                                                   .height = 20});
  expect(std::ranges::any_of(
             settings_view.items,
             [](ava::tui::SelectListItemView const& item) { return item.label == "Theme" && item.description == "ava-dark" && item.badge == "built-in"; }) &&
             std::ranges::any_of(settings_view.items,
                                 [](ava::tui::SelectListItemView const& item) {
                                   return item.value == "theme:dark" && item.label == "Theme dark" && item.detail == "persist to display.json" &&
                                          item.badge == "current" && item.current;
                                 }) &&
             std::ranges::any_of(settings_view.items,
                                 [](ava::tui::SelectListItemView const& item) {
                                   return item.value == "theme:light" && item.label == "Theme light" && item.detail == "persist to display.json" &&
                                          item.badge == "select" && !item.current;
                                 }) &&
             std::ranges::any_of(settings_view.items,
                                 [](ava::tui::SelectListItemView const& item) {
                                   return item.label == "Current directory" && item.description == "/workspace/project" && item.detail == "project";
                                 }) &&
             std::ranges::any_of(settings_view.items,
                                 [](ava::tui::SelectListItemView const& item) {
                                   return item.label == "Project trust" && item.description == "trusted" && item.detail == "project resources enabled" &&
                                          item.badge == "trusted";
                                 }) &&
             std::ranges::any_of(settings_view.items,
                                 [](ava::tui::SelectListItemView const& item) {
                                   return item.label == "Protected resources" && item.description == "3 protected project resources" &&
                                          item.detail == "matched /workspace/project";
                                 }) &&
             std::ranges::any_of(settings_view.items,
                                 [](ava::tui::SelectListItemView const& item) {
                                   return item.label == "Trust status" && item.value == "settings:trust.status" && item.description == "/trust status" &&
                                          item.badge == "status";
                                 }) &&
             std::ranges::any_of(settings_view.items,
                                 [](ava::tui::SelectListItemView const& item) {
                                   return item.label == "Trust project" && item.value == "settings:trust.project" &&
                                          item.detail == "Enter runs /trust project" && item.badge == "trust";
                                 }) &&
             std::ranges::any_of(settings_view.items,
                                 [](ava::tui::SelectListItemView const& item) {
                                   return item.label == "Deny project" && item.value == "settings:trust.deny" && item.detail == "Enter runs /trust deny" &&
                                          item.badge == "deny";
                                 }) &&
             std::ranges::any_of(settings_view.items,
                                 [](ava::tui::SelectListItemView const& item) {
                                   return item.label == "Clear trust decision" && item.value == "settings:trust.clear" &&
                                          item.detail == "Enter runs /trust clear" && item.badge == "clear";
                                 }) &&
             std::ranges::any_of(settings_view.items,
                                 [](ava::tui::SelectListItemView const& item) {
                                   return item.label == "Image preview" && !item.description.empty() && !item.detail.empty() && !item.badge.empty();
                                 }) &&
             std::ranges::any_of(settings_view.items,
                                 [](ava::tui::SelectListItemView const& item) {
                                   return item.label == "Keybindings" && item.value == "settings:keybindings.open" &&
                                          item.description.find("active actions") != std::string::npos && item.detail == "Enter opens active bindings" &&
                                          item.badge == "open";
                                 }) &&
             std::ranges::any_of(settings_view.items,
                                 [](ava::tui::SelectListItemView const& item) {
                                   return item.label == "Keybindings file" && item.value == "settings:keybindings.validate" &&
                                          item.description == "$XDG_CONFIG_HOME/ava/keybinds.json" &&
                                          item.detail == "Enter validates with /keybindings validate" && item.badge == "validate";
                                 }) &&
             std::ranges::any_of(settings_view.items,
                                 [](ava::tui::SelectListItemView const& item) {
                                   return item.label == "Keybindings edit" && item.value == "settings:keybindings.edit" &&
                                          item.description == "/keybindings set <action> <key>" &&
                                          item.detail == "Enter drafts the edit command; reset removes one override" && item.badge == "draft";
                                 }) &&
             std::ranges::any_of(settings_view.items,
                                 [](ava::tui::SelectListItemView const& item) {
                                   return item.label == "Keybindings reload" && item.value == "settings:keybindings.reload" &&
                                          item.description == "/reload keybindings" && item.detail == "Enter applies valid keybinds.json edits" &&
                                          item.badge == "live";
                                 }) &&
             std::ranges::any_of(settings_view.items,
                                 [](ava::tui::SelectListItemView const& item) {
                                   return item.label == "Model selector" && item.value == "settings:models.open" && item.description == "openai/gpt-5.5" &&
                                          item.detail == "Enter opens /models selector" && item.badge == "open";
                                 }) &&
             std::ranges::any_of(settings_view.items,
                                 [](ava::tui::SelectListItemView const& item) {
                                   return item.label == "Model cycle scope" && item.value == "settings:models.scoped" &&
                                          item.description == "Ctrl+P scoped cycle" && item.detail.find("/scoped-models") != std::string::npos &&
                                          item.detail.find("Ctrl+S") != std::string::npos && item.badge == "open";
                                 }) &&
             std::ranges::any_of(settings_frame, [](std::string const& line) { return strip_sgr(line).find("Settings") != std::string::npos; }) &&
             std::ranges::any_of(settings_frame, [](std::string const& line) { return strip_sgr(line).find("ava-dark") != std::string::npos; }),
         "settings view exposes runtime, workspace, built-in theme status, and selectable theme rows");

  {
    ScopedEnvVar no_color_settings_guard("NO_COLOR", "");
    ScopedEnvVar light_theme_guard("AVA_TUI_THEME", "light");
    ScopedEnvVar colorfgbg_guard("COLORFGBG", "");
    ava::tui::set_tui_config_theme(std::nullopt);
    auto const light_settings_view =
        ava::tui::settings_select_list_view(ava::tui::ComposerSnapshot{.mode = "build",
                                                                       .provider = "openai",
                                                                       .model = "gpt-5.5",
                                                                       .session_id = "session_test",
                                                                       .input = "",
                                                                       .status = "ready",
                                                                       .token_status = "1.3k (0.7%)",
                                                                       .reasoning_status = "low",
                                                                       .transcript = {},
                                                                       .sidebar = ava::tui::SidebarSnapshot{.session_id = "session_test",
                                                                                                            .mode = "build",
                                                                                                            .provider = "openai",
                                                                                                            .model = "gpt-5.5",
                                                                                                            .workspace = "/workspace/project",
                                                                                                            .git_branch = "develop",
                                                                                                            .version = "0.32",
                                                                                                            .token_status = "1.3k (0.7%)",
                                                                                                            .reasoning_status = "low",
                                                                                                            .context_source_count = 2,
                                                                                                            .session_path = "/tmp/ava/session_test.jsonl",
                                                                                                            .session_entry_count = 42}});
    expect(std::ranges::any_of(light_settings_view.items,
                               [](ava::tui::SelectListItemView const& item) {
                                 return item.label == "Theme" && item.description == "ava-light" && item.detail == "built-in light ncurses token palette" &&
                                        item.badge == "AVA_TUI_THEME";
                               }),
           "settings view reports process-selected built-in light theme from AVA_TUI_THEME");
  }

  {
    ScopedEnvVar no_color_settings_guard("NO_COLOR", "");
    ScopedEnvVar theme_env_guard("AVA_TUI_THEME", "");
    ScopedEnvVar colorfgbg_guard("COLORFGBG", "");
    ava::tui::set_tui_config_theme("light");
    auto const configured_light_settings_view =
        ava::tui::settings_select_list_view(ava::tui::ComposerSnapshot{.mode = "build",
                                                                       .provider = "openai",
                                                                       .model = "gpt-5.5",
                                                                       .session_id = "session_test",
                                                                       .input = "",
                                                                       .status = "ready",
                                                                       .token_status = "1.3k (0.7%)",
                                                                       .reasoning_status = "low",
                                                                       .transcript = {},
                                                                       .sidebar = ava::tui::SidebarSnapshot{.session_id = "session_test",
                                                                                                            .mode = "build",
                                                                                                            .provider = "openai",
                                                                                                            .model = "gpt-5.5",
                                                                                                            .workspace = "/workspace/project",
                                                                                                            .git_branch = "develop",
                                                                                                            .version = "0.32",
                                                                                                            .token_status = "1.3k (0.7%)",
                                                                                                            .reasoning_status = "low",
                                                                                                            .context_source_count = 2,
                                                                                                            .session_path = "/tmp/ava/session_test.jsonl",
                                                                                                            .session_entry_count = 42}});
    expect(std::ranges::any_of(configured_light_settings_view.items,
                               [](ava::tui::SelectListItemView const& item) {
                                 return item.label == "Theme" && item.description == "ava-light" && item.detail == "built-in light ncurses token palette" &&
                                        item.badge == "display.json";
                               }) &&
               std::ranges::any_of(
                   configured_light_settings_view.items,
                   [](ava::tui::SelectListItemView const& item) { return item.value == "theme:light" && item.badge == "current" && item.current; }),
           "settings view reports persisted built-in light theme from display.json");
    ava::tui::set_tui_config_theme(std::nullopt);
  }

  {
    ScopedEnvVar no_color_settings_guard("NO_COLOR", "");
    ScopedEnvVar theme_env_guard("AVA_TUI_THEME", "");
    ScopedEnvVar colorfgbg_guard("COLORFGBG", "");
    auto custom_theme = ava::tui::TuiCustomTheme{
        .name = "sunrise",
        .path = "/tmp/ava/sunrise.json",
        .palette =
            ava::tui::TuiThemePalette{
                .text = -1, .muted = 242, .success = 34, .warning = 220, .error = 196, .accent = 39, .screen_bg = 255, .composer_bg = 236},
        .revision = "test-custom-theme"};
    ava::tui::set_tui_config_theme("sunrise", custom_theme);
    auto const custom_settings_view = ava::tui::settings_select_list_view(
        ava::tui::ComposerSnapshot{.mode = "build",
                                   .provider = "openai",
                                   .model = "gpt-5.5",
                                   .session_id = "session_test",
                                   .input = "",
                                   .status = "ready",
                                   .token_status = "1.3k (0.7%)",
                                   .reasoning_status = "low",
                                   .transcript = {},
                                   .custom_themes = {ava::tui::ThemeOptionItem{.name = "sunrise", .detail = "/tmp/ava/sunrise.json"}},
                                   .sidebar = ava::tui::SidebarSnapshot{.session_id = "session_test",
                                                                        .mode = "build",
                                                                        .provider = "openai",
                                                                        .model = "gpt-5.5",
                                                                        .workspace = "/workspace/project",
                                                                        .git_branch = "develop",
                                                                        .version = "0.32",
                                                                        .token_status = "1.3k (0.7%)",
                                                                        .reasoning_status = "low",
                                                                        .context_source_count = 2,
                                                                        .session_path = "/tmp/ava/session_test.jsonl",
                                                                        .session_entry_count = 42}});
    expect(std::ranges::any_of(custom_settings_view.items,
                               [](ava::tui::SelectListItemView const& item) {
                                 return item.label == "Theme" && item.description == "sunrise" && item.detail.find("sunrise.json") != std::string::npos &&
                                        item.badge == "display.json";
                               }) &&
               std::ranges::any_of(custom_settings_view.items,
                                   [](ava::tui::SelectListItemView const& item) {
                                     return item.value == "theme:sunrise" && item.label == "Theme sunrise" && item.description == "custom theme" &&
                                            item.detail.find("sunrise.json") != std::string::npos && item.badge == "current" && item.current;
                                   }),
           "settings view reports and exposes a selected custom TUI theme from display.json");
    ava::tui::set_tui_config_theme(std::nullopt);
  }

  {
    ScopedEnvVar no_color_settings_guard("NO_COLOR", "");
    ScopedEnvVar theme_env_guard("AVA_TUI_THEME", "");
    ScopedEnvVar colorfgbg_guard("COLORFGBG", "0;15");
    ava::tui::set_tui_config_theme(std::nullopt);
    auto const terminal_light_settings_view =
        ava::tui::settings_select_list_view(ava::tui::ComposerSnapshot{.mode = "build",
                                                                       .provider = "openai",
                                                                       .model = "gpt-5.5",
                                                                       .session_id = "session_test",
                                                                       .input = "",
                                                                       .status = "ready",
                                                                       .token_status = "1.3k (0.7%)",
                                                                       .reasoning_status = "low",
                                                                       .transcript = {},
                                                                       .sidebar = ava::tui::SidebarSnapshot{.session_id = "session_test",
                                                                                                            .mode = "build",
                                                                                                            .provider = "openai",
                                                                                                            .model = "gpt-5.5",
                                                                                                            .workspace = "/workspace/project",
                                                                                                            .git_branch = "develop",
                                                                                                            .version = "0.32",
                                                                                                            .token_status = "1.3k (0.7%)",
                                                                                                            .reasoning_status = "low",
                                                                                                            .context_source_count = 2,
                                                                                                            .session_path = "/tmp/ava/session_test.jsonl",
                                                                                                            .session_entry_count = 42}});
    expect(std::ranges::any_of(terminal_light_settings_view.items,
                               [](ava::tui::SelectListItemView const& item) {
                                 return item.label == "Theme" && item.description == "ava-light" &&
                                        item.detail == "terminal background appears light from COLORFGBG" && item.badge == "COLORFGBG";
                               }) &&
               std::ranges::any_of(
                   terminal_light_settings_view.items,
                   [](ava::tui::SelectListItemView const& item) { return item.value == "theme:light" && item.badge == "current" && item.current; }),
           "settings view reports terminal-background light theme inferred from COLORFGBG");
  }

  {
    ScopedEnvVar no_color_settings_guard("NO_COLOR", "");
    ScopedEnvVar theme_env_guard("AVA_TUI_THEME", "");
    ScopedEnvVar colorfgbg_guard("COLORFGBG", "15;0");
    ava::tui::set_tui_config_theme(std::nullopt);
    auto const terminal_dark_settings_view =
        ava::tui::settings_select_list_view(ava::tui::ComposerSnapshot{.mode = "build",
                                                                       .provider = "openai",
                                                                       .model = "gpt-5.5",
                                                                       .session_id = "session_test",
                                                                       .input = "",
                                                                       .status = "ready",
                                                                       .token_status = "1.3k (0.7%)",
                                                                       .reasoning_status = "low",
                                                                       .transcript = {},
                                                                       .sidebar = ava::tui::SidebarSnapshot{.session_id = "session_test",
                                                                                                            .mode = "build",
                                                                                                            .provider = "openai",
                                                                                                            .model = "gpt-5.5",
                                                                                                            .workspace = "/workspace/project",
                                                                                                            .git_branch = "develop",
                                                                                                            .version = "0.32",
                                                                                                            .token_status = "1.3k (0.7%)",
                                                                                                            .reasoning_status = "low",
                                                                                                            .context_source_count = 2,
                                                                                                            .session_path = "/tmp/ava/session_test.jsonl",
                                                                                                            .session_entry_count = 42}});
    expect(
        std::ranges::any_of(terminal_dark_settings_view.items,
                            [](ava::tui::SelectListItemView const& item) {
                              return item.label == "Theme" && item.description == "ava-dark" &&
                                     item.detail == "terminal background appears dark from COLORFGBG" && item.badge == "COLORFGBG";
                            }) &&
            std::ranges::any_of(terminal_dark_settings_view.items,
                                [](ava::tui::SelectListItemView const& item) { return item.value == "theme:dark" && item.badge == "current" && item.current; }),
        "settings view reports terminal-background dark theme inferred from COLORFGBG");
  }

  {
    ScopedEnvVar no_color_settings_guard("NO_COLOR", "");
    ScopedEnvVar plain_theme_guard("AVA_TUI_THEME", "plain");
    ScopedEnvVar colorfgbg_guard("COLORFGBG", "");
    ava::tui::set_tui_config_theme(std::nullopt);
    auto const env_plain_settings_view =
        ava::tui::settings_select_list_view(ava::tui::ComposerSnapshot{.mode = "build",
                                                                       .provider = "openai",
                                                                       .model = "gpt-5.5",
                                                                       .session_id = "session_test",
                                                                       .input = "",
                                                                       .status = "ready",
                                                                       .token_status = "1.3k (0.7%)",
                                                                       .reasoning_status = "low",
                                                                       .transcript = {},
                                                                       .sidebar = ava::tui::SidebarSnapshot{.session_id = "session_test",
                                                                                                            .mode = "build",
                                                                                                            .provider = "openai",
                                                                                                            .model = "gpt-5.5",
                                                                                                            .workspace = "/workspace/project",
                                                                                                            .git_branch = "develop",
                                                                                                            .version = "0.32",
                                                                                                            .token_status = "1.3k (0.7%)",
                                                                                                            .reasoning_status = "low",
                                                                                                            .context_source_count = 2,
                                                                                                            .session_path = "/tmp/ava/session_test.jsonl",
                                                                                                            .session_entry_count = 42}});
    auto const env_plain_settings_frame = ava::tui::render_composer(ava::tui::ComposerSnapshot{.mode = "build",
                                                                                               .provider = "openai",
                                                                                               .model = "gpt-5.5",
                                                                                               .session_id = "session_test",
                                                                                               .input = "",
                                                                                               .status = "settings opened",
                                                                                               .transcript = {},
                                                                                               .select_list = env_plain_settings_view,
                                                                                               .width = 96,
                                                                                               .height = 20});
    expect(std::ranges::any_of(env_plain_settings_view.items,
                               [](ava::tui::SelectListItemView const& item) {
                                 return item.label == "Theme" && item.description == "plain" && item.detail == "AVA_TUI_THEME=plain disables ANSI styling" &&
                                        item.badge == "AVA_TUI_THEME";
                               }) &&
               std::ranges::all_of(env_plain_settings_frame, [](std::string const& line) { return line.find("\x1b[") == std::string::npos; }),
           "AVA_TUI_THEME=plain selects the same no-ANSI rendering path as NO_COLOR without requiring NO_COLOR");
  }

  {
    ScopedEnvVar requested_theme_guard("AVA_TUI_THEME", "light");
    ScopedEnvVar no_color_settings_guard("NO_COLOR", "1");
    ScopedEnvVar colorfgbg_guard("COLORFGBG", "0;15");
    ava::tui::set_tui_config_theme(std::nullopt);
    auto const plain_settings_view =
        ava::tui::settings_select_list_view(ava::tui::ComposerSnapshot{.mode = "build",
                                                                       .provider = "openai",
                                                                       .model = "gpt-5.5",
                                                                       .session_id = "session_test",
                                                                       .input = "",
                                                                       .status = "ready",
                                                                       .token_status = "1.3k (0.7%)",
                                                                       .reasoning_status = "low",
                                                                       .transcript = {},
                                                                       .sidebar = ava::tui::SidebarSnapshot{.session_id = "session_test",
                                                                                                            .mode = "build",
                                                                                                            .provider = "openai",
                                                                                                            .model = "gpt-5.5",
                                                                                                            .workspace = "/workspace/project",
                                                                                                            .git_branch = "develop",
                                                                                                            .version = "0.32",
                                                                                                            .token_status = "1.3k (0.7%)",
                                                                                                            .reasoning_status = "low",
                                                                                                            .context_source_count = 2,
                                                                                                            .session_path = "/tmp/ava/session_test.jsonl",
                                                                                                            .session_entry_count = 42}});
    auto const plain_settings_frame = ava::tui::render_composer(ava::tui::ComposerSnapshot{.mode = "build",
                                                                                           .provider = "openai",
                                                                                           .model = "gpt-5.5",
                                                                                           .session_id = "session_test",
                                                                                           .input = "",
                                                                                           .status = "settings opened",
                                                                                           .transcript = {},
                                                                                           .select_list = plain_settings_view,
                                                                                           .width = 96,
                                                                                           .height = 20});
    expect(std::ranges::any_of(plain_settings_view.items,
                               [](ava::tui::SelectListItemView const& item) {
                                 return item.label == "Theme" && item.description == "plain" && item.detail == "NO_COLOR disables ANSI styling" &&
                                        item.badge == "NO_COLOR";
                               }) &&
               std::ranges::any_of(plain_settings_frame, [](std::string const& line) { return line.find("plain") != std::string::npos; }) &&
               std::ranges::all_of(plain_settings_frame, [](std::string const& line) { return line.find("\x1b[") == std::string::npos; }),
           "settings view reports active NO_COLOR plain display mode without ANSI styling");
  }

  std::vector<ava::app::SessionUserTurn> user_turns{
      ava::app::SessionUserTurn{.entry_id = "entry_user_a", .timestamp = "2026-05-08T00:00:01Z", .preview = "alpha first line"},
      ava::app::SessionUserTurn{.entry_id = "entry_user_b", .timestamp = "2026-05-08T00:00:03Z", .preview = "beta second"},
      ava::app::SessionUserTurn{.entry_id = "entry_user_c", .timestamp = "2026-05-08T00:00:06Z", .preview = "gamma third"},
  };
  auto fork_picker = ava::app::user_turn_selector_view(user_turns, "Fork from user turn", "Enter fork · Esc cancel");
  expect(fork_picker.title == "Fork from user turn" && fork_picker.selected_item_index == 0 && fork_picker.items.size() == 3 &&
             fork_picker.items[0].value == "entry_user_c" && fork_picker.items[0].label == "gamma third" &&
             fork_picker.items[0].detail == "2026-05-08T00:00:06Z" && fork_picker.items[1].value == "entry_user_b" &&
             fork_picker.items[2].value == "entry_user_a" && fork_picker.placeholder == "Search user turns",
         "user-turn picker lists newest public turns first with stable entry ids and bounded previews");
  fork_picker.query = "beta";
  auto const beta_matches = ava::tui::filter_select_list_items(fork_picker);
  expect(beta_matches.size() == 1 && fork_picker.items[beta_matches.front()].value == "entry_user_b", "user-turn picker filters by bounded preview text");
  auto filtered_input = ava::tui::handle_select_list_input(fork_picker, ava::tui::InputEvent{.key = ava::tui::Key::Character, .character = 'x'});
  expect(filtered_input.action == ava::tui::SelectListInputAction::Redraw && filtered_input.query == "betax",
         "user-turn picker accepts incremental filter input without mutation");
  fork_picker.query = "beta";
  fork_picker.selected_item_index = ava::tui::clamp_select_list_selection(fork_picker, 0);
  auto resolve_input = ava::tui::handle_select_list_input(fork_picker, ava::tui::InputEvent{.key = ava::tui::Key::Enter});
  expect(resolve_input.action == ava::tui::SelectListInputAction::Resolve && resolve_input.selected_item_index == 1 &&
             fork_picker.items[resolve_input.selected_item_index].value == "entry_user_b",
         "user-turn picker Enter resolves the filtered earlier entry id rather than the tip");
  auto cancel_input = ava::tui::handle_select_list_input(fork_picker, ava::tui::InputEvent{.key = ava::tui::Key::Escape});
  expect(cancel_input.action == ava::tui::SelectListInputAction::Cancel, "user-turn picker Escape cancels without mutation");

  auto empty_result = ava::app::user_turn_selector_view(std::vector<ava::app::SessionUserTurn>{}, "Fork from user turn");
  expect(empty_result.items.empty() && empty_result.empty_text == "No user turns match",
         "user-turn picker builder retains empty-list semantics for callers that surface status without opening");

  for (auto const height : {std::size_t{8}, std::size_t{10}, std::size_t{12}})
  {
    auto tall_picker = fork_picker;
    tall_picker.query.clear();
    tall_picker.selected_item_index = 2;
    auto snapshot = ava::tui::ComposerSnapshot{.mode = "build",
                                               .provider = "openai",
                                               .model = "gpt-5.5",
                                               .session_id = "session_test",
                                               .input = "",
                                               .status = "fork-from selector opened",
                                               .transcript = {},
                                               .select_list = tall_picker,
                                               .width = 80,
                                               .height = height};
    auto const frame = ava::tui::render_composer(snapshot);
    auto const selected_line = std::ranges::find_if(frame, [](std::string const& line) {
      auto const visible = strip_sgr(line);
      return visible.find("›") != std::string::npos && visible.find("alpha first line") != std::string::npos;
    });
    expect(selected_line != frame.end(), "user-turn picker keeps the selected older turn visible at tiny terminal heights");
    if (selected_line != frame.end())
    {
      auto const hit = ava::tui::select_list_selection_for_screen_position(snapshot, static_cast<std::size_t>(selected_line - frame.begin()) + 1, 20);
      expect(hit && *hit == 2, "user-turn picker mouse hit-testing shares the rendered selected row");
    }
  }

  auto prompt_snapshot = ava::tui::ComposerSnapshot{
      .mode = "build",
      .provider = "openai",
      .model = "gpt-5.5",
      .session_id = "session_test",
      .input = "",
      .status = "permission required",
      .transcript = {},
      .permission_prompt = ava::tui::PermissionPromptView{.tool_name = "write_file", .operation = "write_file", .target = "src/main.cpp", .reason = "test"},
      .select_list = fork_picker,
      .width = 80,
      .height = 24};
  // Permission prompts outrank selectors for hit testing; the select-list path must not claim the click.
  auto const blocked_hit = ava::tui::select_list_selection_for_screen_position(prompt_snapshot, 8, 10);
  expect(!blocked_hit, "active permission prompts outrank user-turn selector hit testing");

  // Production-path evidence for ForkUserTurn / CopyUserTurn Enter resolution (F-003).
  fork_picker.query = "alpha";
  fork_picker.selected_item_index = ava::tui::clamp_select_list_selection(fork_picker, 0);
  auto const enter_resolve = ava::tui::handle_select_list_input(fork_picker, ava::tui::InputEvent{.key = ava::tui::Key::Enter});
  expect(enter_resolve.action == ava::tui::SelectListInputAction::Resolve && enter_resolve.selected_item_index < fork_picker.items.size() &&
             fork_picker.items[enter_resolve.selected_item_index].value == "entry_user_a",
         "ForkUserTurn Enter resolves the filtered earlier stable entry id");
  auto const selected_entry_id = fork_picker.items[enter_resolve.selected_item_index].value;

  std::string forked_callback_id;
  auto opened = ava::tui::evaluate_fork_user_turn_selection(selected_entry_id, "session_old", "/tmp/old.jsonl",
                                                            [&](std::string_view entry_id) -> ava::core::Result<ava::tui::TuiRuntimeStateSnapshot> {
                                                              forked_callback_id = std::string(entry_id);
                                                              ava::tui::TuiRuntimeStateSnapshot state;
                                                              state.session_id = "session_forked";
                                                              state.session_path = "/tmp/forked.jsonl";
                                                              state.status = "forked session session_forked from session_old at entry_user_a";
                                                              return state;
                                                            });
  expect(forked_callback_id == "entry_user_a" && opened.action == ava::tui::UserTurnForkSelectionAction::ApplyOpenedSession && opened.clear_transcript &&
             opened.opened_snapshot && opened.opened_snapshot->session_id == "session_forked",
         "fork selection callback receives the stable entry id and apply-opened clears prior transcript");

  auto prior_transcript = std::vector<ava::tui::TranscriptItem>{ava::tui::TranscriptItem{.label = "you", .text = "later turn must clear"},
                                                                ava::tui::TranscriptItem{.label = "ava", .text = "assistant later"}};
  auto presentation = ava::tui::ComposerSnapshot{.mode = "build",
                                                 .provider = "openai",
                                                 .model = "gpt-5.5",
                                                 .session_id = "session_old",
                                                 .input = "",
                                                 .status = "forking session…",
                                                 .transcript = prior_transcript,
                                                 .width = 80,
                                                 .height = 24};
  expect(!presentation.transcript.empty(), "precondition: prior transcript is present before opened apply");
  if (opened.action == ava::tui::UserTurnForkSelectionAction::ApplyOpenedSession && opened.opened_snapshot && opened.clear_transcript)
  {
    // Mirror the production opened-session transition boundary used by runtime.cpp.
    auto status = opened.opened_snapshot->status;
    presentation.transcript.clear();
    ++presentation.transcript_generation;
    presentation.session_id = opened.opened_snapshot->session_id;
    presentation.status = opened.opened_snapshot->status;
    if (!status.empty())
      presentation.transcript.push_back(ava::tui::TranscriptItem{.label = "ava", .text = std::move(status)});
  }
  expect(presentation.session_id == "session_forked" && presentation.transcript.size() == 1 &&
             presentation.transcript.front().text.find("forked session") != std::string::npos &&
             presentation.transcript.front().text.find("later turn must clear") == std::string::npos,
         "opened fork snapshot clears the old transcript and announces the new session");

  std::size_t no_transition_calls = 0;
  auto unchanged = ava::tui::evaluate_fork_user_turn_selection(selected_entry_id, "session_old", "/tmp/old.jsonl",
                                                               [&](std::string_view) -> ava::core::Result<ava::tui::TuiRuntimeStateSnapshot> {
                                                                 ++no_transition_calls;
                                                                 ava::tui::TuiRuntimeStateSnapshot state;
                                                                 state.session_id = "session_old";
                                                                 state.session_path = "/tmp/old.jsonl";
                                                                 state.status = "Cannot branch a sessionless session.";
                                                                 return state;
                                                               });
  expect(no_transition_calls == 1 && unchanged.action == ava::tui::UserTurnForkSelectionAction::NoSessionTransition && !unchanged.clear_transcript &&
             unchanged.beep && unchanged.status.find("Cannot branch a sessionless session") != std::string::npos,
         "unchanged session identity after fork callback does not clear transcript");

  auto kept = presentation;
  kept.transcript = prior_transcript;
  kept.session_id = "session_old";
  if (unchanged.action != ava::tui::UserTurnForkSelectionAction::ApplyOpenedSession)
  {
    kept.status = unchanged.status;
  }
  expect(kept.session_id == "session_old" && kept.transcript.size() == 2 && kept.transcript.front().text == "later turn must clear",
         "no-transition fork decision retains prior transcript rows");

  std::size_t failure_calls = 0;
  auto failed = ava::tui::evaluate_fork_user_turn_selection(
      selected_entry_id, "session_old", "/tmp/old.jsonl", [&](std::string_view) -> ava::core::Result<ava::tui::TuiRuntimeStateSnapshot> {
        ++failure_calls;
        return std::unexpected(ava::core::Error(ava::core::ErrorCategory::NotFound, "branch source entry not found"));
      });
  expect(failure_calls == 1 && failed.action == ava::tui::UserTurnForkSelectionAction::AuthorityFailure && !failed.clear_transcript && failed.beep &&
             failed.status.find("branch source entry not found") != std::string::npos,
         "fork callback failure reports authority error without transcript mutation");

  auto missing = ava::tui::evaluate_fork_user_turn_selection(
      {}, "session_old", "/tmp/old.jsonl", [&](std::string_view) -> ava::core::Result<ava::tui::TuiRuntimeStateSnapshot> {
        expect(false, "empty fork selection must not invoke the authority callback");
        return std::unexpected(ava::core::Error(ava::core::ErrorCategory::InvalidArgument, "unreachable"));
      });
  expect(missing.action == ava::tui::UserTurnForkSelectionAction::MissingSelection && !missing.clear_transcript,
         "empty ForkUserTurn selection fails closed without authority work");

  std::string copy_callback_id;
  std::string copied_payload;
  auto copy_ok = ava::tui::evaluate_copy_user_turn_selection(
      selected_entry_id,
      [&](std::string_view entry_id) -> ava::core::Result<std::string> {
        copy_callback_id = std::string(entry_id);
        return std::string("alpha first line body");
      },
      [&](std::string_view text) {
        copied_payload = std::string(text);
        return true;
      });
  expect(copy_callback_id == "entry_user_a" && copy_ok.action == ava::tui::UserTurnCopySelectionAction::Copied && copy_ok.transcript_label == "status" &&
             copied_payload == "alpha first line body" && copy_ok.status.find("copied user turn") != std::string::npos,
         "copy selection re-reads the selected stable id at action time and reports truthful success");

  auto copy_read_fail = ava::tui::evaluate_copy_user_turn_selection(
      selected_entry_id,
      [&](std::string_view) -> ava::core::Result<std::string> {
        return std::unexpected(ava::core::Error(ava::core::ErrorCategory::NotFound, "session user turn not found"));
      },
      [&](std::string_view) {
        expect(false, "clipboard must not run after read failure");
        return true;
      });
  expect(copy_read_fail.action == ava::tui::UserTurnCopySelectionAction::ReadFailure && copy_read_fail.transcript_label == "error" && copy_read_fail.beep,
         "copy selection surfaces read failure without claiming clipboard success");

  auto copy_osc_fail = ava::tui::evaluate_copy_user_turn_selection(
      selected_entry_id, [&](std::string_view) -> ava::core::Result<std::string> { return std::string("payload"); }, [&](std::string_view) { return false; });
  expect(copy_osc_fail.action == ava::tui::UserTurnCopySelectionAction::ClipboardFailure && copy_osc_fail.status == "clipboard copy failed" &&
             copy_osc_fail.transcript_label == "error",
         "copy selection reports truthful OSC/bound clipboard failure");

  auto soft = ava::tui::attach_soft_status_warning("forked session session_x", "session tree refresh deferred: ", "tree locked\nextra");
  expect(soft == "forked session session_x · session tree refresh deferred: tree locked",
         "soft post-fork catalog warnings attach as single-line status without replacing the opened snapshot");
}
