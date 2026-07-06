#include "ava/app/project_trust.h"
#include "ava/app/runtime.h"
#include "ava/app/runtime_json.h"
#include "ava/app/runtime_model.h"
#include "ava/app/runtime_prompt.h"
#include "ava/app/runtime_reasoning.h"
#include "ava/session/session_branch.h"
#include "ava/session/session_metadata.h"

#include <filesystem>
#include <memory>
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

  auto registry = ava::config::load_model_registry(options.paths);
  if (!registry)
    return std::unexpected(registry.error());
  auto model = ava::config::select_default_model(*registry);

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
    auto resolved = resolve_session_id(workspace_dir, options.paths.sessions_dir, *options.requested_session_id);
    if (!resolved)
      return std::unexpected(resolved.error());
    store = ava::session::SessionStore::open(workspace_dir, *resolved, options.paths.sessions_dir);
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

  std::optional<std::vector<ava::session::SessionEntry>> loaded_entries;
  if (!created || options.fork_session_id)
  {
    auto entries = store->load();
    if (!entries)
      return std::unexpected(std::move(entries.error()));
    if (auto persisted_model = runtime::latest_persisted_model(*registry, *entries))
      model = std::move(*persisted_model);
    loaded_entries = std::move(*entries);
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
      auto appended = runtime::append_session_start(*store, options.mode, model, prompt_state->base_prompt, prompt_state->context_sources.size());
      if (!appended)
        return std::unexpected(appended.error());
    }
  }

  if (options.initial_session_name && !options.fork_session_id)
  {
    auto metadata = ava::session::append_session_metadata(*store, ava::session::SessionMetadataUpdate{.name = options.initial_session_name, .actor = "cli"});
    if (!metadata)
      return std::unexpected(metadata.error());
  }

  return RuntimeSession{.store = std::move(*store),
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
                        .scoped_model_cycle = registry->scoped_model_cycle,
                        .created = created,
                        .sessionless = options.sessionless,
                        .background_jobs = std::make_shared<ava::agent::BackgroundJobRegistry>()};
}

}  // namespace ava::app
