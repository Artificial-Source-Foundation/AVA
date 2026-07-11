#pragma once

#include "ava/permissions/permission.h"

#include "ava/core/result.h"

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace ava::permissions {

// Durable permission rules are intentionally small and inspectable.  AVA stores
// one JSON object per scope (`schema_version: 1`, `rules: [...]`): global rules
// live under the caller-provided config path and enforceable workspace rules are
// stored next to AVA config under a workspace-keyed directory rather than inside
// the model-writable workspace.  A rule matches exact operation plus all
// non-empty target fields (path, command, tool name) and, for workspace rules,
// the normalized workspace directory.  Persistent rules are consulted only after
// the built-in policy has produced an Ask decision, but before session grants and
// resolver prompts; built-in hard Deny decisions are never upgraded by durable
// allow rules. Persistent allow rules cannot authorize repository-controlled
// ctest or cmake --build commands; only a matching in-memory session grant or
// one-shot resolver approval can do so. Matching deny rules always win over
// matching allow rules, and malformed or unsupported storage fails closed by
// returning an authoritative deny
// instead of falling through to a resolver prompt.

inline constexpr long long kCurrentPermissionRulesSchemaVersion = 1;

enum class PermissionRuleScope
{
  Global,
  Workspace,
};

enum class PermissionRuleMode
{
  Any,
  Build,
  Plan,
};

struct PermissionRuleStore
{
  std::filesystem::path global_rules_file;
  std::filesystem::path workspace_rules_file;
  std::filesystem::path workspace_dir;
};

struct PersistentPermissionRule
{
  std::string rule_id;
  PermissionRuleScope scope = PermissionRuleScope::Workspace;
  std::filesystem::path workspace_dir;
  PermissionAction action = PermissionAction::Deny;
  Operation operation = Operation::ReadFile;
  PermissionRuleMode mode = PermissionRuleMode::Any;
  std::string tool_name;
  std::filesystem::path target_path;
  std::string command;
  std::string reason;
  std::string actor;
  std::string created_at;
};

struct PermissionRuleDraft
{
  PermissionRuleScope scope = PermissionRuleScope::Workspace;
  PermissionAction action = PermissionAction::Deny;
  Operation operation = Operation::ReadFile;
  PermissionRuleMode mode = PermissionRuleMode::Any;
  std::string tool_name;
  std::filesystem::path target_path;
  std::string command;
  std::string reason;
  std::string actor = "rpc";
};

[[nodiscard]] std::optional<PermissionRuleScope> parse_permission_rule_scope(std::string_view value);
[[nodiscard]] std::optional<PermissionRuleMode> parse_permission_rule_mode(std::string_view value);
[[nodiscard]] std::string to_string(PermissionRuleScope scope);
[[nodiscard]] std::string to_string(PermissionRuleMode mode);
[[nodiscard]] std::filesystem::path enforceable_permission_rules_file(PermissionRuleStore const& store, PermissionRuleScope scope);
[[nodiscard]] bool is_enforceable_permission_rules_file(PermissionRuleStore const& store, std::filesystem::path const& path);
void register_enforceable_permission_rule_files(PermissionRuleStore const& store);
[[nodiscard]] bool is_registered_enforceable_permission_rules_file(std::filesystem::path const& path);

[[nodiscard]] ava::core::Result<std::vector<PersistentPermissionRule>> load_persistent_permission_rules(PermissionRuleStore const& store);
[[nodiscard]] ava::core::Result<PersistentPermissionRule> add_persistent_permission_rule(PermissionRuleStore const& store, PermissionRuleDraft draft);
[[nodiscard]] ava::core::Result<PersistentPermissionRule> remove_persistent_permission_rule(PermissionRuleStore const& store, std::string_view rule_id);
[[nodiscard]] ava::core::Result<std::optional<PersistentPermissionRule>> match_persistent_permission_rule(PermissionRuleStore const& store,
                                                                                                          PermissionPrompt const& prompt);

[[nodiscard]] PermissionResolver build_persistent_permission_rule_resolver(PermissionRuleStore store, PermissionResolver fallback);

[[nodiscard]] std::string permission_rule_json(PersistentPermissionRule const& rule);
[[nodiscard]] std::string permission_rules_result_json(PermissionRuleStore const& store, std::vector<PersistentPermissionRule> const& rules);

}  // namespace ava::permissions
