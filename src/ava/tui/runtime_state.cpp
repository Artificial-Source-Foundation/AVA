#include "sys.h"
#include "ava/tui/runtime.h"
#include "ava/tui/runtime_state_internal.h"
#include "ava/tui/runtime_views_internal.h"

#include <utility>

namespace ava::tui {
namespace {

ComposerSnapshot initial_snapshot(TuiRuntimeOptions& options)
{
  ComposerSnapshot snapshot{.mode = options.mode,
                            .provider = options.provider,
                            .model = options.model,
                            .session_id = options.session_id,
                            .input = "",
                            .status = options.initial_status,
                            .context_source_count = options.context_source_count,
                            .transcript = std::move(options.initial_transcript),
                            .slash_commands = std::move(options.slash_commands),
                            .file_references = std::move(options.file_references),
                            .custom_themes = options.custom_themes,
                            .project_trust = options.project_trust};
  snapshot.active_run_hint = runtime_views::active_run_hint_for(options.key_bindings);
  return snapshot;
}

}  // namespace

PendingPermissionRequest::PendingPermissionRequest(ava::permissions::PermissionPrompt prompt_in) : prompt(std::move(prompt_in))
{
}

PendingQuestionRequest::PendingQuestionRequest(ava::agent::QuestionPrompt prompt_in) : prompt(std::move(prompt_in))
{
}

ava::app::EventEnvelopeSink EventEnvelopeQueue::sink()
{
  return [this](ava::app::EventEnvelope const& event) -> ava::core::VoidResult {
    std::lock_guard<std::mutex> lock(mutex);
    events.push_back(event);
    received = true;
    return {};
  };
}

std::vector<ava::app::EventEnvelope> EventEnvelopeQueue::drain()
{
  std::lock_guard<std::mutex> lock(mutex);
  auto drained = std::move(events);
  events.clear();
  return drained;
}

bool EventEnvelopeQueue::received_any()
{
  std::lock_guard<std::mutex> lock(mutex);
  return received;
}

RuntimePresentationState::RuntimePresentationState(TuiRuntimeOptions& options)
    : snapshot(initial_snapshot(options)),
      applied_slash_catalog_generation_(options.slash_catalog_generation),
      applied_workspace_catalog_generation_(options.workspace_catalog_generation),
      sidebar{.session_id = options.session_id,
              .mode = options.mode,
              .provider = options.provider,
              .model = options.model,
              .workspace = options.workspace,
              .git_branch = options.git_branch,
              .version = options.app_version,
              .context_source_count = options.context_source_count,
              .session_path = options.session_path}
{
}

void RuntimePresentationState::refresh_token_status(TuiRuntimeOptions const& options)
{
  snapshot.token_status = options.token_status_provider ? options.token_status_provider() : std::nullopt;
  sidebar.token_status = snapshot.token_status;
}

void RuntimePresentationState::refresh_reasoning_status(TuiRuntimeOptions const& options)
{
  snapshot.reasoning_status = options.reasoning_status_provider ? options.reasoning_status_provider() : std::nullopt;
  sidebar.reasoning_status = snapshot.reasoning_status;
}

void RuntimePresentationState::apply_runtime_state_snapshot(TuiRuntimeOptions const& options, TuiRuntimeStateSnapshot state)
{
  auto const session_changed = command_session_grants.clear_for_session_transition(snapshot.session_id, state.session_id);
  if (session_changed)
  {
    pending_image_attachments.clear();
    snapshot.pending_attachments.clear();
  }
  snapshot.mode = std::move(state.mode);
  snapshot.provider = std::move(state.provider);
  snapshot.model = std::move(state.model);
  snapshot.session_id = std::move(state.session_id);
  snapshot.status = std::move(state.status);
  bool completion_sources_changed = false;
  if (!state.slash_catalog_generation || !applied_slash_catalog_generation_ || *state.slash_catalog_generation != *applied_slash_catalog_generation_)
  {
    snapshot.slash_commands = std::move(state.slash_commands);
    applied_slash_catalog_generation_ = state.slash_catalog_generation;
    completion_sources_changed = true;
  }
  if (!state.workspace_catalog_generation || !applied_workspace_catalog_generation_ ||
      *state.workspace_catalog_generation != *applied_workspace_catalog_generation_)
  {
    snapshot.file_references = std::move(state.file_references);
    applied_workspace_catalog_generation_ = state.workspace_catalog_generation;
    completion_sources_changed = true;
  }
  if (completion_sources_changed)
    ++snapshot.file_references_generation;
  snapshot.custom_themes = std::move(state.custom_themes);
  snapshot.project_trust = std::move(state.project_trust);
  snapshot.context_source_count = state.context_source_count;

  sidebar.mode = snapshot.mode;
  sidebar.provider = snapshot.provider;
  sidebar.model = snapshot.model;
  sidebar.session_id = snapshot.session_id;
  sidebar.session_path = std::move(state.session_path);
  sidebar.workspace = std::move(state.workspace);
  sidebar.git_branch = std::move(state.git_branch);
  sidebar.context_source_count = state.context_source_count;
  refresh_token_status(options);
  refresh_reasoning_status(options);
}

}  // namespace ava::tui
