#include "sys.h"
#include "ava/provider/catalog.h"
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
#include "ava/app/onboarding_state.h"
#include "ava/app/startup_overview.h"
#include "ava/app/reasoning_controls.h"
#include "ava/app/rpc/runtime_navigation.h"
#include "ava/app/runtime.h"
#include "ava/app/session_title_coordinator.h"
#include "ava/app/session_user_turns.h"
#include "ava/app/subagent_workspace.h"
#include "ava/tui/keybindings.h"
#include "ava/tui/runtime.h"
#include "ava/tui/theme.h"
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

namespace version = ava::core::version;

int run_tui(ShellState state)
{
  ava::app::runtime::session_ts& unlocked_session = state.unlocked_session;
  auto const invocation_paths = ava::app::runtime::session_ts::rat(unlocked_session)->paths();
  auto key_bindings = ava::tui::default_key_bindings();
  std::string keybind_status;
  if (auto loaded = ava::tui::load_key_bindings(invocation_paths.ava_config_dir / "keybinds.json"); loaded)
  {
    key_bindings = std::move(*loaded);
  }
  else
  {
    keybind_status = loaded.error().format();
  }
  auto display_watch_state = std::make_shared<std::optional<ava::app::TuiDisplaySettingsWatchState>>();
  auto display_watch_mutex = std::make_shared<std::mutex>();
  auto refresh_display_watch_state = [&invocation_paths, display_watch_state, display_watch_mutex]() -> ava::core::VoidResult {
    auto watched = ava::app::load_tui_display_settings_watch_state(invocation_paths);
    if (!watched)
      return std::unexpected(std::move(watched.error()));
    std::lock_guard lock(*display_watch_mutex);
    *display_watch_state = std::move(*watched);
    return {};
  };
  auto effective_display_settings =
      std::make_shared<ava::app::TuiDisplaySettings>(ava::app::TuiDisplaySettings{.theme = std::nullopt,
                                                                                  .custom_theme = std::nullopt,
                                                                                  .show_images = true,
                                                                                  .image_width_cells = ava::app::kDefaultTuiImageWidthCells,
                                                                                  .path = ava::app::tui_display_settings_file(invocation_paths)});
  auto effective_display_settings_mutex = std::make_shared<std::mutex>();
  auto remember_effective_display_settings = [effective_display_settings, effective_display_settings_mutex](ava::app::TuiDisplaySettings settings) {
    std::lock_guard lock(*effective_display_settings_mutex);
    *effective_display_settings = std::move(settings);
  };
  auto copy_effective_display_presentation = [effective_display_settings, effective_display_settings_mutex]() {
    std::lock_guard lock(*effective_display_settings_mutex);
    return std::pair{effective_display_settings->show_images, effective_display_settings->image_width_cells};
  };
  if (auto display_settings = ava::app::apply_tui_display_settings(invocation_paths); !display_settings)
  {
    append_status_line(keybind_status, display_settings.error().format());
  }
  else
  {
    remember_effective_display_settings(*display_settings);
    if (auto watched = refresh_display_watch_state(); !watched)
      append_status_line(keybind_status, watched.error().format());
  }
  auto hotkeys = command_hotkeys_from_key_bindings(key_bindings);
  auto const initial_title_coordinator = ava::app::runtime::session_ts::rat(unlocked_session)->session_title_coordinator();
  auto const initial_title_catalog_cursor =
      initial_title_coordinator ? initial_title_coordinator->catalog_changes_since(0).cursor : std::size_t{0};
  auto initial_application_catalog = ava::app::build_application_catalog_cache(unlocked_session, hotkeys);
  ava::app::ApplicationCatalogCoordinator application_catalog(std::move(initial_application_catalog), initial_title_catalog_cursor);
  auto branch_summary_created = ava::app::BranchSummaryCoordinator::create();
  if (!branch_summary_created)
  {
    std::cerr << "error: parent summary coordinator unavailable\n";
    return 1;
  }
  auto branch_summary_coordinator = std::move(*branch_summary_created);
  struct BranchSummaryShutdownGuard
  {
    std::shared_ptr<ava::app::BranchSummaryCoordinator> coordinator;
    ~BranchSummaryShutdownGuard() { coordinator->shutdown(); }
  } branch_summary_shutdown{branch_summary_coordinator};
  auto model_display = [](ava::config::ModelInfo const& model) {
    return model.display_name.empty() ? ava::config::model_display_label(model.model_id) : model.display_name;
  };
  auto custom_theme_options = [&invocation_paths]() {
    std::vector<ava::tui::ThemeOptionItem> themes;
    for (auto const& theme : ava::app::available_tui_custom_themes(invocation_paths))
    {
      themes.push_back(ava::tui::ThemeOptionItem{.name = theme.name, .detail = theme.path.string(), .palette = theme.palette, .revision = theme.revision});
    }
    return themes;
  };
  auto runtime_open_context = [&unlocked_session]() {
    return ava::app::runtime::session_ts::rat(unlocked_session)->replacement_open_context({});
  };
  auto open_requested_session = [&runtime_open_context](std::string_view session_id) {
    auto context = runtime_open_context();
    ava::app::runtime::SessionLifecycleRequest request;
    request.requested_session_id = std::string(session_id);
    return ava::app::runtime::Session::open(context, request);
  };
  auto capture_title_catalog_changes = [&unlocked_session, &application_catalog]() {
    auto const cursor = application_catalog.title_catalog_cursor();
    auto coordinator = ava::app::runtime::session_ts::rat(unlocked_session)->session_title_coordinator();
    return coordinator ? coordinator->catalog_changes_since(cursor) : ava::app::SessionTitleCatalogChanges{.cursor = cursor};
  };
  auto refresh_title_catalog = [&unlocked_session, &application_catalog, &hotkeys, &capture_title_catalog_changes]() -> ava::core::Result<bool> {
    if (!ava::app::runtime::session_ts::rat(unlocked_session)->session_title_coordinator())
      return false;
    return application_catalog.refresh_title_changes(unlocked_session, capture_title_catalog_changes(), hotkeys);
  };
  auto refresh_session_tree_catalog = [&unlocked_session, &application_catalog, &hotkeys, &capture_title_catalog_changes]() {
    auto const captured_changes = capture_title_catalog_changes();
    return application_catalog.refresh_session_tree_and_consume_title_changes(unlocked_session, captured_changes, hotkeys);
  };
  struct SessionPresentation
  {
    std::string mode;
    std::string provider;
    std::string model;
    std::string session_id;
    std::string session_path;
    std::string workspace;
    std::filesystem::path workspace_dir;
    std::size_t context_source_count;
    ava::tui::ProjectTrustSnapshot project_trust;
  };
  auto session_presentation = [&unlocked_session, &model_display]() {
    SCOPED_CRITICAL_AREA_R(session_r, unlocked_session);
    auto const workspace = session_r->current_dir().empty() ? session_r->workspace_dir() : session_r->current_dir();
    return SessionPresentation{.mode = ava::agent::to_string(session_r->mode()),
                               .provider = session_r->model().provider_id,
                               .model = model_display(session_r->model()),
                               .session_id = session_r->store.session_id(),
                               .session_path = session_r->store.session_path().string(),
                               .workspace = workspace.string(),
                               .workspace_dir = session_r->workspace_dir(),
                               .context_source_count = session_r->context_sources().size(),
                               .project_trust = project_trust_snapshot(session_r->project_trust())};
  };
  auto state_snapshot = [&unlocked_session, &application_catalog, &custom_theme_options, &refresh_title_catalog, &session_presentation, &key_bindings,
                         copy_effective_display_presentation](std::string status) {
    static_cast<void>(refresh_title_catalog());
    auto const [show_images, image_width_cells] = copy_effective_display_presentation();
    auto startup_overview = build_startup_overview_snapshot(unlocked_session, key_bindings, ava::tui::active_tui_theme());
    auto delivery = application_catalog.delivery_snapshot();
    auto presentation = session_presentation();
    return ava::tui::TuiRuntimeStateSnapshot{
        .mode = std::move(presentation.mode),
        .provider = std::move(presentation.provider),
        .model = std::move(presentation.model),
        .session_id = std::move(presentation.session_id),
        .session_path = std::move(presentation.session_path),
        .workspace = std::move(presentation.workspace),
        .git_branch = git_branch_for_sidebar(presentation.workspace_dir),
        .context_source_count = presentation.context_source_count,
        .status = std::move(status),
        .slash_commands = std::move(delivery.slash_commands),
        .slash_catalog_generation = delivery.slash_catalog_generation,
        .file_references = std::move(delivery.file_references),
        .workspace_catalog_generation = delivery.workspace_catalog_generation,
        .custom_themes = custom_theme_options(),
        .project_trust = presentation.project_trust,
        .todos = todos_for_session(unlocked_session),
        .show_images = show_images,
        .image_width_cells = image_width_cells,
        .startup_overview = std::move(startup_overview)};
  };
  auto session_selector_sort = ava::app::SessionSelectorSort::Recent;
  bool session_selector_named_only = false;
  bool session_selector_show_paths = false;
  bool session_selector_show_archived = false;
  bool session_selector_show_label_time = false;
  auto session_selector_view_from_catalog = [&]() {
    return application_catalog.session_view(session_selector_sort,
                                            session_selector_footer_hint(session_selector_sort, session_selector_named_only, session_selector_show_paths,
                                                                         session_selector_show_archived, session_selector_show_label_time),
                                            session_selector_named_only, session_selector_show_paths, session_selector_show_archived,
                                            session_selector_show_label_time,
                                            ava::tui::keys_display(key_bindings, ava::tui::TuiAction::SessionSummarizeParent));
  };
  auto session_selector_snapshot = [&]() {
    static_cast<void>(refresh_title_catalog());
    return session_selector_view_from_catalog();
  };
  auto open_session_selector_target = [&unlocked_session, &open_requested_session, &state_snapshot, &application_catalog, &hotkeys](
                                           std::string target_session_id, std::string status_prefix) -> ava::core::Result<ava::tui::TuiRuntimeStateSnapshot> {
    if (target_session_id.empty())
    {
      return std::unexpected(ava::core::Error(ava::core::ErrorCategory::InvalidArgument, "session branch target is missing session id"));
    }
    auto const current_session_id = ava::app::runtime::session_ts::rat(unlocked_session)->store.session_id();
    if (target_session_id == current_session_id)
    {
      application_catalog.retarget_session(current_session_id);
      return state_snapshot(status_prefix + target_session_id + " (already open)");
    }
    auto unlocked_opened_result = open_requested_session(target_session_id);
    if (!unlocked_opened_result)
      return std::unexpected(std::move(unlocked_opened_result.error()));
    if (auto replaced = ava::app::runtime::Session::replace_with(unlocked_session, *unlocked_opened_result); !replaced)
      return std::unexpected(std::move(replaced.error()));
    application_catalog.retarget_session(ava::app::runtime::session_ts::rat(unlocked_session)->store.session_id());
    application_catalog.refresh_values(unlocked_session, hotkeys);
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
  auto branch_summary_prepare = [&state, &application_catalog,
                                 branch_summary_coordinator](std::string_view selected_session_id) -> ava::core::Result<ava::tui::TuiBranchSummarySnapshot> {
    auto source = application_catalog.session_summary(selected_session_id);
    if (!source)
      return std::unexpected(std::move(source.error()));
    if (!*source)
      return std::unexpected(ava::core::Error(ava::core::ErrorCategory::NotFound, "selected parent session is unavailable"));
    auto request = make_branch_summary_operation_request(unlocked_session, std::move(**source));
    if (!request)
      return std::unexpected(std::move(request.error()));
    auto prepared = branch_summary_coordinator->prepare(std::move(*request));
    if (!prepared)
      return std::unexpected(std::move(prepared.error()));
    auto operation = branch_summary_coordinator->snapshot();
    if (operation.generation != *prepared)
      return std::unexpected(ava::core::Error(ava::core::ErrorCategory::Unknown, "parent summary generation is unavailable"));
    return tui_branch_summary_snapshot(operation);
  };
  auto branch_summary_refresh_catalog = [&refresh_session_tree_catalog, &session_selector_view_from_catalog]() -> ava::core::Result<ava::tui::SelectListView> {
    if (auto refreshed = refresh_session_tree_catalog(); !refreshed)
      return std::unexpected(std::move(refreshed.error()));
    return session_selector_view_from_catalog();
  };
  std::vector<ava::tui::TranscriptItem> initial_transcript;
  if (auto onboarding = ava::app::first_run_auth_onboarding_message(unlocked_session))
  {
    initial_transcript.push_back(ava::tui::TranscriptItem{.label = "setup", .text = std::move(*onboarding)});
  }
  // Local-only first-run wizard eligibility is computed only on this interactive TTY TUI path.
  auto const onboarding_state = load_onboarding_state(invocation_paths);
  bool const auto_open_setup_wizard = setup_wizard_auto_eligible(onboarding_state);
  std::optional<std::string> setup_state_diagnostic;
  if (onboarding_state.kind == OnboardingLoadKind::UnsupportedOrMalformed && !onboarding_state.diagnostic.empty())
    setup_state_diagnostic = onboarding_state.diagnostic;
  auto initial_catalog_snapshot = application_catalog.snapshot();
  auto initial_presentation = session_presentation();
  auto const [initial_show_images, initial_image_width_cells] = copy_effective_display_presentation();
  auto initial_startup_overview = build_startup_overview_snapshot(unlocked_session, key_bindings, ava::tui::active_tui_theme());
  auto result = ava::tui::run_interactive_composer(ava::tui::TuiRuntimeOptions{
      .mode = std::move(initial_presentation.mode),
      .provider = std::move(initial_presentation.provider),
      .model = std::move(initial_presentation.model),
      .session_id = std::move(initial_presentation.session_id),
      .session_path = std::move(initial_presentation.session_path),
      .workspace = std::move(initial_presentation.workspace),
      .git_branch = git_branch_for_sidebar(initial_presentation.workspace_dir),
      .app_version = std::string(version::kDisplayVersion),
      .context_source_count = initial_presentation.context_source_count,
      .initial_status = keybind_status,
      .initial_transcript = std::move(initial_transcript),
      .slash_commands = initial_catalog_snapshot.slash_commands,
      .slash_catalog_generation = initial_catalog_snapshot.slash_catalog_generation,
      .file_references = initial_catalog_snapshot.file_references,
      .workspace_catalog_generation = initial_catalog_snapshot.workspace_catalog_generation,
      .custom_themes = custom_theme_options(),
      .project_trust = initial_presentation.project_trust,
      .show_images = initial_show_images,
      .image_width_cells = initial_image_width_cells,
      .startup_overview = std::move(initial_startup_overview),
      .auto_open_setup_wizard = auto_open_setup_wizard,
      .setup_state_diagnostic = std::move(setup_state_diagnostic),
      .setup_readiness = {},
      .on_setup_readiness = [&unlocked_session]() { return build_setup_readiness_snapshot(unlocked_session); },
      .initial_todos = todos_for_session(unlocked_session),
      .key_bindings = key_bindings,
      .token_status_provider = [&unlocked_session]() { return token_status_for_session(unlocked_session); },
      .active_context_status_provider = [&unlocked_session]() { return active_context_status_for_session(unlocked_session); },
      .reasoning_status_provider = [&unlocked_session]() { return ava::app::reasoning_status_for_session(unlocked_session); },
      .create_active_run_queues =
          [&unlocked_session](ava::event::EventEnvelopeSink event_sink) {
            std::shared_ptr<ava::agent::SubagentCoordinator> active_job_coordinator;
            std::string active_job_owner;
            {
              SCOPED_CRITICAL_AREA_R(session_r, unlocked_session);
              active_job_coordinator = session_r->subagent_coordinator();
              active_job_owner = session_r->store.session_id();
            }
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
          [&state, &unlocked_session, &invocation_paths, &hotkeys, &refresh_display_watch_state, &refresh_session_tree_catalog,
           &refresh_title_catalog, &state_snapshot, &application_catalog, remember_effective_display_settings](
              std::string const& submitted, ava::tui::TuiSubmitContext context) {
            // Persistent rules resolve before the TUI fallback resolver in
            // context, so an exact durable Deny never reaches the in-memory
            // session-grant registry.
            auto const permission_rule_store = ava::app::runtime::session_ts::rat(unlocked_session)->permission_rule_store();
            auto permission_resolver =
                ava::permissions::build_persistent_permission_rule_resolver(permission_rule_store, context.permission_resolver);
            auto const session_id_before = ava::app::runtime::session_ts::rat(unlocked_session)->store.session_id();
            bool workspace_catalog_reload = workspace_catalog_reload_requested(submitted);
            auto line_result =
                handle_line(state, submitted, permission_resolver, context.question_resolver, hotkeys, context.event_sink, context.cancel_requested,
                            context.take_steering_messages, std::move(context.image_attachments), context.request_id, context.on_subagent_launch);
            if (is_display_settings_command(submitted))
            {
              if (auto loaded = ava::app::load_tui_display_settings(invocation_paths); loaded)
                remember_effective_display_settings(*loaded);
              if (auto watched = refresh_display_watch_state(); !watched)
                add_output(line_result, watched.error().format());
            }
            auto const session_changed = run_queued_follow_ups_until_session_transition(
                line_result, workspace_catalog_reload, session_id_before, context,
                [&unlocked_session]() { return ava::app::runtime::session_ts::rat(unlocked_session)->store.session_id(); },
                [&](ava::tui::TuiQueuedFollowUp const& follow_up) {
                  return handle_line(state, follow_up.message, permission_resolver, context.question_resolver, hotkeys, context.event_sink,
                                     context.cancel_requested, context.take_steering_messages, {}, follow_up.request_id, context.on_subagent_launch);
                });
            bool const workspace_changed = workspace_catalog_reload || workspace_catalog_changed(line_result);
            if (workspace_changed)
              application_catalog.refresh_workspace(unlocked_session, hotkeys);
            if (line_result.session_tree_changed)
            {
              if (auto refreshed = refresh_session_tree_catalog(); !refreshed)
                add_output(line_result, refreshed.error().format());
            }
            else if (session_changed)
            {
              application_catalog.retarget_session(ava::app::runtime::session_ts::rat(unlocked_session)->store.session_id());
              application_catalog.refresh_values(unlocked_session, hotkeys);
            }
            else if (line_result.ordinary_turn_committed)
            {
              bool current_refreshed = false;
              if (ava::app::runtime::session_ts::rat(unlocked_session)->session_title_coordinator())
              {
                auto refreshed = refresh_title_catalog();
                if (!refreshed)
                  add_output(line_result, refreshed.error().format());
                else
                  current_refreshed = *refreshed;
              }
              if (!current_refreshed)
              {
                auto refreshed = application_catalog.refresh_current_session(unlocked_session, hotkeys);
                if (!refreshed)
                  add_output(line_result, refreshed.error().format());
              }
            }
            else if (!workspace_changed)
              application_catalog.refresh_values(unlocked_session, hotkeys);
            auto const context_source_count = ava::app::runtime::session_ts::rat(unlocked_session)->context_sources().size();
            return ava::tui::TuiSubmitResult{.quit = line_result.quit,
                                             .output = line_result.output,
                                             .tool_timeline = tui_tool_timeline(line_result.tool_timeline),
                                             .context_source_count = context_source_count,
                                             .state_snapshot = state_snapshot({})};
          },
      .on_attach_image = [&unlocked_session](std::string const& path) -> ava::core::Result<ava::session::ImageAttachmentRef> {
        auto source = std::filesystem::path(path);
        if (source.empty())
        {
          return std::unexpected(ava::core::Error(ava::core::ErrorCategory::InvalidArgument, "usage: /attach <image-path>"));
        }
        if (!source.is_absolute())
        {
          SCOPED_CRITICAL_AREA_R(session_r, unlocked_session);
          auto const base = session_r->current_dir().empty() ? session_r->workspace_dir() : session_r->current_dir();
          source = base / source;
        }
        SCOPED_CRITICAL_AREA_W(session_w, unlocked_session);
        return ava::session::import_image_attachment(session_w->store, source);
      },
      .on_paste_clipboard_image = [&unlocked_session]() -> ava::core::Result<std::optional<ava::session::ImageAttachmentRef>> {
        SCOPED_CRITICAL_AREA_W(session_w, unlocked_session);
        return ava::app::import_clipboard_image_attachment(session_w->store);
      },
      .on_external_editor = [](std::string_view initial_text) -> ava::core::Result<std::optional<std::string>> {
        return edit_text_with_external_editor(initial_text);
      },
      .on_load_image_attachment =
          [&unlocked_session](ava::session::ImageAttachmentRef const& attachment) -> ava::core::Result<ava::session::LoadedImageAttachment> {
        SCOPED_CRITICAL_AREA_R(session_r, unlocked_session);
        return ava::session::load_image_attachment(session_r->store, attachment);
      },
      .on_toggle_mode = [&unlocked_session]() -> ava::core::Result<std::string> {
        auto result = ava::app::run_command(unlocked_session, ava::app::CommandRequest{.command = "/mode"});
        if (!result)
          return std::unexpected(std::move(result.error()));
        return ava::agent::to_string(ava::app::runtime::session_ts::rat(unlocked_session)->mode());
      },
      .on_cycle_reasoning = [&unlocked_session]() -> ava::core::Result<std::string> { return ava::app::cycle_runtime_reasoning(unlocked_session); },
      .on_cycle_model = [&unlocked_session, &state_snapshot](bool forward) -> ava::core::Result<ava::tui::TuiRuntimeStateSnapshot> {
        auto model = forward ? ava::app::rpc::next_runtime_model(ava::app::runtime::session_ts::rat(unlocked_session))
                             : ava::app::rpc::previous_runtime_model(ava::app::runtime::session_ts::rat(unlocked_session));
        if (!model)
          return std::unexpected(std::move(model.error()));
        auto switched = ava::app::runtime::Session::switch_model_and_refresh(unlocked_session, std::move(*model));
        if (!switched)
          return std::unexpected(std::move(switched.error()));
        return state_snapshot(*switched ? "model cycled" : "model already selected");
      },
      .on_reload_key_bindings = [&unlocked_session, &invocation_paths, &key_bindings, &hotkeys, &state_snapshot,
                                 &application_catalog]() -> ava::core::Result<ava::tui::TuiKeyBindingReloadResult> {
        auto loaded = ava::tui::load_key_bindings(invocation_paths.ava_config_dir / "keybinds.json");
        if (!loaded)
          return std::unexpected(std::move(loaded.error()));
        key_bindings = std::move(*loaded);
        hotkeys = command_hotkeys_from_key_bindings(key_bindings);
        application_catalog.refresh_values(unlocked_session, hotkeys);
        return ava::tui::TuiKeyBindingReloadResult{.key_bindings = key_bindings, .state = state_snapshot("keybindings reloaded")};
      },
      .on_reload_display_settings = [&unlocked_session, &invocation_paths, &state_snapshot, &refresh_display_watch_state, &application_catalog, &hotkeys,
                                     remember_effective_display_settings]() -> ava::core::Result<ava::tui::TuiRuntimeStateSnapshot> {
        auto loaded = ava::app::apply_tui_display_settings(invocation_paths);
        if (!loaded)
          return std::unexpected(std::move(loaded.error()));
        remember_effective_display_settings(*loaded);
        if (auto watched = refresh_display_watch_state(); !watched)
          return std::unexpected(std::move(watched.error()));
        application_catalog.refresh_values(unlocked_session, hotkeys);
        return state_snapshot(display_theme_status("display theme reloaded"));
      },
      .on_maybe_reload_display_settings = [&unlocked_session, &invocation_paths, &state_snapshot, &application_catalog, &hotkeys, display_watch_state,
                                           display_watch_mutex,
                                           remember_effective_display_settings]() -> ava::core::Result<std::optional<ava::tui::TuiRuntimeStateSnapshot>> {
        auto watched = ava::app::load_tui_display_settings_watch_state(invocation_paths);
        if (!watched)
          return std::unexpected(std::move(watched.error()));
        std::lock_guard lock(*display_watch_mutex);
        if (*display_watch_state && !ava::app::tui_display_settings_watch_state_changed(**display_watch_state, *watched))
        {
          return std::optional<ava::tui::TuiRuntimeStateSnapshot>{};
        }
        auto loaded = ava::app::apply_tui_display_settings(invocation_paths);
        if (!loaded)
          return std::unexpected(std::move(loaded.error()));
        remember_effective_display_settings(*loaded);
        *display_watch_state = std::move(*watched);
        application_catalog.refresh_values(unlocked_session, hotkeys);
        return state_snapshot(display_theme_status("display theme auto-reloaded"));
      },
      .model_selector_view =
          [&unlocked_session]() { return ava::app::model_selector_view_1(unlocked_session, "Enter switch model · type to filter · Esc cancel"); },
      .reasoning_selector_view =
          [&unlocked_session](bool chained) {
            return ava::app::reasoning_selector_view(
                unlocked_session, chained ? "Enter select · type to filter · Esc keep default" : "Enter select · type to filter · Esc cancel");
          },
      .scoped_model_selector_view =
          [&unlocked_session]() { return ava::app::scoped_model_selector_view_1(unlocked_session, scoped_model_selector_footer_hint()); },
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
      .list_subagents = [&unlocked_session]() {
        std::shared_ptr<ava::agent::SubagentCoordinator> coordinator;
        std::string session_id;
        {
          SCOPED_CRITICAL_AREA_R(session_r, unlocked_session);
          coordinator = session_r->subagent_coordinator();
          session_id = session_r->store.session_id();
        }
        return ava::app::subagent_selector_view(coordinator, session_id);
      },
      .inspect_subagent =
          [&unlocked_session](std::string_view job_id, std::optional<std::uint64_t> known_generation) {
            std::shared_ptr<ava::agent::SubagentCoordinator> coordinator;
            std::string session_id;
            {
              SCOPED_CRITICAL_AREA_R(session_r, unlocked_session);
              coordinator = session_r->subagent_coordinator();
              session_id = session_r->store.session_id();
            }
            if (!coordinator)
            {
              return ava::core::Result<std::shared_ptr<ava::agent::SubagentInspectorFrame const>>(
                  std::unexpected(ava::core::Error(ava::core::ErrorCategory::NotFound, "subagent workspace is unavailable")));
            }
            return coordinator->inspect(session_id, job_id, known_generation);
          },
      .cancel_subagent =
          [&unlocked_session](std::string_view job_id) {
            std::shared_ptr<ava::agent::SubagentCoordinator> coordinator;
            std::string session_id;
            {
              SCOPED_CRITICAL_AREA_R(session_r, unlocked_session);
              coordinator = session_r->subagent_coordinator();
              session_id = session_r->store.session_id();
            }
            return ava::app::cancel_subagent_for_workspace(coordinator, session_id, job_id);
          },
      .promote_subagent =
          [&unlocked_session](std::string_view job_id) {
            std::shared_ptr<ava::agent::SubagentCoordinator> coordinator;
            std::string session_id;
            {
              SCOPED_CRITICAL_AREA_R(session_r, unlocked_session);
              coordinator = session_r->subagent_coordinator();
              session_id = session_r->store.session_id();
            }
            return ava::app::promote_subagent_for_workspace(coordinator, session_id, job_id);
          },
      .on_open_fork_user_turn_selector = [&unlocked_session](std::string_view initial_query) -> ava::core::Result<ava::tui::SelectListView> {
        // F-001: refuse sessionless /fork-from before listing/selecting so a later
        // handled no-op cannot open a picker that wipes the live transcript.
        if (auto allowed = ava::app::require_persistent_session_for_fork_from(unlocked_session); !allowed)
          return std::unexpected(std::move(allowed.error()));
        return ava::app::user_turn_selector_view(unlocked_session, "Fork from user turn", "Enter fork · type to filter · Esc cancel",
                                                 std::string(initial_query));
      },
      .on_open_copy_user_turn_selector = [&unlocked_session](std::string_view initial_query) -> ava::core::Result<ava::tui::SelectListView> {
        return ava::app::user_turn_selector_view(unlocked_session, "Copy user turn", "Enter copy · type to filter · Esc cancel",
                                                 std::string(initial_query));
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
      .on_session_selector_archive = [&unlocked_session, &refresh_session_tree_catalog,
                                      &session_selector_snapshot](std::string_view session_id) -> ava::core::Result<ava::tui::SelectListView> {
        auto command = std::string("/sessions archive ") + std::string(session_id) + " --confirm";
        auto archived = ava::app::run_command(unlocked_session, ava::app::CommandRequest{.command = std::move(command)});
        if (!archived)
          return std::unexpected(std::move(archived.error()));
        if (archived->session_tree_changed)
        {
          if (auto refreshed = refresh_session_tree_catalog(); !refreshed)
            return std::unexpected(std::move(refreshed.error()));
        }
        return session_selector_snapshot();
      },
      .on_session_selector_unarchive = [&unlocked_session, &refresh_session_tree_catalog,
                                        &session_selector_snapshot](std::string_view session_id) -> ava::core::Result<ava::tui::SelectListView> {
        auto command = std::string("/sessions unarchive ") + std::string(session_id);
        auto unarchived = ava::app::run_command(unlocked_session, ava::app::CommandRequest{.command = std::move(command)});
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
      .on_branch_summary_prepare = std::move(branch_summary_prepare),
      .branch_summary_snapshot = [branch_summary_coordinator]() { return tui_branch_summary_snapshot(branch_summary_coordinator->snapshot()); },
      .on_branch_summary_confirm = [branch_summary_coordinator](std::uint64_t generation) { return branch_summary_coordinator->confirm(generation); },
      .on_branch_summary_cancel = [branch_summary_coordinator](std::uint64_t generation) { return branch_summary_coordinator->cancel(generation); },
      .on_branch_summary_refresh_catalog = std::move(branch_summary_refresh_catalog),
      .remember_permission_rule = [&unlocked_session](
                                       ava::permissions::PermissionPrompt const& prompt,
                                       ava::permissions::PermissionAction action) { return remember_permission_rule_for_prompt(unlocked_session, prompt, action); },
      .on_settings_selected = [&unlocked_session, &invocation_paths, &state_snapshot, &refresh_display_watch_state, &application_catalog, &hotkeys,
                               remember_effective_display_settings](std::string_view value) -> ava::core::Result<ava::tui::TuiRuntimeStateSnapshot> {
        if (value == "settings:keybindings.validate")
        {
          auto validated = ava::app::run_command(unlocked_session, ava::app::CommandRequest{.command = "/keybindings validate"});
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
          auto trusted = ava::app::run_command(unlocked_session, ava::app::CommandRequest{.command = std::move(command)});
          if (!trusted)
            return std::unexpected(std::move(trusted.error()));
          application_catalog.refresh_values(unlocked_session, hotkeys);
          auto status = trusted->output.empty() ? std::string("trust action complete") : trusted->output.front();
          return state_snapshot(std::move(status));
        }
        auto apply_display_command = [&](std::string command) -> ava::core::Result<ava::tui::TuiRuntimeStateSnapshot> {
          auto ran = ava::app::run_command(unlocked_session, ava::app::CommandRequest{.command = std::move(command)});
          if (!ran)
            return std::unexpected(std::move(ran.error()));
          auto loaded = ava::app::load_tui_display_settings(invocation_paths);
          if (!loaded)
            return std::unexpected(std::move(loaded.error()));
          remember_effective_display_settings(*loaded);
          if (auto watched = refresh_display_watch_state(); !watched)
            return std::unexpected(std::move(watched.error()));
          application_catalog.refresh_values(unlocked_session, hotkeys);
          auto status = ran->output.empty() ? std::string("display settings updated") : ran->output.front();
          if (auto const newline = status.find('\n'); newline != std::string::npos)
            status.erase(newline);
          return state_snapshot(std::move(status));
        };
        if (value == "settings:images.on")
          return apply_display_command("/images on");
        if (value == "settings:images.off")
          return apply_display_command("/images off");
        if (value == "settings:images.reset")
          return apply_display_command("/images reset");
        if (value == "settings:image-width.reset")
          return apply_display_command("/image-width reset");
        constexpr std::string_view image_width_prefix = "settings:image-width.";
        if (value.starts_with(image_width_prefix))
          return apply_display_command(std::string("/image-width ") + std::string(value.substr(image_width_prefix.size())));
        constexpr std::string_view theme_prefix = "theme:";
        if (!value.starts_with(theme_prefix))
          return state_snapshot("view closed");
        return apply_display_command(std::string("/theme ") + std::string(value.substr(theme_prefix.size())));
      },
      .on_setup_persist_status = [&invocation_paths](ava::tui::SetupWizardPersistStatus status) -> ava::core::VoidResult {
        auto const mapped = status == ava::tui::SetupWizardPersistStatus::Completed ? OnboardingStatus::Completed : OnboardingStatus::Skipped;
        return store_onboarding_status(invocation_paths, mapped);
      },
      .on_model_selected = [&unlocked_session, &invocation_paths, &state_snapshot](std::string_view value)
          -> ava::core::Result<ava::tui::TuiRuntimeStateSnapshot> {
        auto const separator = value.find('/');
        if (separator == std::string_view::npos || separator == 0 || separator + 1 >= value.size())
        {
          return std::unexpected(ava::core::Error(ava::core::ErrorCategory::InvalidArgument, "model selection is missing provider/model"));
        }
        auto provider_catalog = runtime::session_ts::crat(unlocked_session)->provider_catalog();
        auto model = ava::app::resolve_runtime_model(invocation_paths, std::move(provider_catalog), value.substr(0, separator), value.substr(separator + 1));
        if (!model)
          return std::unexpected(std::move(model.error()));
        auto switched = ava::app::runtime::Session::switch_model_and_refresh(unlocked_session, std::move(*model));
        if (!switched)
          return std::unexpected(std::move(switched.error()));
        return state_snapshot(*switched ? "model switched" : "model already selected");
      },
      .on_reasoning_selected = [&unlocked_session, &state_snapshot](std::optional<std::string> level)
          -> ava::core::Result<ava::tui::TuiRuntimeStateSnapshot> {
        if (!level)
        {
          auto changed = ava::app::runtime::Session::set_reasoning_and_refresh(unlocked_session, std::nullopt);
          if (!changed)
            return std::unexpected(std::move(changed.error()));
          return state_snapshot(*changed ? "thinking mode set to Default" : "thinking mode already Default");
        }
        auto const model = ava::app::runtime::session_ts::rat(unlocked_session)->model();
        auto selection = ava::app::reasoning_selection_for_level(model, *level);
        if (!selection)
          return std::unexpected(std::move(selection.error()));
        auto changed = ava::app::runtime::Session::set_reasoning_and_refresh(unlocked_session, std::move(*selection));
        if (!changed)
          return std::unexpected(std::move(changed.error()));
        auto const label = ava::app::reasoning_level_label(*level);
        return state_snapshot(*changed ? "thinking mode set to " + label : "thinking mode already " + label);
      },
      .on_fork_user_turn_selected = [&unlocked_session, &state_snapshot, &application_catalog, &hotkeys,
                                     &refresh_session_tree_catalog](std::string_view entry_id) -> ava::core::Result<ava::tui::TuiRuntimeStateSnapshot> {
        if (entry_id.empty())
        {
          return std::unexpected(ava::core::Error(ava::core::ErrorCategory::InvalidArgument, "fork-from selection is missing entry id"));
        }
        if (auto allowed = ava::app::require_persistent_session_for_fork_from(unlocked_session); !allowed)
          return std::unexpected(std::move(allowed.error()));
        std::string before_session_id;
        std::string before_session_path;
        {
          SCOPED_CRITICAL_AREA_R(session_r, unlocked_session);
          before_session_id = session_r->store.session_id();
          before_session_path = session_r->store.session_path().string();
        }
        auto forked = ava::app::run_fork_command(unlocked_session, {}, entry_id);
        if (!forked)
          return std::unexpected(std::move(forked.error()));

        std::string after_session_id;
        std::string after_session_path;
        {
          SCOPED_CRITICAL_AREA_R(session_r, unlocked_session);
          after_session_id = session_r->store.session_id();
          after_session_path = session_r->store.session_path().string();
        }
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
            application_catalog.refresh_values(unlocked_session, hotkeys);
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
          application_catalog.refresh_values(unlocked_session, hotkeys);
        }
        return state_snapshot(std::move(status));
      },
      .on_read_user_turn_text = [&unlocked_session](std::string_view entry_id) -> ava::core::Result<std::string> {
        return ava::app::read_session_user_turn_text(unlocked_session, entry_id);
      },
      .on_scoped_model_toggled =
          [&unlocked_session](ava::tui::SelectListView const& previous, std::string_view value) -> ava::core::Result<ava::tui::SelectListView> {
        return toggle_scoped_model(unlocked_session, previous, value);
      },
      .on_scoped_model_enable_all = [&unlocked_session](ava::tui::SelectListView const& previous, std::vector<std::string> values)
          -> ava::core::Result<ava::tui::SelectListView> { return enable_scoped_models(unlocked_session, previous, std::move(values)); },
      .on_scoped_model_clear_all = [&unlocked_session](ava::tui::SelectListView const& previous, std::vector<std::string> values)
          -> ava::core::Result<ava::tui::SelectListView> { return clear_scoped_models(unlocked_session, previous, std::move(values)); },
      .on_scoped_model_toggle_provider = [&unlocked_session](ava::tui::SelectListView const& previous, std::string_view value)
          -> ava::core::Result<ava::tui::SelectListView> { return toggle_scoped_model_provider(unlocked_session, previous, value); },
      .on_scoped_model_reorder = [&unlocked_session](ava::tui::SelectListView const& previous, std::string_view value, bool up)
          -> ava::core::Result<ava::tui::SelectListView> { return reorder_scoped_model(unlocked_session, previous, value, up); },
      .on_scoped_model_save = [&unlocked_session]() -> ava::core::Result<std::string> { return save_scoped_model_cycle(unlocked_session); },
      .on_session_selected = [&unlocked_session, &open_requested_session, &state_snapshot, &application_catalog,
                              &hotkeys](std::string_view value) -> ava::core::Result<ava::tui::TuiRuntimeStateSnapshot> {
        if (value.empty())
        {
          return std::unexpected(ava::core::Error(ava::core::ErrorCategory::InvalidArgument, "session selection is missing session id"));
        }
        auto const current_session_id = ava::app::runtime::session_ts::rat(unlocked_session)->store.session_id();
        if (value == current_session_id)
        {
          application_catalog.retarget_session(current_session_id);
          return state_snapshot("session already open");
        }
        auto unlocked_opened_result = open_requested_session(value);
        if (!unlocked_opened_result)
          return std::unexpected(std::move(unlocked_opened_result.error()));
        if (auto replaced = ava::app::runtime::Session::replace_with(unlocked_session, *unlocked_opened_result); !replaced)
          return std::unexpected(std::move(replaced.error()));
        application_catalog.retarget_session(ava::app::runtime::session_ts::rat(unlocked_session)->store.session_id());
        application_catalog.refresh_values(unlocked_session, hotkeys);
        return state_snapshot("session opened");
      },
      .on_before_tui_shutdown = [branch_summary_coordinator]() { branch_summary_coordinator->shutdown(); }});
  std::cout << std::flush;
  branch_summary_coordinator->shutdown();
  return result;
}

}  // namespace ava::app::line_shell_internal
