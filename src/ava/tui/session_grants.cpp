#include "sys.h"
#include "ava/tui/session_grants.h"

namespace ava::tui {

bool tui_session_grant_eligible(ava::permissions::PermissionPrompt const& prompt) noexcept
{
  if (prompt.operation != ava::permissions::Operation::RunCommand || prompt.risk == ava::permissions::PermissionRisk::Critical || !prompt.command_metadata ||
      !ava::permissions::command_permission_allows_reusable_grant(*prompt.command_metadata))
    return false;

  for (auto const scope : prompt.command_metadata->effective_allowed_scopes)
  {
    if (scope == ava::command::InteractiveScope::Session)
      return true;
  }
  return false;
}

bool TuiSessionGrantRegistry::matches(std::string_view session_id, ava::permissions::PermissionPrompt const& prompt) const
{
  // Recheck the backend-issued metadata on every lookup. A stored grant can
  // never make a changed or downgraded command recipe reusable.
  if (session_id.empty() || !tui_session_grant_eligible(prompt))
    return false;

  auto const& recipe_key = prompt.command_metadata->workspace_recipe_key;
  for (auto const& grant : grants_)
  {
    if (grant.session_id == session_id && grant.mode == prompt.mode && grant.tool_name == prompt.tool_name && grant.workspace_recipe_key == recipe_key)
      return true;
  }
  return false;
}

TuiSessionGrantInsertResult TuiSessionGrantRegistry::add(std::string_view session_id, ava::permissions::PermissionPrompt const& prompt)
{
  if (session_id.empty() || !tui_session_grant_eligible(prompt))
    return TuiSessionGrantInsertResult::Ineligible;
  if (matches(session_id, prompt))
    return TuiSessionGrantInsertResult::AlreadyPresent;
  if (grants_.size() >= kMaxGrants)
    return TuiSessionGrantInsertResult::Full;

  grants_.push_back(Grant{.session_id = std::string(session_id),
                          .mode = prompt.mode,
                          .tool_name = prompt.tool_name,
                          .workspace_recipe_key = prompt.command_metadata->workspace_recipe_key});
  return TuiSessionGrantInsertResult::Added;
}

bool TuiSessionGrantRegistry::clear_for_session_transition(std::string_view current_session_id, std::string_view next_session_id) noexcept
{
  if (next_session_id == current_session_id)
    return false;
  grants_.clear();
  return true;
}

std::size_t TuiSessionGrantRegistry::size() const noexcept
{
  return grants_.size();
}

}  // namespace ava::tui
