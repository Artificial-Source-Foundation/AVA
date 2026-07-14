#include "sys.h"
#include "ava/app/project_trust.h"
#include "ava/app/reasoning_controls.h"
#include "ava/app/runtime.h"
#include "ava/app/runtime_json.h"
#include "ava/app/runtime_model.h"
#include "ava/app/runtime_prompt.h"
#include "ava/app/runtime_reasoning.h"
#include "ava/session/session_branch.h"
#include "ava/session/session_metadata.h"
#include "ava/core/ids.h"

#include <filesystem>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace ava::app {
namespace {

ava::core::Result<std::filesystem::path> current_path_result()
{
  std::error_code error;
  auto path = std::filesystem::current_path(error);
  if (!error)
    return path;
  auto result_error = ava::core::Error(ava::core::ErrorCategory::Io, "failed to resolve current directory");
  result_error.with_context("cause", error.message());
  return std::unexpected(std::move(result_error));
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

ava::core::VoidResult apply_initial_reasoning_level(RuntimeSession& session, std::string_view requested_level)
{
  auto level = runtime::trimmed_copy(requested_level);
  if (level.empty())
  {
    auto error = ava::core::Error(ava::core::ErrorCategory::InvalidArgument, "reasoning level is required");
    add_cli_reasoning_context(error, session.model);
    return std::unexpected(std::move(error));
  }

  std::optional<RuntimeReasoningSelection> selection = std::nullopt;
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

}  // namespace

ava::core::Result<RuntimeSession> open_runtime_session(RuntimeOpenOptions const& options)
{
  if (options.requested_session_id && options.continue_last_session)
  {
    return std::unexpected(ava::core::Error(ava::core::ErrorCategory::InvalidArgument, "use either requested session id or continue, not both"));
  }
  if (options.fork_session_id && (options.requested_session_id || options.continue_last_session))
  {
    return std::unexpected(ava::core::Error(ava::core::ErrorCategory::InvalidArgument, "use either fork or session resume options, not both"));
  }
  if (options.sessionless && (options.requested_session_id || options.continue_last_session || options.fork_session_id))
  {
    return std::unexpected(ava::core::Error(ava::core::ErrorCategory::InvalidArgument, "use either no-session or session resume options, not both"));
  }

  auto cwd = current_path_result();
  if (!cwd)
    return std::unexpected(std::move(cwd.error()));
  auto const workspace_dir = options.workspace_dir.empty() ? *cwd : options.workspace_dir;
  auto const current_dir = options.current_dir.empty() ? workspace_dir : options.current_dir;

  if (options.pin_model_override && !options.default_model_override)
  {
    return std::unexpected(ava::core::Error(ava::core::ErrorCategory::InvalidArgument, "pinned runtime model override is missing"));
  }

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

  bool created = true;
  bool append_session_start = true;
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
    auto branch = ava::session::create_session_branch(ava::session::SessionBranchOptions{.workspace_dir = workspace_dir,
                                                                                         .root_dir = options.paths.sessions_dir,
                                                                                         .source_session_id = *resolved,
                                                                                         .branch_from_entry_id = {},
                                                                                         .name = options.initial_session_name,
                                                                                         .labels = std::nullopt,
                                                                                         .mode = ava::session::SessionBranchMode::Fork,
                                                                                         .actor = "cli"});
    if (!branch)
      return std::unexpected(branch.error());
    store = std::move(branch->store);
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
  }
  else if (options.continue_last_session)
  {
    auto sessions = ava::session::SessionStore::list_sessions(workspace_dir, options.paths.sessions_dir);
    if (!sessions)
      return std::unexpected(sessions.error());
    if (!sessions->empty())
    {
      store = ava::session::SessionStore::open(workspace_dir, sessions->front().session_id, options.paths.sessions_dir);
      created = false;
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

  ava::session::SessionLease lease;
  auto acquire_runtime_lease = [&]() -> ava::core::VoidResult {
    if (options.sessionless || !lease.canonical_path().empty())
      return {};
    auto acquired = ava::session::SessionLease::acquire(store->session_path());
    if (!acquired)
      return std::unexpected(std::move(acquired.error()));
    lease = std::move(*acquired);
    return {};
  };
  if (!created || options.fork_session_id)
  {
    if (auto acquired = acquire_runtime_lease(); !acquired)
      return std::unexpected(std::move(acquired.error()));
  }

  std::optional<std::vector<ava::session::SessionEntry>> loaded_entries;
  if (!created || options.fork_session_id)
  {
    auto entries = options.session_read_limits ? store->load_bounded(*options.session_read_limits) : store->load();
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
      auto summary = store->inspect_bounded(options.session_read_limits.value_or(ava::session::SessionReadLimits{}));
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

  std::optional<RuntimeReasoningSelection> reasoning;
  if (loaded_entries)
    reasoning = runtime::latest_persisted_reasoning(*loaded_entries, model);

  auto project_trust = load_project_trust_state(options.paths, workspace_dir);
  auto prompt_state = runtime::load_runtime_prompt_state(options.paths, model, options.mode, workspace_dir, current_dir,
                                                         project_resources_trusted(project_trust), options.prompt_overrides);
  if (!prompt_state)
    return std::unexpected(prompt_state.error());

  if (created)
  {
    if (append_session_start)
    {
      auto appended = runtime::append_session_start(*store, options.mode, model, prompt_state->base_prompt, prompt_state->context_sources.size(), current_dir);
      if (!appended)
        return std::unexpected(appended.error());
    }
    if (auto acquired = acquire_runtime_lease(); !acquired)
      return std::unexpected(std::move(acquired.error()));
  }

  if (options.initial_session_name && !options.fork_session_id)
  {
    auto metadata = ava::session::append_session_metadata(*store, ava::session::SessionMetadataUpdate{.name = options.initial_session_name, .actor = "cli"});
    if (!metadata)
      return std::unexpected(metadata.error());
  }

  RuntimeSession session{.store = std::move(*store),
                         .lease = std::move(lease),
                         .mode = options.mode,
                         .model = std::move(model),
                         .base_prompt = std::move(prompt_state->base_prompt),
                         .paths = options.paths,
                         .workspace_dir = workspace_dir,
                         .current_dir = current_dir,
                         .project_trust = std::move(project_trust),
                         .prompt_overrides = options.prompt_overrides,
                         .tool_visibility = options.tool_visibility,
                         .context_sources = std::move(prompt_state->context_sources),
                         .freshness_sources = std::move(prompt_state->freshness_sources),
                         .system_prompt = std::move(prompt_state->system_prompt),
                         .reasoning = std::move(reasoning),
                         .scoped_model_cycle = registry.scoped_model_cycle,
                         .created = created,
                         .sessionless = options.sessionless,
                         .run_controller = std::make_unique<SessionRunController>(),
                         .background_jobs = std::make_shared<ava::agent::BackgroundJobRegistry>(),
                         .offline = options.offline};

  if (options.initial_reasoning_level)
  {
    if (auto applied = apply_initial_reasoning_level(session, *options.initial_reasoning_level); !applied)
      return std::unexpected(std::move(applied.error()));
  }

  return session;
}

ava::core::VoidResult replace_runtime_session(RuntimeSession& destination, RuntimeSession replacement)
{
  // Owner append routes retain controller state plus a Store reference. Stop
  // and join every old worker before retiring that state, then it is safe to
  // move the old store out under ordinary memberwise assignment.
  auto old_jobs = std::move(destination.background_jobs);
  if (old_jobs)
  {
    old_jobs->request_stop_all();
    old_jobs.reset();
  }
  destination.run_controller.reset();
  destination = std::move(replacement);
  return {};
}

ava::core::Result<ava::session::SessionMetadataView> append_runtime_session_metadata(RuntimeSession& session, ava::session::SessionMetadataUpdate update)
{
  auto entries = session.store.load();
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

ava::core::VoidResult append_runtime_mode_change(RuntimeSession& session, ava::agent::Mode mode)
{
  return session.append_owned(ava::session::SessionEntry{.id = ava::core::make_id("entry"),
                                                         .parent_id = "",
                                                         .type = ava::session::EntryType::ModeChange,
                                                         .timestamp = ava::session::now_timestamp(),
                                                         .data_json = "{\"mode\":\"" + ava::agent::to_string(mode) + "\"}"});
}

std::string to_string(RuntimeFreshnessSourceKind kind)
{
  using enum RuntimeFreshnessSourceKind;
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
