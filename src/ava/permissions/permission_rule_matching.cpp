#include "sys.h"
#include "ava/permissions/permission_rules_internal.h"
#include "ava/core/ids.h"

#include <algorithm>
#include <chrono>
#include <ctime>
#include <filesystem>
#include <iomanip>
#include <optional>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

namespace ava::permissions::permission_rules_internal {

ava::core::Result<std::filesystem::path> normalize_rule_target_path(PermissionRuleStore const& store, PermissionRuleScope scope,
                                                                    std::filesystem::path const& target_path)
{
  if (target_path.empty())
    return std::filesystem::path{};
  auto candidate = target_path;
  if (candidate.is_relative())
  {
    if (scope == PermissionRuleScope::Global)
    {
      auto error = ava::core::Error(ava::core::ErrorCategory::InvalidArgument, "global permission path rules require an absolute target_path");
      error.with_context("target_path", target_path.string());
      return std::unexpected(std::move(error));
    }
    if (contains_parent_reference(candidate))
    {
      auto error = ava::core::Error(ava::core::ErrorCategory::InvalidArgument, "relative permission rule target_path must stay within the workspace");
      error.with_context("target_path", target_path.string());
      return std::unexpected(std::move(error));
    }
    candidate = store.workspace_dir / candidate;
  }
  return normalized_path(candidate);
}
PermissionRuleDraft normalize_draft_defaults(PermissionRuleDraft draft)
{
  if (draft.actor.empty())
    draft.actor = "rpc";
  return draft;
}

bool is_repository_build_or_test_allow(PersistentPermissionRule const& rule);
std::string now_timestamp();

ava::core::Result<PersistentPermissionRule> rule_from_draft(PermissionRuleStore const& store, PermissionRuleDraft draft)
{
  draft = normalize_draft_defaults(std::move(draft));
  auto normalized_target = normalize_rule_target_path(store, draft.scope, draft.target_path);
  if (!normalized_target)
    return std::unexpected(std::move(normalized_target.error()));

  PersistentPermissionRule rule{.rule_id = ava::core::make_id("permrule"),
                                .scope = draft.scope,
                                .workspace_dir = draft.scope == PermissionRuleScope::Workspace ? normalized_path(store.workspace_dir) : std::filesystem::path{},
                                .action = draft.action,
                                .operation = draft.operation,
                                .mode = draft.mode,
                                .tool_name = std::move(draft.tool_name),
                                .target_path = std::move(*normalized_target),
                                .command = std::move(draft.command),
                                .command_recipe_key = std::move(draft.command_recipe_key),
                                .recipe_display = std::move(draft.recipe_display),
                                .critical_acknowledged = draft.critical_acknowledged,
                                .schema_version = kCurrentPermissionRulesSchemaVersion,
                                .reason = std::move(draft.reason),
                                .actor = std::move(draft.actor),
                                .created_at = now_timestamp()};
  auto const path = rules_file_path(store, rule.scope);
  if (auto valid = validate_rule(rule, path); !valid)
    return std::unexpected(std::move(valid.error()));
  if (rule.operation == Operation::RunCommand && rule.action == PermissionAction::Allow && rule.critical_acknowledged)
  {
    return std::unexpected(rule_parse_error("Critical command approvals are one-shot and cannot be persisted", path, "critical_acknowledged"));
  }
  if (is_repository_build_or_test_allow(rule))
  {
    return std::unexpected(rule_parse_error(
        "persistent allow rules require a stable command recipe key; raw repository build/test command text is not authorizing", path, "command_recipe_key"));
  }
  return rule;
}

bool mode_matches(PermissionRuleMode rule_mode, ava::agent::Mode prompt_mode)
{
  switch (rule_mode)
  {
    case PermissionRuleMode::Any:
      return true;
    case PermissionRuleMode::Build:
      return prompt_mode == ava::agent::Mode::Build;
    case PermissionRuleMode::Plan:
      return prompt_mode == ava::agent::Mode::Plan;
  }
  return false;
}

bool scope_matches(PermissionRuleStore const& store, PersistentPermissionRule const& rule, PermissionPrompt const& prompt)
{
  if (rule.scope == PermissionRuleScope::Global)
    return true;
  auto const store_workspace = normalized_path(store.workspace_dir);
  auto const prompt_workspace = normalized_path(prompt.workspace_dir);
  return rule.workspace_dir == store_workspace && prompt_workspace == store_workspace;
}

bool path_matches(PermissionRuleStore const& store, PersistentPermissionRule const& rule, PermissionPrompt const& prompt)
{
  if (rule.target_path.empty())
    return true;
  if (prompt.target_path.empty())
    return false;
  auto const target = normalize_rule_target_path(store, rule.scope, prompt.target_path);
  return target && *target == rule.target_path;
}

bool command_recipe_matches(PersistentPermissionRule const& rule, PermissionPrompt const& prompt)
{
  if (rule.command_recipe_key.empty())
    return true;
  if (!prompt.command_metadata)
    return false;
  auto const& key = rule.scope == PermissionRuleScope::Global ? prompt.command_metadata->global_recipe_key : prompt.command_metadata->workspace_recipe_key;
  return !key.empty() && key == rule.command_recipe_key;
}

bool rule_matches(PermissionRuleStore const& store, PersistentPermissionRule const& rule, PermissionPrompt const& prompt)
{
  if (rule.operation != prompt.operation)
    return false;
  if (!mode_matches(rule.mode, prompt.mode))
    return false;
  if (!scope_matches(store, rule, prompt))
    return false;
  if (!rule.tool_name.empty() && rule.tool_name != prompt.tool_name)
    return false;
  if (!command_recipe_matches(rule, prompt))
    return false;
  bool const typed_command_recipe = rule.operation == Operation::RunCommand && !rule.command_recipe_key.empty();
  if (!typed_command_recipe && !rule.command.empty() && rule.command != prompt.command)
    return false;
  return path_matches(store, rule, prompt);
}

bool command_allow_is_authoritative(PersistentPermissionRule const& rule, PermissionPrompt const& prompt)
{
  if (rule.operation != Operation::RunCommand || rule.action != PermissionAction::Allow || rule.schema_version != kCurrentPermissionRulesSchemaVersion ||
      !prompt.command_metadata)
    return false;
  if (!rule.command_recipe_key.empty())
    return command_prompt_allows_persistent_allow(prompt) && command_recipe_matches(rule, prompt);
  // Schema-v2 exact Critical acknowledgements remain parseable so users can
  // remove old rules, but backend Once is an absolute maximum: no command text
  // can recover persistent execution authority.
  return false;
}

bool is_repository_build_or_test_allow(PersistentPermissionRule const& rule)
{
  return rule.action == PermissionAction::Allow && rule.operation == Operation::RunCommand && rule.command_recipe_key.empty() && !rule.critical_acknowledged &&
         is_repository_controlled_build_or_test_command(rule.command);
}

bool contains_legacy_command_allow(std::vector<PersistentPermissionRule> const& rules)
{
  return std::ranges::any_of(rules, [](PersistentPermissionRule const& rule) {
    return rule.schema_version == kLegacyPermissionRulesSchemaVersion && rule.operation == Operation::RunCommand && rule.action == PermissionAction::Allow;
  });
}

bool workspace_rule(PersistentPermissionRule const& rule)
{
  return rule.scope == PermissionRuleScope::Workspace;
}

int rule_specificity(PersistentPermissionRule const& rule)
{
  int specificity = 0;
  if (!rule.target_path.empty())
    ++specificity;
  if (!rule.command.empty())
    ++specificity;
  if (!rule.command_recipe_key.empty())
    ++specificity;
  if (!rule.tool_name.empty())
    ++specificity;
  if (rule.mode != PermissionRuleMode::Any)
    ++specificity;
  return specificity;
}

std::string now_timestamp()
{
  auto const now = std::chrono::system_clock::now();
  auto const time = std::chrono::system_clock::to_time_t(now);
  std::tm tm{};
  gmtime_r(&time, &tm);
  std::ostringstream out;
  out << std::put_time(&tm, "%Y-%m-%dT%H:%M:%SZ");
  return out.str();
}

PersistentPermissionRule const& prefer_more_specific(PersistentPermissionRule const& current, PersistentPermissionRule const& candidate)
{
  if (!workspace_rule(current) && workspace_rule(candidate))
    return candidate;
  if (workspace_rule(current) == workspace_rule(candidate) && rule_specificity(candidate) > rule_specificity(current))
    return candidate;
  return current;
}

}  // namespace ava::permissions::permission_rules_internal

namespace ava::permissions {

using namespace permission_rules_internal;

ava::core::Result<std::optional<PersistentPermissionRule>> match_persistent_permission_rule(PermissionRuleStore const& store, PermissionPrompt const& prompt)
{
  auto rules = load_persistent_permission_rules(store);
  if (!rules)
    return std::unexpected(std::move(rules.error()));

  std::optional<PersistentPermissionRule> matched_deny;
  std::optional<PersistentPermissionRule> matched_allow;
  for (auto const& rule : *rules)
  {
    if (!rule_matches(store, rule, prompt))
      continue;
    if (rule.action == PermissionAction::Deny)
    {
      matched_deny = matched_deny ? prefer_more_specific(*matched_deny, rule) : rule;
      continue;
    }
    if (rule.operation == Operation::RunCommand && !command_allow_is_authoritative(rule, prompt))
      continue;
    matched_allow = matched_allow ? prefer_more_specific(*matched_allow, rule) : rule;
  }
  if (matched_deny)
    return matched_deny;
  if (matched_allow)
    return matched_allow;
  return std::optional<PersistentPermissionRule>{};
}

}  // namespace ava::permissions
