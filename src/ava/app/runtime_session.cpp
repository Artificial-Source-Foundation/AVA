#include "sys.h"
#include "ava/app/project_trust.h"
#include "ava/app/reasoning_controls.h"
#include "ava/app/runtime.h"
#include "ava/app/runtime/Session.h"
#include "ava/app/runtime_json.h"
#include "ava/app/runtime_model.h"
#include "ava/app/runtime_prompt.h"
#include "ava/app/runtime_reasoning.h"
#include "ava/core/string_utils.h"
#include "ava/app/session_title_coordinator.h"
#include "ava/app/subagent_delivery_manager.h"
#include "ava/agent/agent_loop_session.h"
#include "ava/config/session_title_config.h"
#include "ava/diagnostics/runtime_diagnostics.h"
#include "ava/session/session_branch.h"
#include "ava/session/session_metadata.h"
#include "ava/core/AnchorSet.h"
#include "ava/core/ids.h"
#include "ava/core/trusted_home.h"

#include <filesystem>
#include <memory>
#include <string>
#include <utility>
#include <vector>
#include "debug.h"

namespace ava::app {
namespace {

ava::core::Result<std::filesystem::path> current_path_result()
{
  return ava::core::launch_workspace_root();
}

ava::core::Result<std::string> resolve_session_id(std::filesystem::path const& workspace_dir, std::filesystem::path const& root_dir,
                                                  std::string_view requested_id)
{
  auto sessions = ava::session::SessionStore::list_sessions(workspace_dir, root_dir);
  if (!sessions)
    return std::unexpected(sessions.error());

  std::vector<std::string> matches;
  for (auto const& session : *sessions)
  {
    if (session.session_id == requested_id || session.session_id.starts_with(requested_id))
    {
      matches.push_back(session.session_id);
    }
  }
  if (matches.empty())
  {
    auto error = ava::core::Error(ava::core::ErrorCategory::NotFound, "session not found");
    error.with_context("session_id", std::string(requested_id));
    return std::unexpected(std::move(error));
  }
  if (matches.size() > 1)
  {
    auto error = ava::core::Error(ava::core::ErrorCategory::InvalidArgument, "session id prefix is ambiguous");
    error.with_context("session_id", std::string(requested_id));
    error.with_context("matches", std::to_string(matches.size()));
    return std::unexpected(std::move(error));
  }
  return matches.front();
}

std::string cli_supported_reasoning_levels(ava::config::ModelInfo const& model)
{
  std::string levels = "off";
  for (auto const& level : ava::config::supported_reasoning_levels(model))
  {
    if (level.empty() || level == "off")
      continue;
    levels += ", ";
    levels += level;
  }
  return levels;
}

void add_cli_reasoning_context(ava::core::Error& error, ava::config::ModelInfo const& model)
{
  error.with_context("option", "--thinking");
  error.with_context("supported_levels", cli_supported_reasoning_levels(model));
}

ava::core::VoidResult apply_initial_reasoning_level(runtime::Session& session, std::string_view requested_level)
{
  auto level = core::trim(requested_level);
  if (level.empty())
  {
    auto error = ava::core::Error(ava::core::ErrorCategory::InvalidArgument, "reasoning level is required");
    add_cli_reasoning_context(error, session.model);
    return std::unexpected(std::move(error));
  }

  std::optional<runtime::ReasoningSelection> selection = std::nullopt;
  if (level != "off")
  {
    auto selected = reasoning_selection_for_level(session.model, std::move(level));
    if (!selected)
    {
      auto error = std::move(selected.error());
      add_cli_reasoning_context(error, session.model);
      return std::unexpected(std::move(error));
    }
    selection = std::move(*selected);
  }

  auto changed = set_runtime_reasoning(session, std::move(selection));
  if (!changed)
  {
    auto error = std::move(changed.error());
    add_cli_reasoning_context(error, session.model);
    return std::unexpected(std::move(error));
  }
  return {};
}

ava::core::Result<std::pair<std::filesystem::path, std::filesystem::path>> resolve_runtime_directories(runtime::OpenOptions const& options)
{
  auto cwd = current_path_result();
  if (!cwd)
    return std::unexpected(std::move(cwd.error()));
  auto workspace_dir = options.workspace_dir.empty() ? *cwd : options.workspace_dir;
  auto current_dir = options.current_dir.empty() ? workspace_dir : options.current_dir;
  return std::pair<std::filesystem::path, std::filesystem::path>{std::move(workspace_dir), std::move(current_dir)};
}

ava::core::Result<ava::session::SessionReadAuthority> bind_runtime_read_authority(ava::session::SessionStore const& store,
                                                                                  ava::session::SessionLease const& lease,
                                                                                  ava::session::SessionReadLimits read_limits)
{
  return store.is_ephemeral() ? ava::session::SessionReadAuthority::create_ephemeral(store, read_limits)
                              : ava::session::SessionReadAuthority::create_persistent(store, lease, read_limits);
}

ava::core::VoidResult reconcile_committed_function_calls(std::shared_ptr<ava::session::SessionAppendTarget> const& append_target,
                                                         ava::session::SessionReadLimits limits)
{
  auto read_authority = append_target->read_authority();
  if (!read_authority)
    return std::unexpected(std::move(read_authority.error()));
  return ava::agent::reconcile_unresolved_committed_function_calls(
      *read_authority, [append_target](ava::session::SessionEntry entry) { return append_target->append(entry); }, limits);
}

ava::core::Result<std::shared_ptr<SubagentDeliveryManager>> delivery_manager_for_options(runtime::OpenOptions const& options,
                                                                                         std::shared_ptr<ava::core::AnchorSet> const& anchor_set)
{
  if (options.subagent_delivery_manager)
    return options.subagent_delivery_manager;
  auto coordinator = options.subagent_coordinator
                         ? ava::core::Result<std::shared_ptr<ava::agent::SubagentCoordinator>>(options.subagent_coordinator)
                         : ava::agent::SubagentCoordinator::create({.ava_state_dir = options.paths.ava_state_dir, .anchor_set = anchor_set});
  if (!coordinator)
    return std::unexpected(std::move(coordinator.error()));
  return SubagentDeliveryManager::create({.coordinator = std::move(*coordinator)});
}

ava::core::Result<std::shared_ptr<SessionTitleCoordinator>> title_coordinator_for_options(
    runtime::OpenOptions const& options, std::shared_ptr<ava::core::AnchorSet> const& anchor_set)
{
  if (options.session_title_coordinator)
    return options.session_title_coordinator;
  auto config = ava::config::load_session_title_config(options.paths, *anchor_set);
  if (!config)
    return std::unexpected(std::move(config.error()));
  return SessionTitleCoordinator::create({.config = std::move(*config)});
}

ava::core::Result<runtime::Session> construct_runtime_session(runtime::OpenOptions const& options, ava::session::SessionStore& store,
                                                              ava::session::SessionLease& lease, bool created, bool load_existing_entries,
                                                              bool append_session_start, bool append_initial_session_name,
                                                              std::shared_ptr<SubagentDeliveryManager> delivery_manager,
                                                              std::shared_ptr<SessionTitleCoordinator> title_coordinator)
{
  auto directories = resolve_runtime_directories(options);
  if (!directories)
    return std::unexpected(std::move(directories.error()));
  auto const& workspace_dir = directories->first;
  auto const& current_dir = directories->second;
  auto const session_read_limits = options.session_read_limits.value_or(ava::session::legacy_unbounded_session_read_limits());

  if (options.pin_model_override && !options.default_model_override)
    return std::unexpected(ava::core::Error(ava::core::ErrorCategory::InvalidArgument, "pinned runtime model override is missing"));

  ava::config::ModelRegistry registry;
  if (options.pin_model_override)
  {
    registry.default_provider_id = options.default_model_override->provider_id;
    registry.default_model_id = options.default_model_override->model_id;
    registry.models = {*options.default_model_override};
    registry.scoped_model_cycle = std::nullopt;
  }
  else
  {
    auto loaded_registry = ava::config::load_model_registry(options.paths);
    if (!loaded_registry)
      return std::unexpected(loaded_registry.error());
    registry = std::move(*loaded_registry);
  }
  auto model = options.default_model_override.value_or(ava::config::select_default_model(registry));

  std::optional<std::vector<ava::session::SessionEntry>> loaded_entries;
  if (load_existing_entries)
  {
    auto read_authority = bind_runtime_read_authority(store, lease, session_read_limits);
    if (!read_authority)
      return std::unexpected(std::move(read_authority.error()));
    auto entries = read_authority->load();
    if (!entries)
      return std::unexpected(std::move(entries.error()));
    if (!options.pin_model_override)
    {
      if (auto persisted_model = runtime::latest_persisted_model(registry, *entries))
        model = std::move(*persisted_model);
    }
    loaded_entries = std::move(*entries);
    if (options.expected_original_cwd)
    {
      auto summary = read_authority->inspect_bounded(session_read_limits);
      if (!summary)
        return std::unexpected(std::move(summary.error()));
      auto const persisted_cwd = summary->original_cwd.empty() ? workspace_dir : summary->original_cwd;
      if (persisted_cwd != *options.expected_original_cwd)
      {
        auto error = ava::core::Error(ava::core::ErrorCategory::InvalidArgument, "requested cwd does not match persisted session cwd");
        error.with_context("persisted_cwd", persisted_cwd.string()).with_context("requested_cwd", options.expected_original_cwd->string());
        return std::unexpected(std::move(error));
      }
    }
  }

  std::optional<runtime::ReasoningSelection> reasoning;
  if (loaded_entries)
    reasoning = runtime::latest_persisted_reasoning(*loaded_entries, model);

  auto project_trust = load_project_trust_state(options.paths, workspace_dir);
  auto prompt_state = runtime::load_runtime_prompt_state(options.paths, model, options.mode, workspace_dir, current_dir,
                                                         project_resources_trusted(project_trust), options.prompt_overrides);
  if (!prompt_state)
    return std::unexpected(prompt_state.error());

  if (append_session_start)
  {
    if (!store.is_ephemeral() && lease.canonical_path().empty())
    {
      auto acquired = ava::session::SessionLease::create_and_acquire(store.session_path());
      if (!acquired)
        return std::unexpected(std::move(acquired.error()));
      lease = std::move(*acquired);
    }
    auto appended =
        store.is_ephemeral()
            ? runtime::append_session_start_ephemeral(store, options.mode, model, prompt_state->base_prompt, prompt_state->context_sources.size(), current_dir)
            : runtime::append_session_start(store, lease, options.mode, model, prompt_state->base_prompt, prompt_state->context_sources.size(), current_dir);
    if (!appended)
      return std::unexpected(std::move(appended.error()));
  }

  bool const sessionless = store.is_ephemeral();
  auto append_target = sessionless ? ava::session::SessionAppendTarget::create_ephemeral(store, session_read_limits)
                                   : ava::session::SessionAppendTarget::create_persistent(store, lease, session_read_limits);
  if (!append_target)
    return std::unexpected(std::move(append_target.error()));
  // Opening/resuming is the durable recovery boundary: the append target is
  // already authority-checked, so any committed function call left without an
  // exact result is closed without re-executing its tool.
  if (auto reconciled = reconcile_committed_function_calls(*append_target, session_read_limits); !reconciled)
    return std::unexpected(std::move(reconciled.error()));

  if (append_initial_session_name && options.initial_session_name)
  {
    auto read_authority = (*append_target)->read_authority();
    if (!read_authority)
      return std::unexpected(std::move(read_authority.error()));
    auto entries = read_authority->load();
    if (!entries)
      return std::unexpected(std::move(entries.error()));
    auto entry = ava::session::make_session_metadata_entry(ava::session::SessionMetadataUpdate{.name = options.initial_session_name, .actor = "cli"},
                                                           entries->empty() ? std::string{} : entries->back().id);
    if (!entry)
      return std::unexpected(std::move(entry.error()));
    if (auto appended = (*append_target)->append(*entry); !appended)
      return std::unexpected(std::move(appended.error()));
  }

  // Open the AnchorSet once for the session lifetime. The logical workspace,
  // AVA-owned roots, spill directory, and any additional writable directories
  // are opened as descriptors shared by parent delivery and every child loop.
  std::shared_ptr<ava::core::AnchorSet> anchor_set;
  {
    std::vector<std::filesystem::path> anchor_roots;
    anchor_roots.push_back(workspace_dir);
    auto const spill_dir = store.session_path().parent_path() / "spill";
    // Create the spill directory now so it can be opened as an anchor.
    // spill_files.cpp also creates it on first use, but we need it to exist
    // before AnchorSet::open so the anchor descriptor is pre-opened.
    std::error_code spill_error;
    std::filesystem::create_directories(spill_dir, spill_error);
    if (spill_error)
      return std::unexpected(ava::core::Error(ava::core::ErrorCategory::Io, "failed to create the session spill anchor"));
    std::filesystem::permissions(spill_dir, std::filesystem::perms::owner_all, std::filesystem::perm_options::replace, spill_error);
    if (spill_error)
      return std::unexpected(ava::core::Error(ava::core::ErrorCategory::Io, "failed to secure the session spill anchor"));
    anchor_roots.push_back(spill_dir);

    std::error_code config_error;
    std::filesystem::create_directories(options.paths.ava_config_dir, config_error);
    if (config_error)
      return std::unexpected(ava::core::Error(ava::core::ErrorCategory::Io, "failed to create the AVA config anchor"));
    std::filesystem::permissions(options.paths.ava_config_dir, std::filesystem::perms::owner_all, std::filesystem::perm_options::replace, config_error);
    if (config_error)
      return std::unexpected(ava::core::Error(ava::core::ErrorCategory::Io, "failed to secure the AVA config anchor"));
    anchor_roots.push_back(options.paths.ava_config_dir);

    std::error_code state_error;
    std::filesystem::create_directories(options.paths.ava_state_dir, state_error);
    if (state_error)
      return std::unexpected(ava::core::Error(ava::core::ErrorCategory::Io, "failed to create the AVA state anchor"));
    std::filesystem::permissions(options.paths.ava_state_dir, std::filesystem::perms::owner_all, std::filesystem::perm_options::replace, state_error);
    if (state_error)
      return std::unexpected(ava::core::Error(ava::core::ErrorCategory::Io, "failed to secure the AVA state anchor"));
    anchor_roots.push_back(options.paths.ava_state_dir);

    for (auto const& dir : options.additional_writable_dirs) anchor_roots.push_back(dir);
    if (options.anchor_set)
    {
      anchor_set = options.anchor_set;
    }
    else
    {
      auto opened = ava::core::AnchorSet::open(anchor_roots);
      if (!opened)
        return std::unexpected(std::move(opened.error()));
      anchor_set = std::move(*opened);
    }
    auto const config_anchor = anchor_set->find_anchor(options.paths.ava_config_dir);
    auto const state_anchor = anchor_set->find_anchor(options.paths.ava_state_dir);
    if (!anchor_set->find_anchor(workspace_dir) || !anchor_set->find_anchor(spill_dir) || !config_anchor || !config_anchor->relative().empty() || !state_anchor ||
        !state_anchor->relative().empty() ||
        std::ranges::any_of(options.additional_writable_dirs, [&](auto const& directory) { return !anchor_set->find_anchor(directory); }))
      return std::unexpected(ava::core::Error(ava::core::ErrorCategory::Io, "failed to retain all required runtime anchors"));
  }

  // Resolve the trusted local account once per process and freeze it. The home
  // directory is read from HOME (passwd fallback) here, at startup, so that
  // later command planning reuses the cached value (ava::core::cached_trusted_account)
  // instead of re-reading the sensitive HOME environment variable after the AI
  // or user shell commands may have run. open_runtime_session is invoked for
  // every session (interactive, print, rpc, ACP, and forked/subagent sessions),
  // so the cache check keeps the actual HOME read to the very first call and
  // lets later sessions reuse the frozen result without tripping the freeze
  // assertion.
  if (auto result = ava::core::load_account_once_and_freeze(); !result)
    return std::unexpected(std::move(result.error()));

  if (options.diagnostics)
  {
    if (auto bound = options.diagnostics->bind_anchor_set(anchor_set); !bound)
      return std::unexpected(std::move(bound.error()));
  }

  if (!delivery_manager)
  {
    auto created_manager = delivery_manager_for_options(options, anchor_set);
    if (!created_manager)
      return std::unexpected(std::move(created_manager.error()));
    delivery_manager = std::move(*created_manager);
  }
  if (!title_coordinator)
  {
    auto created_coordinator = title_coordinator_for_options(options, anchor_set);
    if (!created_coordinator)
      return std::unexpected(std::move(created_coordinator.error()));
    title_coordinator = std::move(*created_coordinator);
  }

  runtime::Session session{.store = std::move(store),
                           .lease = std::move(lease),
                           .mode = options.mode,
                           .model = std::move(model),
                           .base_prompt = std::move(prompt_state->base_prompt),
                           .paths = options.paths,
                           .session_read_limits = session_read_limits,
                           .workspace_dir = workspace_dir,
                           .current_dir = current_dir,
                           .additional_writable_dirs = options.additional_writable_dirs,
                           .anchor_set = std::move(anchor_set),
                           .project_trust = std::move(project_trust),
                           .prompt_overrides = options.prompt_overrides,
                           .tool_visibility = options.tool_visibility,
                           .context_sources = std::move(prompt_state->context_sources),
                           .freshness_sources = std::move(prompt_state->freshness_sources),
                           .system_prompt = std::move(prompt_state->system_prompt),
                           .reasoning = std::move(reasoning),
                           .scoped_model_cycle = registry.scoped_model_cycle,
                           .created = created,
                           .sessionless = sessionless,
                           .run_controller = std::make_shared<SessionRunController>(*append_target),
                           .append_target = std::move(*append_target),
                           .subagent_coordinator = delivery_manager->coordinator(),
                           .subagent_delivery_manager = std::move(delivery_manager),
                           .session_title_coordinator = std::move(title_coordinator),
                           .diagnostics = options.diagnostics,
                           .offline = options.offline};

  if (options.initial_reasoning_level)
  {
    if (auto applied = apply_initial_reasoning_level(session, *options.initial_reasoning_level); !applied)
    {
      auto error = std::move(applied.error());
      store = std::move(session.store);
      lease = std::move(session.lease);
      return std::unexpected(std::move(error));
    }
  }
  // The exact parent session is now fully initialized and its lease is still
  // held. Activate only this parent's journal at the final publication point
  // so a later initialization failure cannot leak an attached owner lease.
  if (!sessionless)
  {
    auto activated = session.subagent_coordinator->activate_parent(session.store.session_id());
    if (!activated)
    {
      auto error = std::move(activated.error());
      store = std::move(session.store);
      lease = std::move(session.lease);
      return std::unexpected(std::move(error));
    }
    session.subagent_delivery_manager->attach_parent(session.store.session_id());
  }
  return session;
}

}  // namespace

ava::core::Result<runtime::Session> open_runtime_session(runtime::OpenOptions const& options)
{
  if (options.requested_session_id && options.continue_last_session)
    return std::unexpected(ava::core::Error(ava::core::ErrorCategory::InvalidArgument, "use either requested session id or continue, not both"));
  if (options.fork_session_id && (options.requested_session_id || options.continue_last_session))
    return std::unexpected(ava::core::Error(ava::core::ErrorCategory::InvalidArgument, "use either fork or session resume options, not both"));
  if (options.sessionless && (options.requested_session_id || options.continue_last_session || options.fork_session_id))
    return std::unexpected(ava::core::Error(ava::core::ErrorCategory::InvalidArgument, "use either no-session or session resume options, not both"));

  auto directories = resolve_runtime_directories(options);
  if (!directories)
    return std::unexpected(std::move(directories.error()));
  auto const& workspace_dir = directories->first;

  if (options.requested_session_id && options.subagent_delivery_manager)
  {
    auto retained = options.subagent_delivery_manager->retained_session(*options.requested_session_id, workspace_dir, options.exact_session_id);
    if (!retained)
      return std::unexpected(std::move(retained.error()));
    if (*retained)
      return std::move(**retained);
  }
  auto const session_read_limits = options.session_read_limits.value_or(ava::session::legacy_unbounded_session_read_limits());

  bool created = true;
  bool created_from_fork = false;
  bool load_existing_entries = false;
  bool append_session_start = true;
  ava::session::SessionLease lease;
  ava::core::Result<ava::session::SessionStore> store = std::unexpected(ava::core::Error(ava::core::ErrorCategory::Unknown, "session was not initialized"));
  if (options.sessionless)
  {
    store = ava::session::SessionStore::create_ephemeral(workspace_dir);
  }
  else if (options.fork_session_id)
  {
    auto resolved = resolve_session_id(workspace_dir, options.paths.sessions_dir, *options.fork_session_id);
    if (!resolved)
      return std::unexpected(resolved.error());
    auto source = ava::session::SessionStore::open(workspace_dir, *resolved, options.paths.sessions_dir);
    if (!source)
      return std::unexpected(std::move(source.error()));
    auto source_lease = ava::session::SessionLease::acquire(source->session_path());
    if (!source_lease)
      return std::unexpected(std::move(source_lease.error()));
    auto recovered = source->recover_torn_tail(*source_lease, session_read_limits);
    if (!recovered)
      return std::unexpected(std::move(recovered.error()));
    auto staged_recovery = source->recover_incomplete_assistant_output_suffix(*source_lease, session_read_limits);
    if (!staged_recovery)
      return std::unexpected(std::move(staged_recovery.error()));
    auto branch = ava::session::create_session_branch(ava::session::SessionBranchOptions{.workspace_dir = workspace_dir,
                                                                                         .root_dir = options.paths.sessions_dir,
                                                                                         .source_session_id = *resolved,
                                                                                         .branch_from_entry_id = {},
                                                                                         .name = options.initial_session_name,
                                                                                         .labels = std::nullopt,
                                                                                         .read_limits = options.session_read_limits,
                                                                                         .source_lease = &*source_lease,
                                                                                         .mode = ava::session::SessionBranchMode::Fork,
                                                                                         .actor = "cli"});
    if (!branch)
      return std::unexpected(std::move(branch.error()));
    store = std::move(branch->store);
    lease = std::move(branch->lease);
    created_from_fork = true;
    load_existing_entries = true;
    append_session_start = false;
  }
  else if (options.requested_session_id)
  {
    if (options.exact_session_id)
    {
      store = ava::session::SessionStore::open(workspace_dir, *options.requested_session_id, options.paths.sessions_dir);
    }
    else
    {
      auto resolved = resolve_session_id(workspace_dir, options.paths.sessions_dir, *options.requested_session_id);
      if (!resolved)
        return std::unexpected(resolved.error());
      store = ava::session::SessionStore::open(workspace_dir, *resolved, options.paths.sessions_dir);
    }
    created = false;
    load_existing_entries = true;
  }
  else if (options.continue_last_session)
  {
    auto sessions = ava::session::SessionStore::list_sessions(workspace_dir, options.paths.sessions_dir);
    if (!sessions)
      return std::unexpected(sessions.error());
    if (!sessions->empty())
    {
      if (options.subagent_delivery_manager)
      {
        auto retained = options.subagent_delivery_manager->retained_session(sessions->front().session_id, workspace_dir, true);
        if (!retained)
          return std::unexpected(std::move(retained.error()));
        if (*retained)
          return std::move(**retained);
      }
      store = ava::session::SessionStore::open(workspace_dir, sessions->front().session_id, options.paths.sessions_dir);
      created = false;
      load_existing_entries = true;
    }
    else
    {
      store = ava::session::SessionStore::create(workspace_dir, options.paths.sessions_dir);
    }
  }
  else
  {
    store = ava::session::SessionStore::create(workspace_dir, options.paths.sessions_dir);
  }
  if (!store)
    return std::unexpected(store.error());

  if (!created)
  {
    auto acquired = ava::session::SessionLease::acquire(store->session_path());
    if (!acquired)
      return std::unexpected(std::move(acquired.error()));
    lease = std::move(*acquired);
    auto recovered = store->recover_torn_tail(lease, session_read_limits);
    if (!recovered)
      return std::unexpected(std::move(recovered.error()));
    auto staged_recovery = store->recover_incomplete_assistant_output_suffix(lease, session_read_limits);
    if (!staged_recovery)
      return std::unexpected(std::move(staged_recovery.error()));
  }

  auto session = construct_runtime_session(options, *store, lease, created, load_existing_entries, created && append_session_start,
                                           options.initial_session_name.has_value() && !options.fork_session_id, options.subagent_delivery_manager,
                                           options.session_title_coordinator);
  if (!session && created_from_fork)
  {
    auto error = std::move(session.error());
    ava::session::rollback_created_session_with_context(*store, lease, error);
    return std::unexpected(std::move(error));
  }
  return session;
}

ava::core::Result<runtime::Session> open_owned_runtime_session(runtime::OpenOptions const& options, ava::session::SessionStore& store,
                                                               ava::session::SessionLease& lease, bool created)
{
  if (options.requested_session_id || options.fork_session_id || options.continue_last_session || options.sessionless || options.initial_session_name ||
      options.initial_reasoning_level)
  {
    return std::unexpected(
        ava::core::Error(ava::core::ErrorCategory::InvalidArgument, "owned runtime session options must not select or initialize another session"));
  }
  if (store.is_ephemeral())
    return std::unexpected(ava::core::Error(ava::core::ErrorCategory::InvalidArgument, "owned runtime session handoff requires a persistent session"));
  if (lease.canonical_path().empty())
    return std::unexpected(ava::core::Error(ava::core::ErrorCategory::InvalidArgument, "owned runtime session handoff requires an active lease"));

  auto const session_read_limits = options.session_read_limits.value_or(ava::session::legacy_unbounded_session_read_limits());
  auto recovered = store.recover_torn_tail(lease, session_read_limits);
  if (!recovered)
    return std::unexpected(std::move(recovered.error()));
  auto staged_recovery = store.recover_incomplete_assistant_output_suffix(lease, session_read_limits);
  if (!staged_recovery)
    return std::unexpected(std::move(staged_recovery.error()));
  return construct_runtime_session(options, store, lease, created, true, false, false, options.subagent_delivery_manager,
                                   options.session_title_coordinator);
}

ava::core::VoidResult replace_runtime_session(runtime::Session& destination, runtime::Session&& replacement)
{
  // Background ownership is application-scoped. Retire only the visible
  // session controller and preserve the exact coordinator across navigation.
  auto coordinator = destination.subagent_coordinator;
  auto delivery_manager = destination.subagent_delivery_manager;
  auto title_coordinator = destination.session_title_coordinator;
  auto const detached_parent_id = destination.sessionless ? std::string{} : destination.store.session_id();
  bool const leaves_detached_parent = !detached_parent_id.empty() && (replacement.sessionless || replacement.store.session_id() != detached_parent_id);
  destination.run_controller.reset();
  destination = std::move(replacement);
  if (delivery_manager)
  {
    destination.subagent_delivery_manager = delivery_manager;
    destination.subagent_coordinator = destination.subagent_delivery_manager->coordinator();
  }
  else if (coordinator)
    destination.subagent_coordinator = coordinator;
  if (title_coordinator)
    destination.session_title_coordinator = std::move(title_coordinator);

  // This is an explicit visible-session detach boundary. The delivery manager
  // keeps a capsule and journal owner when work remains; otherwise its exact
  // generation release allows another AVA process to activate this history.
  if (leaves_detached_parent)
  {
    if (delivery_manager)
      delivery_manager->release_detached_parent(detached_parent_id);
    else if (coordinator)
      static_cast<void>(coordinator->release_parent_if_idle(detached_parent_id));
  }
  return {};
}

ava::core::Result<ava::session::SessionMetadataView> append_runtime_session_metadata(runtime::Session& session, ava::session::SessionMetadataUpdate update)
{
  auto read_authority = session.read_authority();
  if (!read_authority)
    return std::unexpected(std::move(read_authority.error()));
  auto entries = read_authority->load();
  if (!entries)
    return std::unexpected(std::move(entries.error()));
  auto entry = ava::session::make_session_metadata_entry(std::move(update), entries->empty() ? std::string{} : entries->back().id);
  if (!entry)
    return std::unexpected(std::move(entry.error()));
  if (auto appended = session.append_owned(*entry); !appended)
    return std::unexpected(std::move(appended.error()));
  entries->push_back(std::move(*entry));
  return ava::session::session_metadata_from_entries(*entries);
}

ava::core::VoidResult append_runtime_mode_change(runtime::Session& session, ava::agent::Mode mode)
{
  return session.append_owned(ava::session::SessionEntry{.id = ava::core::make_id("entry"),
                                                         .parent_id = "",
                                                         .type = ava::session::EntryType::ModeChange,
                                                         .timestamp = ava::session::now_timestamp(),
                                                         .data_json = "{\"mode\":\"" + ava::agent::to_string(mode) + "\"}"});
}

std::string to_string(runtime::FreshnessSourceKind kind)
{
  using enum runtime::FreshnessSourceKind;
  switch (kind)
  {
    case SystemPrompt:
      return "SystemPrompt";
    case AppendSystemPrompt:
      return "AppendSystemPrompt";
    case PromptCommand:
      return "PromptCommand";
    case Skill:
      return "Skill";
    case PluginManifest:
      return "PluginManifest";
    case PluginPrompt:
      return "PluginPrompt";
    case PluginSkill:
      return "PluginSkill";
  }
  AI_NEVER_REACHED
}

}  // namespace ava::app
