#include "sys.h"
#include "ava/permissions/permission_rules.h"
#include "ava/core/json.h"

#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace ava::permissions {

std::optional<PermissionRuleScope> parse_permission_rule_scope(std::string_view value)
{
  if (value == "global")
    return PermissionRuleScope::Global;
  if (value == "workspace")
    return PermissionRuleScope::Workspace;
  return std::nullopt;
}

std::optional<PermissionRuleMode> parse_permission_rule_mode(std::string_view value)
{
  if (value == "any")
    return PermissionRuleMode::Any;
  if (value == "build")
    return PermissionRuleMode::Build;
  if (value == "plan")
    return PermissionRuleMode::Plan;
  return std::nullopt;
}

std::string to_string(PermissionRuleScope scope)
{
  switch (scope)
  {
    case PermissionRuleScope::Global:
      return "global";
    case PermissionRuleScope::Workspace:
      return "workspace";
  }
  return "workspace";
}

std::string to_string(PermissionRuleMode mode)
{
  switch (mode)
  {
    case PermissionRuleMode::Any:
      return "any";
    case PermissionRuleMode::Build:
      return "build";
    case PermissionRuleMode::Plan:
      return "plan";
  }
  return "any";
}
PermissionResolver build_persistent_permission_rule_resolver(PermissionRuleStore store, PermissionResolver fallback)
{
  register_enforceable_permission_rule_files(store);
  return [store = std::move(store), fallback = std::move(fallback)](PermissionPrompt const& prompt) -> ava::core::Result<PermissionResolutionDecision> {
    auto matched = match_persistent_permission_rule(store, prompt);
    if (!matched)
    {
      PermissionResolutionDecision decision{PermissionResolution::Deny, matched.error().format()};
      decision.resolution_source = "persistent_rule_error";
      decision.authoritative = true;
      return decision;
    }
    if (*matched)
    {
      auto const resolution = (*matched)->action == PermissionAction::Allow ? PermissionResolution::Allow : PermissionResolution::Deny;
      PermissionResolutionDecision decision{resolution, (*matched)->reason};
      decision.resolution_source = "persistent_rule";
      decision.rule_id = (*matched)->rule_id;
      decision.authoritative = true;
      return decision;
    }
    if (fallback)
      return fallback(prompt);
    return PermissionResolution::Deny;
  };
}

PermissionResolver build_persistent_permission_deny_preflight(PermissionRuleStore store)
{
  register_enforceable_permission_rule_files(store);
  return [store = std::move(store)](PermissionPrompt const& prompt) -> ava::core::Result<PermissionResolutionDecision> {
    auto matched = match_persistent_permission_rule(store, prompt);
    if (!matched)
    {
      PermissionResolutionDecision decision{PermissionResolution::Deny, matched.error().format()};
      decision.resolution_source = "persistent_rule_error";
      decision.authoritative = true;
      return decision;
    }
    if (*matched && (*matched)->action == PermissionAction::Deny)
    {
      PermissionResolutionDecision decision{PermissionResolution::Deny, (*matched)->reason};
      decision.resolution_source = "persistent_rule";
      decision.rule_id = (*matched)->rule_id;
      decision.authoritative = true;
      return decision;
    }
    PermissionResolutionDecision decision{PermissionResolution::Allow, "no persistent deny matched"};
    decision.resolution_source = "persistent_deny_preflight";
    decision.authoritative = true;
    return decision;
  };
}
std::string permission_rules_result_json(PermissionRuleStore const& store, std::vector<PersistentPermissionRule> const& rules)
{
  std::string json = "{";
  json += "\"schema_version\":" + std::to_string(kCurrentPermissionRulesSchemaVersion);
  json += ",\"global_rules_file\":\"" + ava::core::json::escape(store.global_rules_file.string()) + "\"";
  json += ",\"workspace_rules_file\":\"" + ava::core::json::escape(enforceable_permission_rules_file(store, PermissionRuleScope::Workspace).string()) + "\"";
  json += ",\"rules\":[";
  for (std::size_t index = 0; index < rules.size(); ++index)
  {
    if (index > 0)
      json += ',';
    json += permission_rule_json(rules[index]);
  }
  json += "]}";
  return json;
}

}  // namespace ava::permissions
