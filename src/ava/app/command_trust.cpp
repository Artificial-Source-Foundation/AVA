#include "sys.h"
#include "ava/app/command_format.h"
#include "ava/app/command_trust.h"
#include "ava/app/commands.h"
#include "ava/app/project_trust.h"
#include "ava/app/runtime.h"
#include "ava/app/runtime/Session.h"
#include "ava/app/runtime_prompt.h"

#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

namespace ava::app {
namespace {

std::string project_trust_summary(ProjectTrustState const& state)
{
  std::string output = "Project trust:\n";
  output += "  workspace=" + state.workspace_dir.string() + "\n";
  output += "  decision=" + std::string(to_string(state.decision)) + "\n";
  if (!state.matched_path.empty())
    output += "  matched=" + state.matched_path.string() + "\n";
  output += "  trust_file=" + state.trust_file.string() + "\n";
  output += "  project_resources=" + std::string(project_resources_trusted(state) ? "enabled" : "skipped") + "\n";
  if (!state.diagnostic.empty())
    output += "  diagnostic=" + sanitize_inline_text(state.diagnostic) + "\n";
  if (state.protected_resources.empty())
  {
    output += "  protected_resources=none";
    return output;
  }
  output += "  protected_resources=" + std::to_string(state.protected_resources.size()) + "\n";
  for (auto const& resource : state.protected_resources)
  {
    output += "    " + sanitize_inline_text(resource.kind) + "  " + resource.path.string() + "\n";
  }
  if (!output.empty() && output.back() == '\n')
    output.pop_back();
  return output;
}

ava::core::Result<CommandResult> reload_project_trust_state(runtime::session_ts& unlocked_session, std::string prefix)
{
  ava::config::XdgPaths paths;
  ava::config::ModelInfo model;
  ava::agent::Mode mode;
  std::filesystem::path workspace_dir;
  std::filesystem::path current_dir;
  runtime::PromptOverrides prompt_overrides;
  std::optional<std::string> requested_primary_agent;
  {
    SCOPED_CRITICAL_AREA_R(session_r, unlocked_session);
    paths = session_r->paths();
    model = session_r->model();
    mode = session_r->mode();
    workspace_dir = session_r->workspace_dir();
    current_dir = session_r->current_dir();
    prompt_overrides = session_r->prompt_overrides();
    requested_primary_agent = session_r->requested_primary_agent();
  }
  auto next_trust = load_project_trust_state(paths, workspace_dir);
  auto selected_primary_agent = runtime::resolve_runtime_primary_agent(paths, workspace_dir, project_resources_trusted(next_trust), requested_primary_agent,
                                                                       runtime::PrimaryAgentResolutionPolicy::AllowUnavailable);
  if (!selected_primary_agent)
    return std::unexpected(std::move(selected_primary_agent.error()));
  auto prompt_state = runtime::load_runtime_prompt_state(paths, model, mode, workspace_dir, current_dir, project_resources_trusted(next_trust),
                                                         prompt_overrides, *selected_primary_agent);
  if (!prompt_state)
  {
    auto error = std::move(prompt_state.error());
    if (!project_resources_trusted(next_trust))
    {
      // A persisted revocation must not leave the old project prompt live merely because unrelated non-project prompt reconstruction failed.
      runtime::PromptState fail_closed_prompt;
      fail_closed_prompt.mode = mode;
      if (auto secured =
              runtime::Session::apply_trust_prompt_state_and_refresh(unlocked_session, std::move(next_trust), std::nullopt, std::move(fail_closed_prompt));
          !secured)
      {
        return std::unexpected(std::move(secured.error()));
      }
    }
    return std::unexpected(std::move(error));
  }
  if (auto refreshed = runtime::Session::apply_trust_prompt_state_and_refresh(unlocked_session, std::move(next_trust), std::move(*selected_primary_agent),
                                                                              std::move(*prompt_state));
      !refreshed)
  {
    return std::unexpected(std::move(refreshed.error()));
  }
  auto const applied_trust = runtime::session_ts::rat(unlocked_session)->project_trust();
  return handled_text(std::move(prefix) + "\n" + project_trust_summary(applied_trust));
}

}  // namespace

ava::core::Result<CommandResult> run_trust_command(runtime::session_ts& unlocked_session, std::string_view argument)
{
  auto const args = split_command_arguments(argument);
  auto const action = args.empty() ? std::string("status") : args.front();
  if (action == "status")
    return handled_text(project_trust_summary(runtime::session_ts::rat(unlocked_session)->project_trust()));

  bool const enables_trust = action == "project" || action == "trust" || action == "approve";
  bool const denies_trust = action == "deny" || action == "untrust";
  bool const clears_trust = action == "clear";
  if (!enables_trust && !denies_trust && !clears_trust)
    return handled_text("unsupported trust action: " + action + "\nsupported: status, project, deny, clear");

  ava::config::XdgPaths paths;
  std::filesystem::path workspace_dir;
  std::shared_ptr<SessionRunController> run_controller;
  {
    SCOPED_CRITICAL_AREA_R(session_r, unlocked_session);
    paths = session_r->paths();
    workspace_dir = session_r->workspace_dir();
    run_controller = session_r->run_controller();
  }
  if (!run_controller)
    return std::unexpected(ava::core::Error(ava::core::ErrorCategory::InvalidArgument, "runtime session controller is unavailable"));
  // Exclude provider admission from the persisted trust write through the atomic in-memory authority publication.
  auto maintenance = run_controller->reserve_maintenance();
  if (!maintenance)
    return std::unexpected(std::move(maintenance.error()));

  if (enables_trust)
  {
    auto saved = set_project_trust_decision(paths, workspace_dir, true);
    if (!saved)
      return std::unexpected(std::move(saved.error()));
    return reload_project_trust_state(unlocked_session, "trusted project resources");
  }
  if (denies_trust)
  {
    auto saved = set_project_trust_decision(paths, workspace_dir, false);
    if (!saved)
      return std::unexpected(std::move(saved.error()));
    return reload_project_trust_state(unlocked_session, "denied project resources");
  }

  auto cleared = clear_project_trust_decision(paths, workspace_dir);
  if (!cleared)
    return std::unexpected(std::move(cleared.error()));
  return reload_project_trust_state(unlocked_session, "cleared project trust decision");
}

}  // namespace ava::app
