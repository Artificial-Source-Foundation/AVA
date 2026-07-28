#include "sys.h"
#include "ava/app/command_format.h"
#include "ava/app/command_trust.h"
#include "ava/app/commands.h"
#include "ava/app/project_trust.h"
#include "ava/app/runtime.h"
#include "ava/app/runtime/Session.h"
#include "ava/app/runtime_prompt.h"

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

ava::core::Result<CommandResult> reload_project_trust_state(runtime::Session& session, std::string prefix)
{
  auto next_trust = load_project_trust_state(session.paths(), session.workspace_dir());
  auto prompt_state = runtime::load_runtime_prompt_state(session.paths(), session.model(), session.mode(), session.workspace_dir(), session.current_dir(),
                                                         project_resources_trusted(next_trust), session.prompt_overrides());
  if (!prompt_state)
    return std::unexpected(std::move(prompt_state.error()));
  session.trust_state().project_trust = std::move(next_trust);
  if (auto refreshed = apply_runtime_prompt_state(session, std::move(*prompt_state)); !refreshed)
    return std::unexpected(std::move(refreshed.error()));
  return handled_text(std::move(prefix) + "\n" + project_trust_summary(session.project_trust()));
}

}  // namespace

ava::core::Result<CommandResult> run_trust_command(runtime::Session& session, std::string_view argument)
{
  auto const args = split_command_arguments(argument);
  auto const action = args.empty() ? std::string("status") : args.front();
  if (action == "status")
    return handled_text(project_trust_summary(session.project_trust()));
  if (action == "project" || action == "trust" || action == "approve")
  {
    auto saved = set_project_trust_decision(session.paths(), session.workspace_dir(), true);
    if (!saved)
      return std::unexpected(std::move(saved.error()));
    return reload_project_trust_state(session, "trusted project resources");
  }
  if (action == "deny" || action == "untrust")
  {
    auto saved = set_project_trust_decision(session.paths(), session.workspace_dir(), false);
    if (!saved)
      return std::unexpected(std::move(saved.error()));
    return reload_project_trust_state(session, "denied project resources");
  }
  if (action == "clear")
  {
    auto cleared = clear_project_trust_decision(session.paths(), session.workspace_dir());
    if (!cleared)
      return std::unexpected(std::move(cleared.error()));
    return reload_project_trust_state(session, "cleared project trust decision");
  }
  return handled_text("unsupported trust action: " + action + "\nsupported: status, project, deny, clear");
}

}  // namespace ava::app
