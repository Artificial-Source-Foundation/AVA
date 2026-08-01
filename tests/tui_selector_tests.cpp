#include "sys.h"
#include "tests/support/test_harness.h"
#include "tests/support/tui_test_support.h"
#include "ava/app/command_palette.h"
#include "ava/app/reasoning_controls.h"
#include "ava/app/subagent_workspace.h"
#include "ava/tui/composer.h"
#include "ava/tui/keybindings.h"
#include "ava/tui/runtime.h"
#include "ava/tui/runtime_commands_internal.h"
#include "ava/tui/runtime_subagent_workspace_internal.h"
#include "ava/tui/runtime_user_turn_selection_internal.h"
#include "ava/tui/theme.h"
#include "ava/config/model_config.h"
#include "ava/session/session_store.h"
#include "ava/session/session_tree.h"
#include "ava/core/error.h"

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

void run_tui_selector_tests()
{
  expect(ava::tui::runtime_commands::session_switching_command("/clear") && ava::tui::runtime_commands::session_switching_command("  /clear Fresh session  ") &&
             !ava::tui::runtime_commands::session_switching_command("/clearance") &&
             !ava::tui::runtime_commands::session_switching_command("/clearance Fresh session"),
         "session-switch attachment safety recognizes exact /clear submissions and rejects ambiguous prefixes");

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
  auto reasoning_model = make_model("openai", "reasoning-policy", "Reasoning Policy", "gpt-5", true, {"off", "low", "medium", "high", "disabled", "blocked"});
  reasoning_model.reasoning_level_mappings.push_back(
      ava::config::ModelReasoningLevelMapping{.level = "blocked", .provider_level = std::nullopt, .supported = false});
  auto const default_reasoning_picker = ava::app::reasoning_selector_view(reasoning_model, std::nullopt, "Enter select · type to filter · Esc keep default");
  auto const explicit_reasoning_picker = ava::app::reasoning_selector_view(
      reasoning_model,
      ava::app::runtime::ReasoningSelection{.level = "high", .provider_level = std::string("provider-secret"), .budget_tokens = std::nullopt, .display = {}},
      "Esc cancel");
  auto non_reasoning_model = make_model("openai", "plain", "Plain", "plain", false, {"low", "disabled"});
  auto const unavailable_reasoning_picker = ava::app::reasoning_selector_view(non_reasoning_model, std::nullopt);
  expect(default_reasoning_picker && default_reasoning_picker->title == "Select thinking mode" && default_reasoning_picker->items.size() == 4 &&
             default_reasoning_picker->items[0].value == "default" && default_reasoning_picker->items[0].label == "Default" &&
             default_reasoning_picker->items[0].current && default_reasoning_picker->selected_item_index == 0 &&
             default_reasoning_picker->footer_hint.find("Esc keep default") != std::string::npos && explicit_reasoning_picker &&
             explicit_reasoning_picker->selected_item_index == 3 && explicit_reasoning_picker->items[3].value == "high" &&
             explicit_reasoning_picker->items[3].label == "High" && explicit_reasoning_picker->items[3].current &&
             std::ranges::none_of(explicit_reasoning_picker->items,
                                  [](auto const& item) { return item.value == "off" || item.value == "disabled" || item.value == "blocked"; }) &&
             !unavailable_reasoning_picker,
         "thinking-mode selector uses policy-resolved configurable levels with Default and explicit current semantics");

  auto const enabled_labels_are_unique = [](ava::tui::SelectListView const& view) {
    for (std::size_t left = 0; left < view.items.size(); ++left)
    {
      if (!view.items[left].enabled)
        continue;
      for (std::size_t right = left + 1; right < view.items.size(); ++right)
      {
        if (view.items[right].enabled && view.items[left].label == view.items[right].label)
          return false;
      }
    }
    return true;
  };
  auto const builtin_models = ava::config::builtin_model_registry();
  auto const gpt56_model = ava::config::find_model(builtin_models, "openai", "gpt-5.6-sol");
  auto const gpt56_reasoning_picker = gpt56_model ? ava::app::reasoning_selector_view(*gpt56_model, std::nullopt) : std::nullopt;
  std::vector<std::pair<std::string, std::string>> const expected_gpt56_rows = {
      {"default", "Default"}, {"minimal", "Minimal"}, {"low", "Low"}, {"medium", "Medium"}, {"high", "High"}, {"xhigh", "Extra high"}, {"max", "Max"}};
  auto gpt56_rows_match = gpt56_reasoning_picker && gpt56_reasoning_picker->items.size() == expected_gpt56_rows.size();
  if (gpt56_rows_match)
  {
    for (std::size_t index = 0; index < expected_gpt56_rows.size(); ++index)
    {
      gpt56_rows_match = gpt56_reasoning_picker->items[index].value == expected_gpt56_rows[index].first &&
                         gpt56_reasoning_picker->items[index].label == expected_gpt56_rows[index].second;
      if (!gpt56_rows_match)
        break;
    }
  }
  expect(gpt56_model && gpt56_rows_match && enabled_labels_are_unique(*gpt56_reasoning_picker),
         "GPT-5.6 thinking-mode selector keeps canonical authority and distinct Default through Max human labels");

  auto collision_model = make_model("custom", "colliding-levels", "Colliding Levels", "custom", true, {"foo-bar", "foo_bar"});
  auto const collision_picker = ava::app::reasoning_selector_view(collision_model, std::nullopt);
  expect(collision_picker && collision_picker->items.size() == 3 && collision_picker->items[1].value == "foo-bar" &&
             collision_picker->items[1].label == "Foo Bar (foo-bar)" && collision_picker->items[2].value == "foo_bar" &&
             collision_picker->items[2].label == "Foo Bar (foo_bar)" && enabled_labels_are_unique(*collision_picker),
         "thinking-mode selector deterministically disambiguates colliding custom human labels without changing canonical values");

  if (explicit_reasoning_picker)
  {
    auto tiny_snapshot = ava::tui::ComposerSnapshot{.mode = "build",
                                                    .provider = "openai",
                                                    .model = "Reasoning Policy",
                                                    .session_id = "session_reasoning_selector",
                                                    .input = {},
                                                    .status = "selecting thinking mode",
                                                    .transcript = {},
                                                    .select_list = *explicit_reasoning_picker,
                                                    .width = 44,
                                                    .height = 8};
    auto const frame = ava::tui::render_composer(tiny_snapshot);
    auto const selected_line = std::ranges::find_if(frame, [](std::string const& line) {
      auto const visible = strip_sgr(line);
      return visible.find("›") != std::string::npos && visible.find("High") != std::string::npos;
    });
    auto const visible = tui_test_support::join_visible_lines(frame);
    expect(selected_line != frame.end() && visible.find("provider-secret") == std::string::npos && visible.find("blocked") == std::string::npos,
           "thinking-mode selector stays human-readable and keeps the current row visible at tiny terminal size");
    if (selected_line != frame.end())
    {
      auto const hit = ava::tui::select_list_selection_for_screen_position(tiny_snapshot, static_cast<std::size_t>(selected_line - frame.begin()) + 1, 22);
      expect(hit && *hit == 3, "thinking-mode selector tiny-terminal mouse hit-testing shares generic rendered rows");
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
  auto const mode_toggle_item = std::ranges::find_if(hotkeys_view.items, [](ava::tui::SelectListItemView const& item) { return item.value == "mode_toggle"; });
  expect(hotkeys_view.title == "Keybindings" && hotkeys_view.subtitle.find("$XDG_CONFIG_HOME/ava/keybinds.json") != std::string::npos &&
             hotkeys_view.subtitle.find("Enter drafts /keybindings set") != std::string::npos &&
             hotkeys_view.subtitle.find("/reload keybindings") != std::string::npos && hotkeys_view.footer_hint.find("Enter draft edit") != std::string::npos &&
             mode_toggle_item != hotkeys_view.items.end() && mode_toggle_item->label == "Toggle build/plan mode" && mode_toggle_item->value == "mode_toggle" &&
             mode_toggle_item->description == "mode_toggle" && mode_toggle_item->detail.empty() && mode_toggle_item->badge.find("Tab") != std::string::npos &&
             mode_toggle_item->badge.find("shared") != std::string::npos &&
             std::ranges::any_of(hotkeys_frame, [](std::string const& line) { return strip_sgr(line).find("Keybindings") != std::string::npos; }) &&
             std::ranges::any_of(hotkeys_frame, [](std::string const& line) { return strip_sgr(line).find("Toggle build/plan mode") != std::string::npos; }) &&
             std::ranges::any_of(hotkeys_frame, [](std::string const& line) { return strip_sgr(line).find("mode_toggle") != std::string::npos; }) &&
             std::ranges::none_of(hotkeys_frame, [](std::string const& line) { return line.find("\x1b[7m") != std::string::npos; }),
         "keybindings view exposes human labels, bound keys before machine ids, edit drafting, config/reload guidance, and context-shared keys");

  auto mode_toggle_render_view = ava::tui::hotkeys_select_list_view(ava::tui::default_key_bindings());
  mode_toggle_render_view.query = "mode_toggle";
  auto const mode_toggle_matches = ava::tui::filter_select_list_items(mode_toggle_render_view);
  expect(!mode_toggle_matches.empty() && mode_toggle_render_view.items[mode_toggle_matches.front()].value == "mode_toggle",
         "keybindings selector can focus mode_toggle by machine id for width-sensitive render checks");
  if (!mode_toggle_matches.empty())
    mode_toggle_render_view.selected_item_index = mode_toggle_matches.front();
  auto const render_mode_toggle_width = [&](std::size_t width) {
    return ava::tui::render_composer(ava::tui::ComposerSnapshot{.mode = "build",
                                                                .provider = "openai",
                                                                .model = "gpt-5.5",
                                                                .session_id = "session_test",
                                                                .input = "composer behind hotkeys",
                                                                .status = "hotkeys opened",
                                                                .transcript = {},
                                                                .select_list = mode_toggle_render_view,
                                                                .width = width,
                                                                .height = 18});
  };
  auto const mode_toggle_frame_80 = render_mode_toggle_width(80);
  auto const mode_toggle_frame_92 = render_mode_toggle_width(92);
  auto const mode_toggle_row_visible = [](std::vector<std::string> const& frame) {
    return std::ranges::any_of(frame, [](std::string const& line) {
      auto const visible = strip_sgr(line);
      return visible.find("Toggle build/plan mode") != std::string::npos && visible.find("Tab") != std::string::npos;
    });
  };
  expect(mode_toggle_row_visible(mode_toggle_frame_80) && mode_toggle_row_visible(mode_toggle_frame_92),
         "keybindings selector keeps mode-toggle human label and Tab keys visible at widths 80 and 92");

  auto cursor_left_filter = ava::tui::hotkeys_select_list_view(ava::tui::default_key_bindings());
  cursor_left_filter.query = "cursor_left";
  auto const cursor_left_matches = ava::tui::filter_select_list_items(cursor_left_filter);
  auto live_tail_filter = ava::tui::hotkeys_select_list_view(ava::tui::default_key_bindings());
  live_tail_filter.query = "live tail";
  auto const live_tail_matches = ava::tui::filter_select_list_items(live_tail_filter);
  auto const cursor_left_item =
      std::ranges::find_if(cursor_left_filter.items, [](ava::tui::SelectListItemView const& item) { return item.value == "cursor_left"; });
  expect(!cursor_left_matches.empty() &&
             std::ranges::any_of(cursor_left_matches,
                                 [&](std::size_t index) {
                                   return cursor_left_filter.items[index].value == "cursor_left" && cursor_left_filter.items[index].label == "Move cursor left";
                                 }) &&
             !live_tail_matches.empty() &&
             std::ranges::any_of(live_tail_matches,
                                 [&](std::size_t index) {
                                   return live_tail_filter.items[index].value == "jump_to_bottom" && live_tail_filter.items[index].label == "Jump to live tail";
                                 }) &&
             mode_toggle_item != hotkeys_view.items.end() && mode_toggle_item->value == "mode_toggle" &&
             ("/keybindings set " + mode_toggle_item->value + " ") == "/keybindings set mode_toggle " && cursor_left_item != cursor_left_filter.items.end() &&
             ("/keybindings set " + cursor_left_item->value + " ") == "/keybindings set cursor_left ",
         "keybindings selector filters on snake_case ids and human labels while Enter drafts canonical machine action ids");

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

  auto make_subagent_snapshot = [](std::string id, std::string owner, std::string title, std::string type, ava::agent::SubagentExecutionState execution,
                                   std::size_t tools, ava::agent::SubagentJobMode mode = ava::agent::SubagentJobMode::Background, bool cancel_requested = false,
                                   bool was_promoted = false) {
    ava::agent::SubagentCoordinatorJobSnapshot snapshot;
    snapshot.job.identity.job_id = std::move(id);
    snapshot.job.identity.parent_session_id = std::move(owner);
    snapshot.job.identity.child_session_id = "session_child_hidden";
    snapshot.job.identity.task_id = "task_hidden";
    snapshot.job.mode = mode;
    snapshot.job.execution = execution;
    snapshot.job.cancel_requested = cancel_requested;
    snapshot.job.was_promoted = was_promoted;
    snapshot.job.display_title = std::move(title);
    snapshot.job.display_subagent_type = std::move(type);
    snapshot.job.tool_calls = tools;
    return snapshot;
  };
  auto running_cancel = make_subagent_snapshot("job_active", "owner-a", "Active", "general", ava::agent::SubagentExecutionState::Running, 1,
                                               ava::agent::SubagentJobMode::Foreground, true);
  auto terminal_cancel = make_subagent_snapshot("job_done", "owner-a", "Done", "general", ava::agent::SubagentExecutionState::Completed, 0);
  auto unexpected_cancel = make_subagent_snapshot("job_odd", "owner-a", "Odd", "general", ava::agent::SubagentExecutionState::Running, 0,
                                                  ava::agent::SubagentJobMode::Foreground, false);
  expect(ava::app::map_subagent_cancel_outcome(running_cancel) == ava::tui::SubagentWorkspaceCancelOutcome::CancellationRequested &&
             ava::app::map_subagent_cancel_outcome(running_cancel) == ava::tui::SubagentWorkspaceCancelOutcome::CancellationRequested &&
             ava::app::map_subagent_cancel_outcome(terminal_cancel) == ava::tui::SubagentWorkspaceCancelOutcome::AlreadyFinished &&
             ava::app::map_subagent_cancel_outcome(unexpected_cancel) == ava::tui::SubagentWorkspaceCancelOutcome::CancelUnavailable &&
             ava::app::map_subagent_cancel_outcome(std::unexpected(ava::core::Error(ava::core::ErrorCategory::NotFound, "secret/job_path\\nleak"))) ==
                 ava::tui::SubagentWorkspaceCancelOutcome::CancelUnavailable,
         "cancel mapper covers active/repeated, terminal, unexpected, and error states without backend text");
  auto promoted_bg = make_subagent_snapshot("job_promoted", "owner-a", "Promoted", "general", ava::agent::SubagentExecutionState::Running, 1,
                                            ava::agent::SubagentJobMode::Background, false, true);
  auto already_bg = make_subagent_snapshot("job_bg", "owner-a", "Background", "general", ava::agent::SubagentExecutionState::Running, 1,
                                           ava::agent::SubagentJobMode::Background);
  auto terminal_promote =
      make_subagent_snapshot("job_term", "owner-a", "Term", "general", ava::agent::SubagentExecutionState::Failed, 0, ava::agent::SubagentJobMode::Foreground);
  auto foreground_running = make_subagent_snapshot("job_fg", "owner-a", "Foreground", "general", ava::agent::SubagentExecutionState::Running, 1,
                                                   ava::agent::SubagentJobMode::Foreground);
  auto promote_error = ava::core::Result<ava::agent::SubagentCoordinatorJobSnapshot>(
      std::unexpected(ava::core::Error(ava::core::ErrorCategory::PermissionDenied, "secret/path/job-old\nunsafe")));
  expect(ava::app::map_subagent_promote_outcome(promoted_bg) == ava::tui::SubagentWorkspacePromoteOutcome::CurrentlyBackground &&
             ava::app::map_subagent_promote_outcome(already_bg) == ava::tui::SubagentWorkspacePromoteOutcome::CurrentlyBackground &&
             ava::app::map_subagent_promote_outcome(terminal_promote) == ava::tui::SubagentWorkspacePromoteOutcome::AlreadyFinished &&
             ava::app::map_subagent_promote_outcome(promote_error, already_bg) == ava::tui::SubagentWorkspacePromoteOutcome::CurrentlyBackground &&
             ava::app::map_subagent_promote_outcome(promote_error, terminal_promote) == ava::tui::SubagentWorkspacePromoteOutcome::AlreadyFinished &&
             ava::app::map_subagent_promote_outcome(promote_error, foreground_running) == ava::tui::SubagentWorkspacePromoteOutcome::PromotionUnavailable &&
             ava::app::map_subagent_promote_outcome(promote_error) == ava::tui::SubagentWorkspacePromoteOutcome::PromotionUnavailable &&
             ava::app::map_subagent_promote_outcome(foreground_running) == ava::tui::SubagentWorkspacePromoteOutcome::PromotionUnavailable,
         "promote mapper covers success/already/background, terminal, race-checked background/terminal failures, and plain errors");
  expect(ava::tui::runtime_commands::exact_command("/jobs", "/jobs") && ava::tui::runtime_commands::exact_command(" /jobs ", "/jobs") &&
             ava::tui::runtime_commands::exact_command("\t/jobs\n", "/jobs") && !ava::tui::runtime_commands::exact_command("/jobs promote x", "/jobs") &&
             !ava::tui::runtime_commands::exact_command("/jobs-extra", "/jobs") && !ava::tui::runtime_commands::exact_command("/jobsx", "/jobs"),
         "exact /jobs classification trims ASCII surrounding whitespace and rejects args or prefixed lookalikes");
  auto selector_snapshots = std::vector<ava::agent::SubagentCoordinatorJobSnapshot>{
      make_subagent_snapshot("job_0123456789abcdef_old", "owner-a", {}, "explore", ava::agent::SubagentExecutionState::Completed, 0),
      make_subagent_snapshot("job_0123456789abcdef_new", "owner-a", "Audit parser", "general", ava::agent::SubagentExecutionState::Running, 3)};
  auto const jobs_selector = ava::app::subagent_selector_view(selector_snapshots);
  expect(jobs_selector.title == "Subagents" && jobs_selector.freeze_underlying_transcript_layout && jobs_selector.items.size() == 2 &&
             jobs_selector.items[0].label == "Audit parser" && jobs_selector.items[0].badge == "Running · Background" &&
             jobs_selector.items[0].description.find("type general") != std::string::npos &&
             jobs_selector.items[0].description.find("tools 3") != std::string::npos && jobs_selector.items[1].label == "Explore subagent" &&
             jobs_selector.items[0].description != jobs_selector.items[1].description,
         "subagent selector is latest-first with human fallback labels, status badges, metadata, and duplicate-safe refs");
  auto selector_screen_snapshot = ava::tui::ComposerSnapshot{};
  selector_screen_snapshot.select_list = jobs_selector;
  selector_screen_snapshot.width = 88;
  selector_screen_snapshot.height = 20;
  auto const selector_screen = tui_test_support::join_visible_lines(ava::tui::render_composer(selector_screen_snapshot));
  expect(selector_screen.find("job_0123456789abcdef_new") == std::string::npos && selector_screen.find("session_child_hidden") == std::string::npos &&
             selector_screen.find("task_hidden") == std::string::npos,
         "subagent selector renders no full job, task, or child-session identities");
  auto unsafe_jobs_selector = ava::app::subagent_selector_view(
      {make_subagent_snapshot("x", "owner-a", "<task> /tmp/session_secret job_secret", "/tmp/private-type", ava::agent::SubagentExecutionState::Running, 0)});
  auto unsafe_selector_snapshot = ava::tui::ComposerSnapshot{};
  unsafe_selector_snapshot.select_list = unsafe_jobs_selector;
  unsafe_selector_snapshot.width = 80;
  unsafe_selector_snapshot.height = 16;
  auto const unsafe_selector_screen = tui_test_support::join_visible_lines(ava::tui::render_composer(unsafe_selector_snapshot));
  expect(unsafe_selector_screen.find("<task>") == std::string::npos && unsafe_selector_screen.find("/tmp/") == std::string::npos &&
             unsafe_selector_screen.find("session_secret") == std::string::npos && unsafe_selector_screen.find("job_secret") == std::string::npos &&
             unsafe_jobs_selector.items.front().label == "Subagent" && unsafe_jobs_selector.items.front().description.find("ref @") != std::string::npos,
         "unsafe title/type metadata falls back safely while even degenerate full ids remain hidden behind short refs");
  auto const empty_jobs_selector = ava::app::subagent_selector_view({});
  expect(empty_jobs_selector.items.size() == 1 && !empty_jobs_selector.items.front().enabled && empty_jobs_selector.items.front().label == "No subagents yet",
         "empty subagent selector stays open with a friendly disabled row");

  ava::agent::SubagentCoordinatorOptions owner_options;
  owner_options.registry_options.wait_for_terminal_before_start_returns = true;
  owner_options.id_generator = [](std::string_view prefix) { return std::string(prefix) + "_owner_bound_0123456789abcdef"; };
  auto owner_coordinator = ava::agent::SubagentCoordinator::create(std::move(owner_options));
  expect(owner_coordinator.has_value(), "owner-bound selector coordinator fixture creates");
  if (owner_coordinator)
  {
    ava::agent::BackgroundJobStartOptions start_options;
    start_options.title = "Owner-visible title";
    start_options.description = "RAW SECRET PROMPT <task>hidden</task>";
    start_options.subagent_type = "general";
    start_options.child_session_id = "session_owner_secret";
    start_options.child_session_path = "/tmp/secret-child.jsonl";
    auto started = (*owner_coordinator)->start_background("owner-a", std::move(start_options), [](ava::agent::BackgroundJobContext const&) {
      ava::agent::BackgroundJobCompletion completion;
      completion.state = ava::agent::BackgroundJobState::Completed;
      completion.final_text = "finished";
      return completion;
    });
    expect(started.has_value(), "owner-bound selector fixture publishes one job");
    auto wrong_owner = ava::app::subagent_selector_view(*owner_coordinator, "owner-b");
    auto right_owner = ava::app::subagent_selector_view(*owner_coordinator, "owner-a");
    expect(wrong_owner && wrong_owner->items.size() == 1 && !wrong_owner->items.front().enabled && right_owner && right_owner->items.size() == 1 &&
               right_owner->items.front().enabled,
           "subagent selector callback is strictly owner-bound");
    if (right_owner)
    {
      auto owner_snapshot = ava::tui::ComposerSnapshot{};
      owner_snapshot.select_list = *right_owner;
      owner_snapshot.width = 90;
      owner_snapshot.height = 18;
      auto const owner_screen = tui_test_support::join_visible_lines(ava::tui::render_composer(owner_snapshot));
      expect(owner_screen.find("RAW SECRET PROMPT") == std::string::npos && owner_screen.find("<task>") == std::string::npos &&
                 owner_screen.find("/tmp/secret-child.jsonl") == std::string::npos && owner_screen.find("session_owner_secret") == std::string::npos &&
                 owner_screen.find(started ? started->job.identity.job_id : std::string{}) == std::string::npos,
             "owner-bound selector never renders raw prompts, XML, paths, session ids, or full job ids");
    }
  }

  ava::tui::SubagentWorkspaceView live_view;
  live_view.title = "Audit parser";
  live_view.status = "Running · Background";
  live_view.messages = {{.role = ava::agent::SubagentLiveMessageRole::User, .text = "Inspect the parser."},
                        {.role = ava::agent::SubagentLiveMessageRole::Assistant, .text = "Committed result line."}};
  auto const live_lines = ava::tui::render_subagent_workspace(live_view, 64, 10);
  auto const live_screen = tui_test_support::join_visible_lines(live_lines);
  expect(live_lines.size() == 10 && live_screen.find("Audit parser") != std::string::npos && live_screen.find("User") != std::string::npos &&
             live_screen.find("Assistant") != std::string::npos && live_screen.find("Committed result line.") != std::string::npos &&
             live_screen.find("C cancel") != std::string::npos && live_screen.find("P promote") != std::string::npos &&
             live_screen.find("Type a message") == std::string::npos,
         "read-only subagent workspace renders only its header, committed child messages, status, and controls");
  auto terminal_controls_view = live_view;
  terminal_controls_view.terminal = true;
  auto const terminal_controls_screen = tui_test_support::join_visible_lines(ava::tui::render_subagent_workspace(terminal_controls_view, 64, 10));
  expect(terminal_controls_screen.find("C cancel") == std::string::npos && terminal_controls_screen.find("P promote") == std::string::npos &&
             terminal_controls_screen.find("Esc jobs") != std::string::npos,
         "terminal workspace frames omit C/P controls from the footer");
  auto waiting_view = live_view;
  waiting_view.messages.clear();
  auto waiting_screen = tui_test_support::join_visible_lines(ava::tui::render_subagent_workspace(waiting_view, 20, 8));
  auto refresh_view = live_view;
  refresh_view.refresh_unavailable = true;
  auto refresh_screen = tui_test_support::join_visible_lines(ava::tui::render_subagent_workspace(refresh_view, 38, 8));
  auto unavailable_view = live_view;
  unavailable_view.unavailable = true;
  auto unavailable_screen = tui_test_support::join_visible_lines(ava::tui::render_subagent_workspace(unavailable_view, 38, 8));
  auto evicted_view = unavailable_view;
  evicted_view.evicted = true;
  auto evicted_screen = tui_test_support::join_visible_lines(ava::tui::render_subagent_workspace(evicted_view, 48, 8));
  expect(waiting_screen.find("Waiting for comm") != std::string::npos && waiting_screen.find("itted messages") != std::string::npos,
         "empty running subagent workspace truthfully renders a waiting state at tiny sizes");
  expect(refresh_screen.find("Live refresh") != std::string::npos && refresh_screen.find("last committ") != std::string::npos,
         "subagent workspace truthfully renders refresh-unavailable state at tiny sizes");
  expect(unavailable_screen.find("Subagent unavail") != std::string::npos && unavailable_screen.find("last committed") != std::string::npos,
         "subagent workspace truthfully renders unavailable state at tiny sizes");
  expect(evicted_screen.find("no longer retained") != std::string::npos, "subagent retention eviction has a distinct truthful workspace state");

  auto make_selector_item = [](std::string value, std::string label, std::string badge) {
    ava::tui::SelectListItemView item;
    item.value = std::move(value);
    item.label = std::move(label);
    item.badge = std::move(badge);
    return item;
  };
  auto controller_selector = [&]() {
    ava::tui::SelectListView view;
    view.title = "Subagents";
    view.placeholder = "Search subagents";
    view.empty_text = "No matching subagents";
    view.footer_hint = "Enter open · Esc close";
    view.freeze_underlying_transcript_layout = true;
    view.items = {make_selector_item("job-new", "New job", "Running · Background"), make_selector_item("job-old", "Old job", "Completed · Background")};
    return view;
  };
  std::size_t list_calls = 0;
  std::size_t inspect_calls = 0;
  bool list_refresh_failure = false;
  std::vector<std::optional<std::uint64_t>> inspected_generations;
  std::string canceled_id;
  std::string promoted_id;
  ava::tui::TuiRuntimeOptions controller_options;
  controller_options.list_subagents = [&]() -> ava::core::Result<ava::tui::SelectListView> {
    ++list_calls;
    if (list_refresh_failure)
      return std::unexpected(ava::core::Error(ava::core::ErrorCategory::Unknown, "private backend detail"));
    return controller_selector();
  };
  controller_options.inspect_subagent =
      [&](std::string_view id, std::optional<std::uint64_t> known) -> ava::core::Result<std::shared_ptr<ava::agent::SubagentInspectorFrame const>> {
    ++inspect_calls;
    inspected_generations.push_back(known);
    auto frame = std::make_shared<ava::agent::SubagentInspectorFrame>();
    if (id == "job-old")
    {
      frame->generation = 7;
      frame->terminal = true;
      frame->messages = {{.role = ava::agent::SubagentLiveMessageRole::Assistant, .text = "Old terminal result"}};
    }
    else if (!known)
    {
      frame->generation = 1;
      frame->messages = {{.role = ava::agent::SubagentLiveMessageRole::User, .text = "Committed first"}};
    }
    else if (*known == 1)
    {
      frame->generation = 2;
      std::string long_result;
      for (int line = 0; line < 30; ++line) long_result += "Committed live line " + std::to_string(line) + "\n";
      frame->messages = {{.role = ava::agent::SubagentLiveMessageRole::User, .text = "Committed first"},
                         {.role = ava::agent::SubagentLiveMessageRole::Assistant, .text = std::move(long_result)}};
    }
    else
    {
      frame->generation = *known;
      frame->not_modified = true;
    }
    return std::shared_ptr<ava::agent::SubagentInspectorFrame const>(std::move(frame));
  };
  auto cancel_outcome = ava::tui::SubagentWorkspaceCancelOutcome::CancellationRequested;
  auto promote_outcome = ava::tui::SubagentWorkspacePromoteOutcome::PromotionUnavailable;
  controller_options.cancel_subagent = [&](std::string_view id) {
    canceled_id = std::string(id);
    return cancel_outcome;
  };
  controller_options.promote_subagent = [&](std::string_view id) {
    promoted_id = std::string(id);
    return promote_outcome;
  };
  ava::tui::ComposerSnapshot controller_snapshot;
  controller_snapshot.input = "parent draft";
  controller_snapshot.status = "parent status";
  controller_snapshot.transcript = {{.label = "assistant", .text = "parent transcript"}};
  controller_snapshot.transcript_generation = 42;
  controller_snapshot.width = 40;
  controller_snapshot.height = 8;
  ava::tui::RuntimeSubagentWorkspaceController workspace_controller(controller_options, controller_snapshot);
  using WorkspaceClock = ava::tui::RuntimeSubagentWorkspaceController::Clock;
  auto const time_zero = WorkspaceClock::time_point{};
  expect(workspace_controller.open_selector(time_zero) && workspace_controller.selector_active() && list_calls == 1,
         "shared subagent controller opens the selector without changing the parent composer state");
  ava::tui::InputEvent type_query;
  type_query.key = ava::tui::Key::Character;
  type_query.character = 'n';
  type_query.text = "n";
  auto query_result = workspace_controller.handle_input(type_query, time_zero);
  expect(query_result.changed && controller_snapshot.select_list && controller_snapshot.select_list->query == "n",
         "subagent controller owns selector filtering");
  ava::tui::InputEvent enter_workspace;
  enter_workspace.key = ava::tui::Key::Enter;
  auto opened_workspace = workspace_controller.handle_input(enter_workspace, time_zero);
  expect(opened_workspace.changed && workspace_controller.workspace_active() && workspace_controller.active_job_id() == "job-new" && inspect_calls == 1 &&
             !inspected_generations.front() && controller_snapshot.subagent_workspace && controller_snapshot.subagent_workspace->messages.size() == 1,
         "selector Enter opens the owner-bound frozen frame using hidden job identity");
  expect(!workspace_controller.poll(time_zero + std::chrono::milliseconds(149)) && inspect_calls == 1 && list_calls == 1,
         "subagent workspace never polls faster than 150ms");
  auto const live_changed = workspace_controller.poll(time_zero + std::chrono::milliseconds(150));
  expect(live_changed && inspect_calls == 2 && list_calls == 2 && inspected_generations.back() == std::optional<std::uint64_t>{1} &&
             workspace_controller.known_generation() == std::optional<std::uint64_t>{2} && controller_snapshot.subagent_workspace &&
             controller_snapshot.subagent_workspace->messages.size() == 2,
         "subagent polling passes known generations and publishes only newly committed messages");
  auto const bottom_offset = controller_snapshot.subagent_workspace ? controller_snapshot.subagent_workspace->scroll_offset : 0;
  ava::tui::InputEvent up_three;
  up_three.key = ava::tui::Key::ArrowUp;
  auto scrolled_up = workspace_controller.handle_input(up_three);
  expect(scrolled_up.changed && controller_snapshot.subagent_workspace && bottom_offset >= controller_snapshot.subagent_workspace->scroll_offset + 3,
         "workspace Up scrolls exactly three rendered rows within bounds");
  ava::tui::InputEvent page_up;
  page_up.key = ava::tui::Key::PageUp;
  auto const before_page = controller_snapshot.subagent_workspace ? controller_snapshot.subagent_workspace->scroll_offset : 0;
  auto paged_up = workspace_controller.handle_input(page_up);
  expect(paged_up.changed && controller_snapshot.subagent_workspace && before_page >= controller_snapshot.subagent_workspace->scroll_offset + 5,
         "workspace PageUp scrolls exactly five rendered rows within bounds");
  auto const unchanged_poll = workspace_controller.poll(time_zero + std::chrono::milliseconds(300));
  expect(!unchanged_poll && inspect_calls == 3 && inspected_generations.back() == std::optional<std::uint64_t>{2},
         "not-modified inspector polls retain the frozen frame without requesting redraw");
  list_refresh_failure = true;
  expect(workspace_controller.poll(time_zero + std::chrono::milliseconds(450)) && controller_snapshot.subagent_workspace &&
             controller_snapshot.subagent_workspace->refresh_unavailable && controller_snapshot.subagent_workspace->messages.size() == 2 && inspect_calls == 3,
         "metadata refresh failures retain the frozen child frame and expose only a fixed unavailable state");
  auto const refresh_failure_screen =
      controller_snapshot.subagent_workspace
          ? tui_test_support::join_visible_lines(ava::tui::render_subagent_workspace(*controller_snapshot.subagent_workspace, 40, 8))
          : std::string{};
  expect(refresh_failure_screen.find("Live refresh unavail") != std::string::npos && refresh_failure_screen.find("private backend detail") == std::string::npos,
         "poll failure rendering is truthful and sanitized");
  list_refresh_failure = false;
  expect(workspace_controller.poll(time_zero + std::chrono::milliseconds(600)) && controller_snapshot.subagent_workspace &&
             !controller_snapshot.subagent_workspace->refresh_unavailable && inspect_calls == 4,
         "a successful metadata and known-generation refresh clears transient unavailable state");
  ava::tui::InputEvent escape_workspace;
  escape_workspace.key = ava::tui::Key::Escape;
  auto escaped_to_selector = workspace_controller.handle_input(escape_workspace);
  expect(
      escaped_to_selector.changed && workspace_controller.selector_active() && controller_snapshot.select_list && controller_snapshot.select_list->query == "n",
      "workspace Esc returns to the same selector query");
  static_cast<void>(workspace_controller.handle_input(enter_workspace));
  ava::tui::InputEvent next_job;
  next_job.key = ava::tui::Key::Tab;
  auto cycled = workspace_controller.handle_input(next_job);
  expect(cycled.changed && workspace_controller.active_job_id() == "job-old" && controller_snapshot.subagent_workspace &&
             controller_snapshot.subagent_workspace->terminal,
         "workspace Tab cycles by hidden job identity and opens the terminal frozen frame");
  ava::tui::InputEvent cancel_job;
  cancel_job.key = ava::tui::Key::Character;
  cancel_job.character = 'C';
  cancel_job.text = "C";
  cancel_outcome = ava::tui::SubagentWorkspaceCancelOutcome::CancellationRequested;
  auto canceled = workspace_controller.handle_input(cancel_job);
  expect(canceled.changed && !canceled.beep && canceled_id == "job-old" && controller_snapshot.subagent_workspace &&
             controller_snapshot.subagent_workspace->notice == "Cancellation requested",
         "active cancel publishes the fixed Cancellation requested notice without a beep");
  auto canceled_again = workspace_controller.handle_input(cancel_job);
  expect(canceled_again.changed && !canceled_again.beep && controller_snapshot.subagent_workspace &&
             controller_snapshot.subagent_workspace->notice == "Cancellation requested",
         "repeated cancel stays truthfully Cancellation requested");
  cancel_outcome = ava::tui::SubagentWorkspaceCancelOutcome::AlreadyFinished;
  auto canceled_terminal = workspace_controller.handle_input(cancel_job);
  expect(canceled_terminal.changed && !canceled_terminal.beep && controller_snapshot.subagent_workspace &&
             controller_snapshot.subagent_workspace->notice == "Already finished",
         "terminal cancel publishes the fixed Already finished notice");
  cancel_outcome = ava::tui::SubagentWorkspaceCancelOutcome::CancelUnavailable;
  auto canceled_unavailable = workspace_controller.handle_input(cancel_job);
  expect(canceled_unavailable.changed && canceled_unavailable.beep && controller_snapshot.subagent_workspace &&
             controller_snapshot.subagent_workspace->notice == "Cancel unavailable",
         "cancel errors map to Cancel unavailable with a beep");
  ava::tui::InputEvent promote_job;
  promote_job.key = ava::tui::Key::Character;
  promote_job.character = 'P';
  promote_job.text = "P";
  promote_outcome = ava::tui::SubagentWorkspacePromoteOutcome::CurrentlyBackground;
  auto promoted = workspace_controller.handle_input(promote_job);
  expect(promoted.changed && !promoted.beep && promoted_id == "job-old" && controller_snapshot.subagent_workspace &&
             controller_snapshot.subagent_workspace->notice == "Currently background",
         "successful or already-background promote publishes Currently background without a beep");
  promote_outcome = ava::tui::SubagentWorkspacePromoteOutcome::AlreadyFinished;
  auto promoted_terminal = workspace_controller.handle_input(promote_job);
  expect(promoted_terminal.changed && !promoted_terminal.beep && controller_snapshot.subagent_workspace &&
             controller_snapshot.subagent_workspace->notice == "Already finished",
         "terminal promote publishes Already finished");
  promote_outcome = ava::tui::SubagentWorkspacePromoteOutcome::PromotionUnavailable;
  auto promoted_unavailable = workspace_controller.handle_input(promote_job);
  auto const control_screen = controller_snapshot.subagent_workspace
                                  ? tui_test_support::join_visible_lines(ava::tui::render_subagent_workspace(*controller_snapshot.subagent_workspace, 40, 8))
                                  : std::string{};
  expect(promoted_unavailable.changed && promoted_unavailable.beep && controller_snapshot.subagent_workspace &&
             controller_snapshot.subagent_workspace->notice == "Promotion unavailable",
         "promote errors map to Promotion unavailable with a beep");
  expect(control_screen.find("Promotion") != std::string::npos && control_screen.find("unavaila") != std::string::npos &&
             control_screen.find("secret/path") == std::string::npos && control_screen.find("job-old") == std::string::npos &&
             control_screen.find("PermissionDenied") == std::string::npos && control_screen.find("Cancel requested") == std::string::npos,
         "workspace control failures stay sanitized, use fixed notices, and never leak backend text or ids");
  static_cast<void>(workspace_controller.handle_input(escape_workspace));
  static_cast<void>(workspace_controller.handle_input(escape_workspace));
  expect(!workspace_controller.active() && controller_snapshot.input == "parent draft" && controller_snapshot.status == "parent status" &&
             controller_snapshot.transcript_generation == 42 && controller_snapshot.transcript.size() == 1,
         "closing the shared workspace restores the exact parent draft, status, transcript, and generation");

  auto prompt_precedence_snapshot = ava::tui::ComposerSnapshot{};
  prompt_precedence_snapshot.subagent_workspace = live_view;
  ava::tui::QuestionPromptView prompt_view;
  prompt_view.header = "Permission first";
  prompt_view.question = "Continue parent prompt?";
  prompt_view.modal = true;
  ava::tui::QuestionPromptOptionView prompt_option;
  prompt_option.value = "yes";
  prompt_option.label = "Yes";
  prompt_view.options.push_back(std::move(prompt_option));
  prompt_precedence_snapshot.question_prompt = std::move(prompt_view);
  prompt_precedence_snapshot.width = 64;
  prompt_precedence_snapshot.height = 16;
  auto const prompt_precedence_screen = tui_test_support::join_visible_lines(ava::tui::render_composer(prompt_precedence_snapshot));
  expect(prompt_precedence_screen.find("Continue parent prompt?") != std::string::npos &&
             prompt_precedence_screen.find("Committed result line.") == std::string::npos,
         "question prompts render ahead of the jobs workspace");
  auto permission_precedence_snapshot = ava::tui::ComposerSnapshot{};
  permission_precedence_snapshot.subagent_workspace = live_view;
  ava::tui::PermissionPromptView permission_view;
  permission_view.tool_name = "write_file";
  permission_view.operation = "write";
  permission_view.target = "fixture.txt";
  permission_view.reason = "test precedence";
  permission_precedence_snapshot.permission_prompt = std::move(permission_view);
  permission_precedence_snapshot.width = 64;
  permission_precedence_snapshot.height = 16;
  auto const permission_precedence_screen = tui_test_support::join_visible_lines(ava::tui::render_composer(permission_precedence_snapshot));
  expect(permission_precedence_screen.find("Permission required") != std::string::npos &&
             permission_precedence_screen.find("Committed result line.") == std::string::npos,
         "permission prompts render ahead of the jobs workspace");
}
