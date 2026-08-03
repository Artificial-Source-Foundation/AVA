#include "sys.h"
#include "ava/app/clipboard_image.h"
#include "ava/app/command_format.h"
#include "ava/app/command_jobs.h"
#include "ava/app/command_palette.h"
#include "ava/app/command_sessions.h"
#include "ava/app/commands.h"
#include "ava/app/display_settings.h"
#include "ava/app/interactive_run_queue.h"
#include "ava/app/line_shell_internal.h"
#include "ava/app/onboarding.h"
#include "ava/app/reasoning_controls.h"
#include "ava/app/rpc/runtime_navigation.h"
#include "ava/app/runtime.h"
#include "ava/app/session_title_coordinator.h"
#include "ava/app/session_user_turns.h"
#include "ava/app/subagent_workspace.h"
#include "ava/tui/keybindings.h"
#include "ava/tui/runtime.h"
#include "ava/config/model_profiles.h"
#include "ava/session/attachments.h"
#include "ava/core/ids.h"
#include "ava/core/version.h"

#include <cstdint>
#include <filesystem>
#include <iostream>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace ava::app::line_shell_internal {

// THIS IS A HACK.
// I didn't want to change too much of this code because I think Igo is working on it too;
// We need to replace `state.session` with a temporarily unlocked Session reference however.
// Doing that by redefining `state` at the top of every function.
struct FakeState
{
  ShellState& real_state_;
  ava::app::runtime::session_ts::wat session_w_;
  ava::app::runtime::Session& session;
  operator ShellState&() { return real_state_; }

  // Hack access to the base class of real_state.unlocked_session, without locking it.
  FakeState(ShellState& real_state) : real_state_(real_state), session_w_(real_state.unlocked_session), session(*session_w_)
  {
    session_w_.unlock();
  }
};

namespace version = ava::core::version;

int run_tui(ShellState real_state)
{
  FakeState state{real_state};



  auto key_bindings = ava::tui::default_key_bindings();
  std::string keybind_status;
  if (auto loaded = ava::tui::load_key_bindings(state.session.paths().ava_config_dir / "keybinds.json"); loaded)
  {
    key_bindings = std::move(*loaded);
  }
  else
  {
    keybind_status = loaded.error().format();
  }
  auto display_watch_state = std::make_shared<std::optional<ava::app::TuiDisplaySettingsWatchState>>();
  auto display_watch_mutex = std::make_shared<std::mutex>();
  auto refresh_display_watch_state = [&state, display_watch_state, display_watch_mutex]() -> ava::core::VoidResult {
    auto watched = ava::app::load_tui_display_settings_watch_state(state.session.paths());
    if (!watched)
      return std::unexpected(std::move(watched.error()));
    std::lock_guard lock(*display_watch_mutex);
    *display_watch_state = std::move(*watched);
    return {};
  };
  if (auto display_settings = ava::app::apply_tui_display_settings(state.session.paths()); !display_settings)
  {
    append_status_line(keybind_status, display_settings.error().format());
  }
  else if (auto watched = refresh_display_watch_state(); !watched)
  {
    append_status_line(keybind_status, watched.error().format());
  }
  auto hotkeys = command_hotkeys_from_key_bindings(key_bindings);
  auto const initial_title_catalog_cursor =
      state.session.session_title_coordinator() ? state.session.session_title_coordinator()->catalog_changes_since(0).cursor : std::size_t{0};
  auto initial_application_catalog = ava::app::build_application_catalog_cache(state.session, hotkeys);
  ava::app::ApplicationCatalogCoordinator application_catalog(std::move(initial_application_catalog), initial_title_catalog_cursor);
  auto model_display = [](ava::config::ModelInfo const& model) {
    return model.display_name.empty() ? ava::config::model_display_label(model.model_id) : model.display_name;
  };
  auto custom_theme_options = [&state]() {
    std::vector<ava::tui::ThemeOptionItem> themes;
    for (auto const& theme : ava::app::available_tui_custom_themes(state.session.paths()))
    {
      themes.push_back(ava::tui::ThemeOptionItem{.name = theme.name, .detail = theme.path.string()});
    }
    return themes;
  };
  auto runtime_open_context = [&state]() { return state.session.replacement_open_context({}); };
  auto capture_title_catalog_changes = [&state, &application_catalog]() {
    auto const cursor = application_catalog.title_catalog_cursor();
    return state.session.session_title_coordinator() ? state.session.session_title_coordinator()->catalog_changes_since(cursor)
                                                     : ava::app::SessionTitleCatalogChanges{.cursor = cursor};
  };
  auto refresh_title_catalog = [&state, &application_catalog, &hotkeys, &capture_title_catalog_changes]() -> ava::core::Result<bool> {
    if (!state.session.session_title_coordinator())
      return false;
    return application_catalog.refresh_title_changes(state.session, capture_title_catalog_changes(), hotkeys);
  };
  auto refresh_session_tree_catalog = [&state, &application_catalog, &hotkeys, &capture_title_catalog_changes]() {
    auto const captured_changes = capture_title_catalog_changes();
    return application_catalog.refresh_session_tree_and_consume_title_changes(state.session, captured_changes, hotkeys);
  };
  auto state_snapshot = [&state, &application_catalog, &model_display, &custom_theme_options, &refresh_title_catalog](std::string status) {
    static_cast<void>(refresh_title_catalog());
    auto delivery = application_catalog.delivery_snapshot();
    return ava::tui::TuiRuntimeStateSnapshot{
        .mode = ava::agent::to_string(state.session.mode()),
        .provider = state.session.model().provider_id,
        .model = model_display(state.session.model()),
        .session_id = state.session.store.session_id(),
        .session_path = state.session.store.session_path().string(),
        .workspace = state.session.current_dir().empty() ? state.session.workspace_dir().string() : state.session.current_dir().string(),
        .git_branch = git_branch_for_sidebar(state.session.workspace_dir()),
        .context_source_count = state.session.context_sources().size(),
        .status = std::move(status),
        .slash_commands = std::move(delivery.slash_commands),
        .slash_catalog_generation = delivery.slash_catalog_generation,
        .file_references = std::move(delivery.file_references),
        .workspace_catalog_generation = delivery.workspace_catalog_generation,
        .custom_themes = custom_theme_options(),
        .project_trust = project_trust_snapshot(state.session.project_trust()),
        .todos = todos_for_session(state.session)};
  };
  auto session_selector_sort = ava::app::SessionSelectorSort::Recent;
  bool session_selector_named_only = false;
  bool session_selector_show_paths = false;
  bool session_selector_show_archived = false;
  bool session_selector_show_label_time = false;
  auto session_selector_snapshot = [&]() {
    static_cast<void>(refresh_title_catalog());
    return application_catalog.session_view(session_selector_sort,
                                            session_selector_footer_hint(session_selector_sort, session_selector_named_only, session_selector_show_paths,
                                                                         session_selector_show_archived, session_selector_show_label_time),
                                            session_selector_named_only, session_selector_show_paths, session_selector_show_archived,
                                            session_selector_show_label_time);
  };
  auto open_session_selector_target = [&state, &runtime_open_context, &state_snapshot, &application_catalog, &hotkeys](
                                          std::string target_session_id, std::string status_prefix) -> ava::core::Result<ava::tui::TuiRuntimeStateSnapshot> {
    if (target_session_id.empty())
    {
      return std::unexpected(ava::core::Error(ava::core::ErrorCategory::InvalidArgument, "session branch target is missing session id"));
    }
    if (target_session_id == state.session.store.session_id())
    {
      application_catalog.retarget_session(state.session.store.session_id());
      return state_snapshot(status_prefix + target_session_id + " (already open)");
    }
    auto unlocked_opened_result = state.session.open_requested(runtime_open_context(), target_session_id);
    if (!unlocked_opened_result)
      return std::unexpected(std::move(unlocked_opened_result.error()));
    {
      ava::app::runtime::session_ts::wat opened_w(*unlocked_opened_result);
      if (auto replaced = state.session.replace_with(std::move(*opened_w)); !replaced)
        return std::unexpected(std::move(replaced.error()));
    }
    application_catalog.retarget_session(state.session.store.session_id());
    application_catalog.refresh_values(state.session, hotkeys);
    return state_snapshot(status_prefix + target_session_id);
  };
  auto open_selector_branch = [&application_catalog, &open_session_selector_target, &session_selector_sort, &session_selector_show_archived,
                               &refresh_title_catalog](std::string_view selected_session_id,
                                                       bool parent) -> ava::core::Result<ava::tui::TuiRuntimeStateSnapshot> {
    if (auto refreshed = refresh_title_catalog(); !refreshed)
      return std::unexpected(std::move(refreshed.error()));
    if (selected_session_id.empty())
    {
      return std::unexpected(ava::core::Error(ava::core::ErrorCategory::InvalidArgument, "session branch navigation is missing session id"));
    }
    auto target = parent ? application_catalog.parent_target(selected_session_id)
                         : application_catalog.child_target(selected_session_id, session_selector_sort, session_selector_show_archived);
    if (!target)
      return std::unexpected(std::move(target.error()));
    if (!*target)
    {
      auto error =
          ava::core::Error(ava::core::ErrorCategory::NotFound, parent ? "selected session has no parent branch" : "selected session has no child branch");
      error.with_context("session_id", std::string(selected_session_id));
      return std::unexpected(std::move(error));
    }
    return open_session_selector_target(std::move(**target), parent ? std::string("opened parent branch ") : std::string("opened child branch "));
  };
  std::vector<ava::tui::TranscriptItem> initial_transcript;
  if (auto onboarding = ava::app::first_run_auth_onboarding_message(state.session))
  {
    initial_transcript.push_back(ava::tui::TranscriptItem{.label = "setup", .text = std::move(*onboarding)});
  }
  auto initial_catalog_snapshot = application_catalog.snapshot();
  auto result = ava::tui::run_interactive_composer(ava::tui::TuiRuntimeOptions{
      .mode = ava::agent::to_string(state.session.mode()),
      .provider = state.session.model().provider_id,
      .model = model_display(state.session.model()),
      .session_id = state.session.store.session_id(),
      .session_path = state.session.store.session_path().string(),
      .workspace = state.session.current_dir().empty() ? state.session.workspace_dir().string() : state.session.current_dir().string(),
      .git_branch = git_branch_for_sidebar(state.session.workspace_dir()),
      .app_version = std::string(version::kDisplayVersion),
      .context_source_count = state.session.context_sources().size(),
      .initial_status = keybind_status,
      .initial_transcript = std::move(initial_transcript),
      .slash_commands = initial_catalog_snapshot.slash_commands,
      .slash_catalog_generation = initial_catalog_snapshot.slash_catalog_generation,
      .file_references = initial_catalog_snapshot.file_references,
      .workspace_catalog_generation = initial_catalog_snapshot.workspace_catalog_generation,
      .custom_themes = custom_theme_options(),
      .project_trust = project_trust_snapshot(state.session.project_trust()),
      .initial_todos = todos_for_session(state.session),
      .key_bindings = key_bindings,
      .token_status_provider = [&state]() { return token_status_for_session(state.session); },
      .active_context_status_provider = [&state]() { return active_context_status_for_session(state.session); },
      .reasoning_status_provider = [&state]() { return ava::app::reasoning_status_for_session(state.session); },
      .create_active_run_queues =
          [&state](ava::event::EventEnvelopeSink event_sink) {
            auto const active_job_coordinator = state.session.subagent_coordinator();
            auto const active_job_owner = state.session.store.session_id();
            auto queue = std::make_shared<ava::app::InteractiveRunQueue>(active_job_owner, ava::core::make_id("request"), std::move(event_sink));
            return ava::tui::TuiActiveRunQueues{
                .active_request_id = queue->active_request_id(),
                .queue_steering = [queue](std::string message) { return queue->queue_steering(std::move(message)); },
                .queue_follow_up = [queue](std::string message) { return queue->queue_follow_up(std::move(message)); },
                .take_steering_messages = [queue]() { return queue->take_steering_messages(); },
                .skip_active_steering = [queue](std::string_view reason) { return queue->skip_active_steering(reason); },
                .take_next_follow_up = [queue]() -> std::optional<ava::tui::TuiQueuedFollowUp> {
                  auto next = queue->take_next_follow_up();
                  if (!next)
                    return std::nullopt;
                  return ava::tui::TuiQueuedFollowUp{.request_id = next->request_id, .message = next->message};
                },
                .mark_follow_up_started =
                    [queue](ava::tui::TuiQueuedFollowUp const& follow_up) {
                      return queue->mark_follow_up_started(ava::app::InteractiveQueuedMessage{
                          .request_id = follow_up.request_id, .correlation_id = follow_up.request_id, .message = follow_up.message});
                    },
                .restore_latest = [queue]() -> ava::core::Result<ava::tui::TuiRestoredQueuedMessage> {
                  auto restored = queue->restore_latest();
                  if (!restored)
                    return std::unexpected(std::move(restored.error()));
                  return ava::tui::TuiRestoredQueuedMessage{.message = restored->message, .steering = restored->steering};
                },
                .run_nonblocking_command = [active_job_coordinator, active_job_owner](std::string const& submitted) -> std::optional<std::vector<std::string>> {
                  auto arguments = ava::app::active_jobs_command_arguments(submitted);
                  if (!arguments)
                    return std::nullopt;
                  auto command = ava::app::run_jobs_command(active_job_coordinator, active_job_owner, *arguments, true);
                  if (!command)
                    return std::vector<std::string>{command.error().format()};
                  return std::move(command->output);
                },
                .finish = [queue](bool canceled) { return queue->finish(canceled); }};
          },
      .on_submit =
          [&state, &hotkeys, &refresh_display_watch_state, &refresh_session_tree_catalog, &refresh_title_catalog, &state_snapshot, &application_catalog](
              std::string const& submitted, ava::tui::TuiSubmitContext context) {
            // Persistent rules resolve before the TUI fallback resolver in
            // context, so an exact durable Deny never reaches the in-memory
            // session-grant registry.
            auto permission_resolver =
                ava::permissions::build_persistent_permission_rule_resolver(state.session.permission_rule_store(), context.permission_resolver);
            auto const session_id_before = state.session.store.session_id();
            bool workspace_catalog_reload = workspace_catalog_reload_requested(submitted);
            auto line_result =
                handle_line(state, submitted, permission_resolver, context.question_resolver, hotkeys, context.event_sink, context.cancel_requested,
                            context.take_steering_messages, std::move(context.image_attachments), context.request_id, context.on_subagent_launch);
            if (is_display_settings_command(submitted))
            {
              if (auto watched = refresh_display_watch_state(); !watched)
              {
                add_output(line_result, watched.error().format());
              }
            }
            auto const session_changed = run_queued_follow_ups_until_session_transition(
                line_result, workspace_catalog_reload, session_id_before, context, [&state]() { return state.session.store.session_id(); },
                [&](ava::tui::TuiQueuedFollowUp const& follow_up) {
                  return handle_line(state, follow_up.message, permission_resolver, context.question_resolver, hotkeys, context.event_sink,
                                     context.cancel_requested, context.take_steering_messages, {}, follow_up.request_id, context.on_subagent_launch);
                });
            bool const workspace_changed = workspace_catalog_reload || workspace_catalog_changed(line_result);
            if (workspace_changed)
              application_catalog.refresh_workspace(state.session, hotkeys);
            if (line_result.session_tree_changed)
            {
              if (auto refreshed = refresh_session_tree_catalog(); !refreshed)
                add_output(line_result, refreshed.error().format());
            }
            else if (session_changed)
            {
              application_catalog.retarget_session(state.session.store.session_id());
              application_catalog.refresh_values(state.session, hotkeys);
            }
            else if (line_result.ordinary_turn_committed)
            {
              bool current_refreshed = false;
              if (state.session.session_title_coordinator())
              {
                auto refreshed = refresh_title_catalog();
                if (!refreshed)
                  add_output(line_result, refreshed.error().format());
                else
                  current_refreshed = *refreshed;
              }
              if (!current_refreshed)
              {
                auto refreshed = application_catalog.refresh_current_session(state.session, hotkeys);
                if (!refreshed)
                  add_output(line_result, refreshed.error().format());
              }
            }
            else if (!workspace_changed)
              application_catalog.refresh_values(state.session, hotkeys);
            return ava::tui::TuiSubmitResult{.quit = line_result.quit,
                                             .output = line_result.output,
                                             .tool_timeline = tui_tool_timeline(line_result.tool_timeline),
                                             .context_source_count = state.session.context_sources().size(),
                                             .state_snapshot = state_snapshot({})};
          },
      .on_attach_image = [&state](std::string const& path) -> ava::core::Result<ava::session::ImageAttachmentRef> {
        auto source = std::filesystem::path(path);
        if (source.empty())
        {
          return std::unexpected(ava::core::Error(ava::core::ErrorCategory::InvalidArgument, "usage: /attach <image-path>"));
        }
        if (!source.is_absolute())
        {
          auto const base = state.session.current_dir().empty() ? state.session.workspace_dir() : state.session.current_dir();
          source = base / source;
        }
        return ava::session::import_image_attachment(state.session.store, source);
      },
      .on_paste_clipboard_image = [&state]() -> ava::core::Result<std::optional<ava::session::ImageAttachmentRef>> {
        return ava::app::import_clipboard_image_attachment(state.session.store);
      },
      .on_external_editor = [](std::string_view initial_text) -> ava::core::Result<std::optional<std::string>> {
        return edit_text_with_external_editor(initial_text);
      },
      .on_load_image_attachment = [&state](ava::session::ImageAttachmentRef const& attachment) -> ava::core::Result<ava::session::LoadedImageAttachment> {
        return ava::session::load_image_attachment(state.session.store, attachment);
      },
      .on_toggle_mode = [&state]() -> ava::core::Result<std::string> {
        auto result = ava::app::run_command(state.session, ava::app::CommandRequest{.command = "/mode"});
        if (!result)
          return std::unexpected(std::move(result.error()));
        return ava::agent::to_string(state.session.mode());
      },
      .on_cycle_reasoning = [&state]() -> ava::core::Result<std::string> { return ava::app::cycle_runtime_reasoning(state.session); },
      .on_cycle_model = [&state, &state_snapshot](bool forward) -> ava::core::Result<ava::tui::TuiRuntimeStateSnapshot> {
        auto model = forward ? ava::app::rpc::next_runtime_model(ava::app::runtime::session_ts::wat(state.real_state_.unlocked_session))
                             : ava::app::rpc::previous_runtime_model(ava::app::runtime::session_ts::wat(state.real_state_.unlocked_session));
        if (!model)
          return std::unexpected(std::move(model.error()));
        auto switched = state.session.switch_model(std::move(*model));
        if (!switched)
          return std::unexpected(std::move(switched.error()));
        return state_snapshot(*switched ? "model cycled" : "model already selected");
      },
      .on_reload_key_bindings = [&state, &key_bindings, &hotkeys, &state_snapshot,
                                 &application_catalog]() -> ava::core::Result<ava::tui::TuiKeyBindingReloadResult> {
        auto loaded = ava::tui::load_key_bindings(state.session.paths().ava_config_dir / "keybinds.json");
        if (!loaded)
          return std::unexpected(std::move(loaded.error()));
        key_bindings = std::move(*loaded);
        hotkeys = command_hotkeys_from_key_bindings(key_bindings);
        application_catalog.refresh_values(state.session, hotkeys);
        return ava::tui::TuiKeyBindingReloadResult{.key_bindings = key_bindings, .state = state_snapshot("keybindings reloaded")};
      },
      .on_reload_display_settings = [&state, &state_snapshot, &refresh_display_watch_state, &application_catalog,
                                     &hotkeys]() -> ava::core::Result<ava::tui::TuiRuntimeStateSnapshot> {
        auto loaded = ava::app::apply_tui_display_settings(state.session.paths());
        if (!loaded)
          return std::unexpected(std::move(loaded.error()));
        if (auto watched = refresh_display_watch_state(); !watched)
          return std::unexpected(std::move(watched.error()));
        application_catalog.refresh_values(state.session, hotkeys);
        return state_snapshot(display_theme_status("display theme reloaded"));
      },
      .on_maybe_reload_display_settings = [&state, &state_snapshot, &application_catalog, &hotkeys, display_watch_state,
                                           display_watch_mutex]() -> ava::core::Result<std::optional<ava::tui::TuiRuntimeStateSnapshot>> {
        auto watched = ava::app::load_tui_display_settings_watch_state(state.session.paths());
        if (!watched)
          return std::unexpected(std::move(watched.error()));
        std::lock_guard lock(*display_watch_mutex);
        if (*display_watch_state && !ava::app::tui_display_settings_watch_state_changed(**display_watch_state, *watched))
        {
          return std::optional<ava::tui::TuiRuntimeStateSnapshot>{};
        }
        auto loaded = ava::app::apply_tui_display_settings(state.session.paths());
        if (!loaded)
          return std::unexpected(std::move(loaded.error()));
        *display_watch_state = std::move(*watched);
        application_catalog.refresh_values(state.session, hotkeys);
        return state_snapshot(display_theme_status("display theme auto-reloaded"));
      },
      .model_selector_view = [&state]() { return ava::app::model_selector_view_1(state.session, "Enter switch model · type to filter · Esc cancel"); },
      .reasoning_selector_view =
          [&state](bool chained) {
            return ava::app::reasoning_selector_view(
                state.session, chained ? "Enter select · type to filter · Esc keep default" : "Enter select · type to filter · Esc cancel");
          },
      .scoped_model_selector_view = [&state]() { return ava::app::scoped_model_selector_view_1(state.session, scoped_model_selector_footer_hint()); },
      .session_selector_view =
          [&session_selector_sort, &session_selector_named_only, &session_selector_show_paths, &session_selector_show_archived,
           &session_selector_show_label_time, &session_selector_snapshot]() {
            session_selector_sort = ava::app::SessionSelectorSort::Recent;
            session_selector_named_only = false;
            session_selector_show_paths = false;
            session_selector_show_archived = false;
            session_selector_show_label_time = false;
            return session_selector_snapshot();
          },
      .list_subagents = [&state]() { return ava::app::subagent_selector_view(state.session.subagent_coordinator(), state.session.store.session_id()); },
      .inspect_subagent =
          [&state](std::string_view job_id, std::optional<std::uint64_t> known_generation) {
            auto const coordinator = state.session.subagent_coordinator();
            if (!coordinator)
            {
              return ava::core::Result<std::shared_ptr<ava::agent::SubagentInspectorFrame const>>(
                  std::unexpected(ava::core::Error(ava::core::ErrorCategory::NotFound, "subagent workspace is unavailable")));
            }
            return coordinator->inspect(state.session.store.session_id(), job_id, known_generation);
          },
      .cancel_subagent =
          [&state](std::string_view job_id) {
            return ava::app::cancel_subagent_for_workspace(state.session.subagent_coordinator(), state.session.store.session_id(), job_id);
          },
      .promote_subagent =
          [&state](std::string_view job_id) {
            return ava::app::promote_subagent_for_workspace(state.session.subagent_coordinator(), state.session.store.session_id(), job_id);
          },
      .on_open_fork_user_turn_selector = [&state](std::string_view initial_query) -> ava::core::Result<ava::tui::SelectListView> {
        // F-001: refuse sessionless /fork-from before listing/selecting so a later
        // handled no-op cannot open a picker that wipes the live transcript.
        if (auto allowed = ava::app::require_persistent_session_for_fork_from(state.session); !allowed)
          return std::unexpected(std::move(allowed.error()));
        return ava::app::user_turn_selector_view(state.session, "Fork from user turn", "Enter fork · type to filter · Esc cancel", std::string(initial_query));
      },
      .on_open_copy_user_turn_selector = [&state](std::string_view initial_query) -> ava::core::Result<ava::tui::SelectListView> {
        return ava::app::user_turn_selector_view(state.session, "Copy user turn", "Enter copy · type to filter · Esc cancel", std::string(initial_query));
      },
      .on_session_selector_sort_cycle =
          [&session_selector_sort, &session_selector_snapshot]() {
            session_selector_sort = ava::app::next_session_selector_sort(session_selector_sort);
            return session_selector_snapshot();
          },
      .on_session_selector_named_filter_toggle =
          [&session_selector_named_only, &session_selector_snapshot]() {
            session_selector_named_only = !session_selector_named_only;
            return session_selector_snapshot();
          },
      .on_session_selector_path_display_toggle =
          [&session_selector_show_paths, &session_selector_snapshot]() {
            session_selector_show_paths = !session_selector_show_paths;
            return session_selector_snapshot();
          },
      .on_session_selector_archived_filter_toggle =
          [&session_selector_show_archived, &session_selector_snapshot]() {
            session_selector_show_archived = !session_selector_show_archived;
            return session_selector_snapshot();
          },
      .on_session_selector_label_timestamp_toggle =
          [&session_selector_show_label_time, &session_selector_snapshot]() {
            session_selector_show_label_time = !session_selector_show_label_time;
            return session_selector_snapshot();
          },
      .on_session_selector_archive = [&state, &refresh_session_tree_catalog,
                                      &session_selector_snapshot](std::string_view session_id) -> ava::core::Result<ava::tui::SelectListView> {
        auto command = std::string("/sessions archive ") + std::string(session_id) + " --confirm";
        auto archived = ava::app::run_command(state.session, ava::app::CommandRequest{.command = std::move(command)});
        if (!archived)
          return std::unexpected(std::move(archived.error()));
        if (archived->session_tree_changed)
        {
          if (auto refreshed = refresh_session_tree_catalog(); !refreshed)
            return std::unexpected(std::move(refreshed.error()));
        }
        return session_selector_snapshot();
      },
      .on_session_selector_unarchive = [&state, &refresh_session_tree_catalog,
                                        &session_selector_snapshot](std::string_view session_id) -> ava::core::Result<ava::tui::SelectListView> {
        auto command = std::string("/sessions unarchive ") + std::string(session_id);
        auto unarchived = ava::app::run_command(state.session, ava::app::CommandRequest{.command = std::move(command)});
        if (!unarchived)
          return std::unexpected(std::move(unarchived.error()));
        if (unarchived->session_tree_changed)
        {
          if (auto refreshed = refresh_session_tree_catalog(); !refreshed)
            return std::unexpected(std::move(refreshed.error()));
        }
        return session_selector_snapshot();
      },
      .on_session_selector_branch_parent = [open_selector_branch](std::string_view session_id) -> ava::core::Result<ava::tui::TuiRuntimeStateSnapshot> {
        return open_selector_branch(session_id, true);
      },
      .on_session_selector_branch_child = [open_selector_branch](std::string_view session_id) -> ava::core::Result<ava::tui::TuiRuntimeStateSnapshot> {
        return open_selector_branch(session_id, false);
      },
      .remember_permission_rule = [&state](
                                      ava::permissions::PermissionPrompt const& prompt,
                                      ava::permissions::PermissionAction action) { return remember_permission_rule_for_prompt(state.session, prompt, action); },
      .on_settings_selected = [&state, &state_snapshot, &refresh_display_watch_state, &application_catalog,
                               &hotkeys](std::string_view value) -> ava::core::Result<ava::tui::TuiRuntimeStateSnapshot> {
        if (value == "settings:keybindings.validate")
        {
          auto validated = ava::app::run_command(state.session, ava::app::CommandRequest{.command = "/keybindings validate"});
          if (!validated)
            return std::unexpected(std::move(validated.error()));
          auto status = validated->output.empty() ? std::string("keybindings validation complete") : validated->output.front();
          return state_snapshot(std::move(status));
        }
        constexpr std::string_view trust_prefix = "settings:trust.";
        if (value.starts_with(trust_prefix))
        {
          auto action = value.substr(trust_prefix.size());
          auto command = std::string("/trust ") + std::string(action);
          auto trusted = ava::app::run_command(state.session, ava::app::CommandRequest{.command = std::move(command)});
          if (!trusted)
            return std::unexpected(std::move(trusted.error()));
          application_catalog.refresh_values(state.session, hotkeys);
          auto status = trusted->output.empty() ? std::string("trust action complete") : trusted->output.front();
          return state_snapshot(std::move(status));
        }
        constexpr std::string_view theme_prefix = "theme:";
        if (!value.starts_with(theme_prefix))
          return state_snapshot("view closed");
        auto command = std::string("/theme ") + std::string(value.substr(theme_prefix.size()));
        auto themed = ava::app::run_command(state.session, ava::app::CommandRequest{.command = std::move(command)});
        if (!themed)
          return std::unexpected(std::move(themed.error()));
        if (auto watched = refresh_display_watch_state(); !watched)
          return std::unexpected(std::move(watched.error()));
        application_catalog.refresh_values(state.session, hotkeys);
        auto status = themed->output.empty() ? std::string("theme updated") : themed->output.front();
        if (auto const newline = status.find('\n'); newline != std::string::npos)
          status.erase(newline);
        return state_snapshot(std::move(status));
      },
      .on_model_selected = [&state, &state_snapshot](std::string_view value) -> ava::core::Result<ava::tui::TuiRuntimeStateSnapshot> {
        auto const separator = value.find('/');
        if (separator == std::string_view::npos || separator == 0 || separator + 1 >= value.size())
        {
          return std::unexpected(ava::core::Error(ava::core::ErrorCategory::InvalidArgument, "model selection is missing provider/model"));
        }
        auto model = ava::app::resolve_runtime_model(state.session.paths(), value.substr(0, separator), value.substr(separator + 1));
        if (!model)
          return std::unexpected(std::move(model.error()));
        auto switched = state.session.switch_model(std::move(*model));
        if (!switched)
          return std::unexpected(std::move(switched.error()));
        return state_snapshot(*switched ? "model switched" : "model already selected");
      },
      .on_reasoning_selected = [&state, &state_snapshot](std::optional<std::string> level) -> ava::core::Result<ava::tui::TuiRuntimeStateSnapshot> {
        if (!level)
        {
          auto changed = state.session.set_reasoning(std::nullopt);
          if (!changed)
            return std::unexpected(std::move(changed.error()));
          return state_snapshot(*changed ? "thinking mode set to Default" : "thinking mode already Default");
        }
        auto selection = ava::app::reasoning_selection_for_level(state.session.model(), *level);
        if (!selection)
          return std::unexpected(std::move(selection.error()));
        auto changed = state.session.set_reasoning(std::move(*selection));
        if (!changed)
          return std::unexpected(std::move(changed.error()));
        auto const label = ava::app::reasoning_level_label(*level);
        return state_snapshot(*changed ? "thinking mode set to " + label : "thinking mode already " + label);
      },
      .on_fork_user_turn_selected = [&state, &state_snapshot, &application_catalog, &hotkeys,
                                     &refresh_session_tree_catalog](std::string_view entry_id) -> ava::core::Result<ava::tui::TuiRuntimeStateSnapshot> {
        if (entry_id.empty())
        {
          return std::unexpected(ava::core::Error(ava::core::ErrorCategory::InvalidArgument, "fork-from selection is missing entry id"));
        }
        if (auto allowed = ava::app::require_persistent_session_for_fork_from(state.session); !allowed)
          return std::unexpected(std::move(allowed.error()));
        auto const before_session_id = state.session.store.session_id();
        auto const before_session_path = state.session.store.session_path().string();
        auto forked = ava::app::run_fork_command(state.session, {}, entry_id);
        if (!forked)
          return std::unexpected(std::move(forked.error()));

        auto const after_session_id = state.session.store.session_id();
        auto const after_session_path = state.session.store.session_path().string();
        // F-001 defense: never return an "opened" snapshot when authority did not
        // switch (for example sessionless handled success with no branch).
        if (after_session_id == before_session_id && after_session_path == before_session_path)
        {
          auto message = forked->output.empty() ? std::string("fork-from did not switch sessions") : forked->output.front();
          if (auto const newline = message.find('\n'); newline != std::string::npos)
            message.erase(newline);
          auto error = ava::core::Error(ava::core::ErrorCategory::InvalidArgument, std::move(message));
          error.with_context("operation", "on_fork_user_turn_selected");
          error.with_context("session_id", before_session_id);
          return std::unexpected(std::move(error));
        }

        auto status = forked->output.empty() ? std::string("forked session") : forked->output.front();
        if (auto const newline = status.find('\n'); newline != std::string::npos)
          status.erase(newline);

        // F-002: after a successful switch always return/apply the post-fork
        // snapshot. Catalog refresh is soft and visible — never error after
        // mutation and never keep old presentation over the new authority.
        if (forked->session_tree_changed)
        {
          if (auto refreshed = refresh_session_tree_catalog(); !refreshed)
          {
            application_catalog.retarget_session(after_session_id);
            application_catalog.refresh_values(state.session, hotkeys);
            auto warning = ava::app::sanitize_inline_text(refreshed.error().format());
            if (auto const newline = warning.find('\n'); newline != std::string::npos)
              warning.erase(newline);
            if (!status.empty())
              status += " · ";
            status += "session tree refresh deferred: ";
            status += warning;
          }
        }
        else
        {
          application_catalog.retarget_session(after_session_id);
          application_catalog.refresh_values(state.session, hotkeys);
        }
        return state_snapshot(std::move(status));
      },
      .on_read_user_turn_text = [&state](std::string_view entry_id) -> ava::core::Result<std::string> {
        return ava::app::read_session_user_turn_text(state.session, entry_id);
      },
      .on_scoped_model_toggled = [&state](ava::tui::SelectListView const& previous, std::string_view value) -> ava::core::Result<ava::tui::SelectListView> {
        return toggle_scoped_model(state.session, previous, value);
      },
      .on_scoped_model_enable_all = [&state](ava::tui::SelectListView const& previous, std::vector<std::string> values)
          -> ava::core::Result<ava::tui::SelectListView> { return enable_scoped_models(state.session, previous, std::move(values)); },
      .on_scoped_model_clear_all = [&state](ava::tui::SelectListView const& previous, std::vector<std::string> values)
          -> ava::core::Result<ava::tui::SelectListView> { return clear_scoped_models(state.session, previous, std::move(values)); },
      .on_scoped_model_toggle_provider = [&state](ava::tui::SelectListView const& previous, std::string_view value)
          -> ava::core::Result<ava::tui::SelectListView> { return toggle_scoped_model_provider(state.session, previous, value); },
      .on_scoped_model_reorder = [&state](ava::tui::SelectListView const& previous, std::string_view value, bool up)
          -> ava::core::Result<ava::tui::SelectListView> { return reorder_scoped_model(state.session, previous, value, up); },
      .on_scoped_model_save = [&state]() -> ava::core::Result<std::string> { return save_scoped_model_cycle(state.session); },
      .on_session_selected = [&state, &runtime_open_context, &state_snapshot, &application_catalog,
                              &hotkeys](std::string_view value) -> ava::core::Result<ava::tui::TuiRuntimeStateSnapshot> {
        if (value.empty())
        {
          return std::unexpected(ava::core::Error(ava::core::ErrorCategory::InvalidArgument, "session selection is missing session id"));
        }
        if (value == state.session.store.session_id())
        {
          application_catalog.retarget_session(state.session.store.session_id());
          return state_snapshot("session already open");
        }
        auto unlocked_opened_result = state.session.open_requested(runtime_open_context(), value);
        if (!unlocked_opened_result)
          return std::unexpected(std::move(unlocked_opened_result.error()));
        {
          ava::app::runtime::session_ts::wat opened_w(*unlocked_opened_result);
          if (auto replaced = state.session.replace_with(std::move(*opened_w)); !replaced)
            return std::unexpected(std::move(replaced.error()));
        }
        application_catalog.retarget_session(state.session.store.session_id());
        application_catalog.refresh_values(state.session, hotkeys);
        return state_snapshot("session opened");
      }});
  std::cout << std::flush;
  return result;
}

}  // namespace ava::app::line_shell_internal
