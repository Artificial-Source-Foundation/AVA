#include "sys.h"
#include "tests/support/test_harness.h"
#include "ava/tools/file_tools.h"
#include "ava/permissions/permission_rules.h"

#include <array>
#include <filesystem>
#include <fstream>
#include <string>
#include <utility>
#include <sys/stat.h>

namespace {

ava::permissions::PermissionRuleStore test_store(std::filesystem::path const& root)
{
  auto const workspace = root / "workspace";
  std::filesystem::create_directories(workspace);
  return ava::permissions::PermissionRuleStore{.global_rules_file = root / "config" / "ava" / "permission-rules.json",
                                               .workspace_rules_file = workspace / ".ava" / "permission-rules.json",
                                               .workspace_dir = workspace};
}

void write_file_with_mode(std::filesystem::path const& path, std::string const& content, mode_t mode)
{
  std::filesystem::create_directories(path.parent_path());
  std::ofstream file(path, std::ios::binary | std::ios::trunc);
  file << content;
  file.close();
  ::chmod(path.c_str(), mode);
}

bool owner_only_file(std::filesystem::path const& path)
{
  struct stat st{};
  if (::stat(path.c_str(), &st) != 0)
    return false;
  return (st.st_mode & (S_IRWXG | S_IRWXO)) == 0;
}

ava::permissions::PermissionPrompt read_prompt(ava::permissions::PermissionRuleStore const& store, std::filesystem::path const& target)
{
  return ava::permissions::PermissionPrompt{.permission_request_id = "permreq_test",
                                            .operation = ava::permissions::Operation::ReadFile,
                                            .mode = ava::agent::Mode::Build,
                                            .workspace_dir = store.workspace_dir,
                                            .target_path = target,
                                            .command = "",
                                            .tool_name = "read_file",
                                            .reason = "target is outside the workspace",
                                            .risk = ava::permissions::PermissionRisk::High};
}

void test_permission_rule_storage_add_list_remove()
{
  auto const root = temp_root() / "permission-rules-storage";
  std::error_code remove_error;
  std::filesystem::remove_all(root, remove_error);
  auto const store = test_store(root);
  auto const outside = root / "outside.txt";

  auto added =
      ava::permissions::add_persistent_permission_rule(store, ava::permissions::PermissionRuleDraft{.scope = ava::permissions::PermissionRuleScope::Workspace,
                                                                                                    .action = ava::permissions::PermissionAction::Allow,
                                                                                                    .operation = ava::permissions::Operation::ReadFile,
                                                                                                    .mode = ava::permissions::PermissionRuleMode::Any,
                                                                                                    .tool_name = "",
                                                                                                    .target_path = outside,
                                                                                                    .command = "",
                                                                                                    .command_recipe_key = {},
                                                                                                    .recipe_display = {},
                                                                                                    .critical_acknowledged = false,
                                                                                                    .reason = "allow this exact file",
                                                                                                    .actor = "test"});
  expect(added.has_value() && added->rule_id.starts_with("permrule_"), "permission rule storage creates a stable rule id");
  auto const workspace_rules_file = ava::permissions::enforceable_permission_rules_file(store, ava::permissions::PermissionRuleScope::Workspace);
  expect(workspace_rules_file != store.workspace_rules_file && std::filesystem::exists(workspace_rules_file) && owner_only_file(workspace_rules_file),
         "permission rule storage writes an owner-only inspectable workspace file outside the workspace");

  auto loaded = ava::permissions::load_persistent_permission_rules(store);
  expect(loaded && loaded->size() == 1 && loaded->front().rule_id == added->rule_id && loaded->front().target_path == added->target_path,
         "permission rule storage loads persisted workspace rules");

  auto matched = ava::permissions::match_persistent_permission_rule(store, read_prompt(store, outside));
  expect(matched && *matched && (*matched)->rule_id == added->rule_id && (*matched)->action == ava::permissions::PermissionAction::Allow,
         "permission rule matcher applies exact persisted path rules");

  auto removed = ava::permissions::remove_persistent_permission_rule(store, added->rule_id);
  expect(removed && removed->rule_id == added->rule_id, "permission rule removal returns the removed rule");
  auto after_remove = ava::permissions::load_persistent_permission_rules(store);
  expect(after_remove && after_remove->empty(), "permission rule removal persists an empty rules array");
}

void test_legacy_command_allows_are_removed_one_at_a_time()
{
  auto const root = temp_root() / "permission-rules-legacy-sequential-remove";
  std::error_code remove_error;
  std::filesystem::remove_all(root, remove_error);
  auto const store = test_store(root);
  auto legacy_allow = ava::permissions::PersistentPermissionRule{.rule_id = "permrule_legacy_first",
                                                                 .scope = ava::permissions::PermissionRuleScope::Global,
                                                                 .workspace_dir = {},
                                                                 .action = ava::permissions::PermissionAction::Allow,
                                                                 .operation = ava::permissions::Operation::RunCommand,
                                                                 .mode = ava::permissions::PermissionRuleMode::Build,
                                                                 .tool_name = "bash",
                                                                 .target_path = {},
                                                                 .command = "legacy first",
                                                                 .command_recipe_key = {},
                                                                 .recipe_display = {},
                                                                 .critical_acknowledged = false,
                                                                 .schema_version = ava::permissions::kLegacyPermissionRulesSchemaVersion,
                                                                 .reason = "legacy command allow",
                                                                 .actor = "test",
                                                                 .created_at = "2026-07-19T00:00:00Z"};
  auto second_legacy_allow = legacy_allow;
  second_legacy_allow.rule_id = "permrule_legacy_second";
  second_legacy_allow.command = "legacy second";
  write_file_with_mode(store.global_rules_file,
                       std::string("{\"schema_version\":1,\"rules\":[") + ava::permissions::permission_rule_json(legacy_allow) + "," +
                           ava::permissions::permission_rule_json(second_legacy_allow) + "]}",
                       S_IRUSR | S_IWUSR);

  auto first_removed = ava::permissions::remove_persistent_permission_rule(store, legacy_allow.rule_id);
  std::ifstream after_first_file(store.global_rules_file);
  std::string after_first((std::istreambuf_iterator<char>(after_first_file)), std::istreambuf_iterator<char>());
  auto add_while_legacy_remains =
      ava::permissions::add_persistent_permission_rule(store, ava::permissions::PermissionRuleDraft{.scope = ava::permissions::PermissionRuleScope::Global,
                                                                                                    .action = ava::permissions::PermissionAction::Deny,
                                                                                                    .operation = ava::permissions::Operation::ReadFile,
                                                                                                    .mode = ava::permissions::PermissionRuleMode::Any,
                                                                                                    .tool_name = "",
                                                                                                    .target_path = root / "outside.txt",
                                                                                                    .command = "",
                                                                                                    .command_recipe_key = {},
                                                                                                    .recipe_display = {},
                                                                                                    .critical_acknowledged = false,
                                                                                                    .reason = "must not rewrite legacy command allows",
                                                                                                    .actor = "test"});
  auto second_removed = ava::permissions::remove_persistent_permission_rule(store, second_legacy_allow.rule_id);
  std::ifstream after_second_file(store.global_rules_file);
  std::string after_second((std::istreambuf_iterator<char>(after_second_file)), std::istreambuf_iterator<char>());
  auto reloaded = ava::permissions::load_persistent_permission_rules(store);

  expect(
      first_removed && first_removed->rule_id == legacy_allow.rule_id && after_first.find("\"schema_version\":1") != std::string::npos &&
          after_first.find(second_legacy_allow.rule_id) != std::string::npos && !add_while_legacy_remains && second_removed &&
          second_removed->rule_id == second_legacy_allow.rule_id && after_second.find("\"schema_version\":2") != std::string::npos && reloaded &&
          reloaded->empty(),
      "legacy command Allows can be removed sequentially without rewriting the remaining v1 Allow, additions remain blocked, and final removal migrates to v2");
}

void test_permission_rule_precedence_denies_win()
{
  auto const root = temp_root() / "permission-rules-precedence";
  std::error_code remove_error;
  std::filesystem::remove_all(root, remove_error);
  auto const store = test_store(root);
  auto const outside = root / "outside.txt";

  auto global_allow =
      ava::permissions::add_persistent_permission_rule(store, ava::permissions::PermissionRuleDraft{.scope = ava::permissions::PermissionRuleScope::Global,
                                                                                                    .action = ava::permissions::PermissionAction::Allow,
                                                                                                    .operation = ava::permissions::Operation::ReadFile,
                                                                                                    .mode = ava::permissions::PermissionRuleMode::Any,
                                                                                                    .tool_name = "",
                                                                                                    .target_path = outside,
                                                                                                    .command = "",
                                                                                                    .command_recipe_key = {},
                                                                                                    .recipe_display = {},
                                                                                                    .critical_acknowledged = false,
                                                                                                    .reason = "global allow",
                                                                                                    .actor = "test"});
  auto workspace_deny =
      ava::permissions::add_persistent_permission_rule(store, ava::permissions::PermissionRuleDraft{.scope = ava::permissions::PermissionRuleScope::Workspace,
                                                                                                    .action = ava::permissions::PermissionAction::Deny,
                                                                                                    .operation = ava::permissions::Operation::ReadFile,
                                                                                                    .mode = ava::permissions::PermissionRuleMode::Any,
                                                                                                    .tool_name = "",
                                                                                                    .target_path = outside,
                                                                                                    .command = "",
                                                                                                    .command_recipe_key = {},
                                                                                                    .recipe_display = {},
                                                                                                    .critical_acknowledged = false,
                                                                                                    .reason = "workspace deny",
                                                                                                    .actor = "test"});
  expect(global_allow && workspace_deny, "permission rule precedence test creates allow and deny rules");

  auto matched = ava::permissions::match_persistent_permission_rule(store, read_prompt(store, outside));
  expect(matched && *matched && (*matched)->rule_id == workspace_deny->rule_id && (*matched)->action == ava::permissions::PermissionAction::Deny,
         "persistent deny rules take precedence over matching allows");
}

ava::permissions::PermissionPrompt command_prompt(ava::permissions::PermissionRuleStore const& store, std::string command)
{
  return ava::permissions::PermissionPrompt{.permission_request_id = "permreq_command",
                                            .operation = ava::permissions::Operation::RunCommand,
                                            .mode = ava::agent::Mode::Build,
                                            .workspace_dir = store.workspace_dir,
                                            .target_path = {},
                                            .command = std::move(command),
                                            .tool_name = "bash",
                                            .reason = "command requires explicit approval",
                                            .risk = ava::permissions::PermissionRisk::High};
}

ava::permissions::CommandPermissionMetadata stable_command_metadata(std::string global_key, std::string workspace_key)
{
  ava::permissions::CommandPermissionMetadata metadata;
  metadata.level = ava::command::CommandLevel::Standard;
  metadata.family = ava::command::CommandFamily::CmakeBuild;
  metadata.containment_status = ava::permissions::CommandContainmentStatus::Available;
  metadata.backend_maximum_scope = ava::command::InteractiveScope::Workspace;
  metadata.global_recipe_key = std::move(global_key);
  metadata.workspace_recipe_key = std::move(workspace_key);
  metadata.recipe_display = "cmake-build: workspace:build";
  metadata.effective_allowed_scopes = {ava::command::InteractiveScope::Once, ava::command::InteractiveScope::Session,
                                       ava::command::InteractiveScope::Workspace};
  return metadata;
}

void test_schema_v2_recipe_rules_bind_scope_and_deny_precedence()
{
  auto const root = temp_root() / "permission-rules-stable-recipe";
  std::error_code remove_error;
  std::filesystem::remove_all(root, remove_error);
  auto const workspace_a = root / "workspace-a";
  auto const workspace_b = root / "workspace-b";
  std::filesystem::create_directories(workspace_a);
  std::filesystem::create_directories(workspace_b);
  auto const global_file = root / "config" / "ava" / "permission-rules.json";
  auto const store_a = ava::permissions::PermissionRuleStore{
      .global_rules_file = global_file, .workspace_rules_file = workspace_a / ".ava" / "permission-rules.json", .workspace_dir = workspace_a};
  auto const store_b = ava::permissions::PermissionRuleStore{
      .global_rules_file = global_file, .workspace_rules_file = workspace_b / ".ava" / "permission-rules.json", .workspace_dir = workspace_b};
  auto prompt_a = command_prompt(store_a, "cmake --build build");
  auto prompt_b = command_prompt(store_b, "cmake --build build");
  auto const global_key = std::string("sha256:ava-command-recipe-v1:aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa");
  auto const workspace_a_key = std::string("sha256:ava-command-workspace-recipe-v1:bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb");
  auto const workspace_b_key = std::string("sha256:ava-command-workspace-recipe-v1:cccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccc");
  prompt_a.command_metadata = stable_command_metadata(global_key, workspace_a_key);
  prompt_b.command_metadata = stable_command_metadata(global_key, workspace_b_key);

  auto workspace_allow =
      ava::permissions::add_persistent_permission_rule(store_a, ava::permissions::PermissionRuleDraft{.scope = ava::permissions::PermissionRuleScope::Workspace,
                                                                                                      .action = ava::permissions::PermissionAction::Allow,
                                                                                                      .operation = ava::permissions::Operation::RunCommand,
                                                                                                      .mode = ava::permissions::PermissionRuleMode::Build,
                                                                                                      .tool_name = "bash",
                                                                                                      .target_path = {},
                                                                                                      .command = {},
                                                                                                      .command_recipe_key = workspace_a_key,
                                                                                                      .recipe_display = "cmake-build: workspace:build",
                                                                                                      .critical_acknowledged = false,
                                                                                                      .reason = "remember exact workspace recipe",
                                                                                                      .actor = "test"});
  auto reloaded = ava::permissions::load_persistent_permission_rules(store_a);
  auto workspace_match = ava::permissions::match_persistent_permission_rule(store_a, prompt_a);
  auto cross_workspace_miss = ava::permissions::match_persistent_permission_rule(store_b, prompt_b);

  auto global_allow =
      ava::permissions::add_persistent_permission_rule(store_a, ava::permissions::PermissionRuleDraft{.scope = ava::permissions::PermissionRuleScope::Global,
                                                                                                      .action = ava::permissions::PermissionAction::Allow,
                                                                                                      .operation = ava::permissions::Operation::RunCommand,
                                                                                                      .mode = ava::permissions::PermissionRuleMode::Build,
                                                                                                      .tool_name = "bash",
                                                                                                      .target_path = {},
                                                                                                      .command = {},
                                                                                                      .command_recipe_key = global_key,
                                                                                                      .recipe_display = "cmake-build: workspace:build",
                                                                                                      .critical_acknowledged = false,
                                                                                                      .reason = "allow exact typed recipe across workspaces",
                                                                                                      .actor = "test"});
  auto global_match = ava::permissions::match_persistent_permission_rule(store_b, prompt_b);
  auto global_deny = ava::permissions::add_persistent_permission_rule(
      store_a, ava::permissions::PermissionRuleDraft{.scope = ava::permissions::PermissionRuleScope::Global,
                                                     .action = ava::permissions::PermissionAction::Deny,
                                                     .operation = ava::permissions::Operation::RunCommand,
                                                     .mode = ava::permissions::PermissionRuleMode::Build,
                                                     .tool_name = "bash",
                                                     .target_path = {},
                                                     .command = {},
                                                     .command_recipe_key = global_key,
                                                     .recipe_display = "cmake-build: workspace:build",
                                                     .critical_acknowledged = false,
                                                     .reason = "deny overrides standard auto-allow before execution",
                                                     .actor = "test"});
  auto denied = ava::permissions::match_persistent_permission_rule(store_b, prompt_b);

  expect(workspace_allow && reloaded && reloaded->size() == 1 && workspace_match && *workspace_match &&
             (*workspace_match)->rule_id == workspace_allow->rule_id && cross_workspace_miss && !*cross_workspace_miss && global_allow && global_match &&
             *global_match && (*global_match)->rule_id == global_allow->rule_id && global_deny && denied && *denied &&
             (*denied)->action == ava::permissions::PermissionAction::Deny && (*denied)->rule_id == global_deny->rule_id,
         "schema-v2 recipe rules persist by workspace key, global recipe rules span workspaces, and exact recipe denies win before Standard auto-allow");
}

void test_critical_command_allow_requires_explicit_acknowledgement()
{
  auto const root = temp_root() / "permission-rules-critical-ack";
  std::error_code remove_error;
  std::filesystem::remove_all(root, remove_error);
  auto const store = test_store(root);
  auto prompt = command_prompt(store, "python -c 'print(1)'");
  ava::permissions::CommandPermissionMetadata critical;
  critical.level = ava::command::CommandLevel::Critical;
  critical.backend_maximum_scope = ava::command::InteractiveScope::Once;
  prompt.command_metadata = critical;

  auto missing_ack =
      ava::permissions::add_persistent_permission_rule(store, ava::permissions::PermissionRuleDraft{.scope = ava::permissions::PermissionRuleScope::Workspace,
                                                                                                    .action = ava::permissions::PermissionAction::Allow,
                                                                                                    .operation = ava::permissions::Operation::RunCommand,
                                                                                                    .mode = ava::permissions::PermissionRuleMode::Build,
                                                                                                    .tool_name = "bash",
                                                                                                    .target_path = {},
                                                                                                    .command = prompt.command,
                                                                                                    .command_recipe_key = {},
                                                                                                    .recipe_display = {},
                                                                                                    .critical_acknowledged = false,
                                                                                                    .reason = "must fail without acknowledgement",
                                                                                                    .actor = "test"});
  auto acknowledged =
      ava::permissions::add_persistent_permission_rule(store, ava::permissions::PermissionRuleDraft{.scope = ava::permissions::PermissionRuleScope::Workspace,
                                                                                                    .action = ava::permissions::PermissionAction::Allow,
                                                                                                    .operation = ava::permissions::Operation::RunCommand,
                                                                                                    .mode = ava::permissions::PermissionRuleMode::Build,
                                                                                                    .tool_name = "bash",
                                                                                                    .target_path = {},
                                                                                                    .command = prompt.command,
                                                                                                    .command_recipe_key = {},
                                                                                                    .recipe_display = {},
                                                                                                    .critical_acknowledged = true,
                                                                                                    .reason = "advanced exact critical allow",
                                                                                                    .actor = "test"});
  auto matched = ava::permissions::match_persistent_permission_rule(store, prompt);
  critical.executor_identity_verified = false;
  critical.containment_status = ava::permissions::CommandContainmentStatus::UnverifiedDelegatedExecutor;
  prompt.command_metadata = critical;
  auto unverified = ava::permissions::match_persistent_permission_rule(store, prompt);

  expect(!missing_ack && acknowledged && matched && *matched && (*matched)->rule_id == acknowledged->rule_id && unverified && !*unverified,
         "only an explicit advanced exact Critical acknowledgement can allow a verified Critical command; unverified delegated commands remain one-shot");
}

void test_critical_acknowledgements_cannot_authorize_repository_build_or_test_text()
{
  auto const root = temp_root() / "permission-rules-critical-ack-repository-build";
  std::error_code remove_error;
  std::filesystem::remove_all(root, remove_error);
  auto const store = test_store(root);
  auto const command = std::string("cmake --build build");
  auto created =
      ava::permissions::add_persistent_permission_rule(store, ava::permissions::PermissionRuleDraft{.scope = ava::permissions::PermissionRuleScope::Workspace,
                                                                                                    .action = ava::permissions::PermissionAction::Allow,
                                                                                                    .operation = ava::permissions::Operation::RunCommand,
                                                                                                    .mode = ava::permissions::PermissionRuleMode::Build,
                                                                                                    .tool_name = "bash",
                                                                                                    .target_path = {},
                                                                                                    .command = command,
                                                                                                    .command_recipe_key = {},
                                                                                                    .recipe_display = {},
                                                                                                    .critical_acknowledged = true,
                                                                                                    .reason = "forged raw cmake acknowledgement",
                                                                                                    .actor = "test"});
  auto forged = ava::permissions::PersistentPermissionRule{.rule_id = "permrule_forged_cmake_ack",
                                                           .scope = ava::permissions::PermissionRuleScope::Global,
                                                           .workspace_dir = {},
                                                           .action = ava::permissions::PermissionAction::Allow,
                                                           .operation = ava::permissions::Operation::RunCommand,
                                                           .mode = ava::permissions::PermissionRuleMode::Build,
                                                           .tool_name = "bash",
                                                           .target_path = {},
                                                           .command = command,
                                                           .command_recipe_key = {},
                                                           .recipe_display = {},
                                                           .critical_acknowledged = true,
                                                           .schema_version = ava::permissions::kCurrentPermissionRulesSchemaVersion,
                                                           .reason = "forged persisted cmake acknowledgement",
                                                           .actor = "test",
                                                           .created_at = "2026-07-19T00:00:00Z"};
  write_file_with_mode(store.global_rules_file, std::string("{\"schema_version\":2,\"rules\":[") + ava::permissions::permission_rule_json(forged) + "]}",
                       S_IRUSR | S_IWUSR);
  auto prompt = command_prompt(store, command);
  ava::permissions::CommandPermissionMetadata critical;
  critical.level = ava::command::CommandLevel::Critical;
  critical.backend_maximum_scope = ava::command::InteractiveScope::Once;
  prompt.command_metadata = critical;
  auto matched = ava::permissions::match_persistent_permission_rule(store, prompt);
  auto ctest_created =
      ava::permissions::add_persistent_permission_rule(store, ava::permissions::PermissionRuleDraft{.scope = ava::permissions::PermissionRuleScope::Workspace,
                                                                                                    .action = ava::permissions::PermissionAction::Allow,
                                                                                                    .operation = ava::permissions::Operation::RunCommand,
                                                                                                    .mode = ava::permissions::PermissionRuleMode::Build,
                                                                                                    .tool_name = "bash",
                                                                                                    .target_path = {},
                                                                                                    .command = "ctest --test-dir build",
                                                                                                    .command_recipe_key = {},
                                                                                                    .recipe_display = {},
                                                                                                    .critical_acknowledged = true,
                                                                                                    .reason = "forged raw ctest acknowledgement",
                                                                                                    .actor = "test"});
  auto forged_ctest = forged;
  forged_ctest.rule_id = "permrule_forged_ctest_ack";
  forged_ctest.command = "ctest --test-dir build";
  forged_ctest.reason = "forged persisted ctest acknowledgement";
  write_file_with_mode(store.global_rules_file, std::string("{\"schema_version\":2,\"rules\":[") + ava::permissions::permission_rule_json(forged_ctest) + "]}",
                       S_IRUSR | S_IWUSR);
  auto ctest_prompt = command_prompt(store, forged_ctest.command);
  ctest_prompt.command_metadata = critical;
  auto ctest_matched = ava::permissions::match_persistent_permission_rule(store, ctest_prompt);

  expect(!created && created.error().category() == ava::core::ErrorCategory::InvalidArgument && !matched && !ctest_created &&
             ctest_created.error().category() == ava::core::ErrorCategory::InvalidArgument && !ctest_matched,
         "raw critical acknowledgements for repository-controlled cmake build and ctest text are rejected both on creation and during persisted-rule matching");
}

void test_permission_rule_precedence_prefers_specific_same_scope_rules()
{
  auto const root = temp_root() / "permission-rules-specificity";
  std::error_code remove_error;
  std::filesystem::remove_all(root, remove_error);
  auto const store = test_store(root);
  auto const outside = root / "outside.txt";

  auto broad_allow =
      ava::permissions::add_persistent_permission_rule(store, ava::permissions::PermissionRuleDraft{.scope = ava::permissions::PermissionRuleScope::Workspace,
                                                                                                    .action = ava::permissions::PermissionAction::Allow,
                                                                                                    .operation = ava::permissions::Operation::ReadFile,
                                                                                                    .mode = ava::permissions::PermissionRuleMode::Any,
                                                                                                    .tool_name = "",
                                                                                                    .target_path = outside,
                                                                                                    .command = "",
                                                                                                    .command_recipe_key = {},
                                                                                                    .recipe_display = {},
                                                                                                    .critical_acknowledged = false,
                                                                                                    .reason = "allow this path",
                                                                                                    .actor = "test"});
  auto specific_deny =
      ava::permissions::add_persistent_permission_rule(store, ava::permissions::PermissionRuleDraft{.scope = ava::permissions::PermissionRuleScope::Workspace,
                                                                                                    .action = ava::permissions::PermissionAction::Deny,
                                                                                                    .operation = ava::permissions::Operation::ReadFile,
                                                                                                    .mode = ava::permissions::PermissionRuleMode::Any,
                                                                                                    .tool_name = "read_file",
                                                                                                    .target_path = outside,
                                                                                                    .command = "",
                                                                                                    .command_recipe_key = {},
                                                                                                    .recipe_display = {},
                                                                                                    .critical_acknowledged = false,
                                                                                                    .reason = "deny this tool/path pair",
                                                                                                    .actor = "test"});
  expect(broad_allow && specific_deny, "permission rule specificity test creates broad and specific rules");

  auto matched = ava::permissions::match_persistent_permission_rule(store, read_prompt(store, outside));
  expect(matched && *matched && (*matched)->rule_id == specific_deny->rule_id && (*matched)->action == ava::permissions::PermissionAction::Deny,
         "persistent permission rules prefer more specific same-scope matches");
}

void test_permission_rule_matches_command_operations_without_path_targets()
{
  auto const root = temp_root() / "permission-rules-command-operation";
  std::error_code remove_error;
  std::filesystem::remove_all(root, remove_error);
  auto const store = test_store(root);

  auto allow_echo =
      ava::permissions::add_persistent_permission_rule(store, ava::permissions::PermissionRuleDraft{.scope = ava::permissions::PermissionRuleScope::Workspace,
                                                                                                    .action = ava::permissions::PermissionAction::Allow,
                                                                                                    .operation = ava::permissions::Operation::RunCommand,
                                                                                                    .mode = ava::permissions::PermissionRuleMode::Build,
                                                                                                    .tool_name = "bash",
                                                                                                    .target_path = {},
                                                                                                    .command = "echo safe",
                                                                                                    .command_recipe_key = {},
                                                                                                    .recipe_display = {},
                                                                                                    .critical_acknowledged = false,
                                                                                                    .reason = "allow exact verification command",
                                                                                                    .actor = "test"});
  expect(!allow_echo && allow_echo.error().category() == ava::core::ErrorCategory::InvalidArgument,
         "schema-v2 raw command Allows are rejected without a stable recipe key or explicit critical acknowledgement");

  // Schema-v1 command Allows predate sealed recipe identities and remain
  // non-authoritative; exact Denies remain authoritative.
  auto matched = ava::permissions::match_persistent_permission_rule(store, command_prompt(store, "echo safe"));
  expect(matched && !*matched, "legacy v1 command Allow rules are ignored for one-shot command plans");

  auto deny_echo =
      ava::permissions::add_persistent_permission_rule(store, ava::permissions::PermissionRuleDraft{.scope = ava::permissions::PermissionRuleScope::Workspace,
                                                                                                    .action = ava::permissions::PermissionAction::Deny,
                                                                                                    .operation = ava::permissions::Operation::RunCommand,
                                                                                                    .mode = ava::permissions::PermissionRuleMode::Build,
                                                                                                    .tool_name = "bash",
                                                                                                    .target_path = {},
                                                                                                    .command = "echo safe",
                                                                                                    .command_recipe_key = {},
                                                                                                    .recipe_display = {},
                                                                                                    .critical_acknowledged = false,
                                                                                                    .reason = "deny exact verification command",
                                                                                                    .actor = "test"});
  expect(deny_echo.has_value(), "permission rule storage accepts exact command deny rules");
  auto matched_deny = ava::permissions::match_persistent_permission_rule(store, command_prompt(store, "echo safe"));
  expect(matched_deny && *matched_deny && (*matched_deny)->rule_id == deny_echo->rule_id && (*matched_deny)->action == ava::permissions::PermissionAction::Deny,
         "legacy v1 command Deny rules remain authoritative for one-shot command plans");

  auto unmatched = ava::permissions::match_persistent_permission_rule(store, command_prompt(store, "echo unsafe"));
  expect(unmatched && !*unmatched, "non-matching command prompts produce no persistent rule match");
}

void test_repository_build_test_persistent_allows_are_rejected_but_denies_win()
{
  auto const root = temp_root() / "permission-rules-build-test";
  std::error_code remove_error;
  std::filesystem::remove_all(root, remove_error);
  auto const store = test_store(root);

  for (auto const& command : std::array{"ctest --test-dir build", "cmake --build=build", "cmake --build-and-test source build --build-generator Ninja",
                                        "cmake --workflow --preset=ci", "/usr/bin/ctest --test-dir build", "/usr/bin/cmake --build build"})
  {
    auto const prompt = command_prompt(store, command);
    auto persistent_allow =
        ava::permissions::add_persistent_permission_rule(store, ava::permissions::PermissionRuleDraft{.scope = ava::permissions::PermissionRuleScope::Workspace,
                                                                                                      .action = ava::permissions::PermissionAction::Allow,
                                                                                                      .operation = ava::permissions::Operation::RunCommand,
                                                                                                      .mode = ava::permissions::PermissionRuleMode::Build,
                                                                                                      .tool_name = "bash",
                                                                                                      .target_path = {},
                                                                                                      .command = prompt.command,
                                                                                                      .command_recipe_key = {},
                                                                                                      .recipe_display = {},
                                                                                                      .critical_acknowledged = false,
                                                                                                      .reason = "never persist build test approval",
                                                                                                      .actor = "test"});
    expect(!persistent_allow && persistent_allow.error().category() == ava::core::ErrorCategory::InvalidArgument,
           "persistent rules reject repository build/test allow command shapes");

    ava::permissions::PersistentPermissionRule const legacy_allow{.rule_id = "permrule_legacy_build_test",
                                                                  .scope = ava::permissions::PermissionRuleScope::Global,
                                                                  .workspace_dir = {},
                                                                  .action = ava::permissions::PermissionAction::Allow,
                                                                  .operation = ava::permissions::Operation::RunCommand,
                                                                  .mode = ava::permissions::PermissionRuleMode::Build,
                                                                  .tool_name = "bash",
                                                                  .target_path = {},
                                                                  .command = prompt.command,
                                                                  .command_recipe_key = {},
                                                                  .recipe_display = {},
                                                                  .critical_acknowledged = false,
                                                                  .reason = "legacy repository test allow",
                                                                  .actor = "test",
                                                                  .created_at = "2026-07-11T00:00:00Z"};
    write_file_with_mode(store.global_rules_file,
                         std::string("{\"schema_version\":1,\"rules\":[") + ava::permissions::permission_rule_json(legacy_allow) + "]}", S_IRUSR | S_IWUSR);
    auto legacy_match = ava::permissions::match_persistent_permission_rule(store, prompt);
    expect(legacy_match && !*legacy_match, "legacy persistent repository build/test allows are ignored rather than auto-executed");
  }

  auto const prompt = command_prompt(store, "ctest --test-dir build");
  auto persistent_deny =
      ava::permissions::add_persistent_permission_rule(store, ava::permissions::PermissionRuleDraft{.scope = ava::permissions::PermissionRuleScope::Workspace,
                                                                                                    .action = ava::permissions::PermissionAction::Deny,
                                                                                                    .operation = ava::permissions::Operation::RunCommand,
                                                                                                    .mode = ava::permissions::PermissionRuleMode::Build,
                                                                                                    .tool_name = "bash",
                                                                                                    .target_path = {},
                                                                                                    .command = prompt.command,
                                                                                                    .command_recipe_key = {},
                                                                                                    .recipe_display = {},
                                                                                                    .critical_acknowledged = false,
                                                                                                    .reason = "deny repository test",
                                                                                                    .actor = "test"});
  auto matched = ava::permissions::match_persistent_permission_rule(store, prompt);
  expect(persistent_deny && matched && *matched && (*matched)->action == ava::permissions::PermissionAction::Deny,
         "persistent deny rules continue to override repository build and test approvals");
}

void test_legacy_command_allows_do_not_authorize_sealed_critical_or_unverified_plans()
{
  auto const root = temp_root() / "permission-rules-sealed-command-v1";
  std::error_code remove_error;
  std::filesystem::remove_all(root, remove_error);
  auto const store = test_store(root);
  auto prompt = command_prompt(store, "python -c 'print(1)'");
  ava::permissions::PersistentPermissionRule const legacy_allow{.rule_id = "permrule_legacy_command_allow",
                                                                .scope = ava::permissions::PermissionRuleScope::Global,
                                                                .workspace_dir = {},
                                                                .action = ava::permissions::PermissionAction::Allow,
                                                                .operation = ava::permissions::Operation::RunCommand,
                                                                .mode = ava::permissions::PermissionRuleMode::Build,
                                                                .tool_name = "bash",
                                                                .target_path = {},
                                                                .command = prompt.command,
                                                                .command_recipe_key = {},
                                                                .recipe_display = {},
                                                                .critical_acknowledged = false,
                                                                .reason = "legacy exact command allow",
                                                                .actor = "test",
                                                                .created_at = "2026-07-19T00:00:00Z"};
  ava::permissions::PersistentPermissionRule legacy_deny = legacy_allow;
  legacy_deny.rule_id = "permrule_legacy_command_deny";
  legacy_deny.action = ava::permissions::PermissionAction::Deny;
  legacy_deny.reason = "legacy exact command deny";

  auto verify_legacy_allow_blocked = [&](ava::permissions::CommandPermissionMetadata metadata, std::string_view label) {
    prompt.command_metadata = std::move(metadata);
    write_file_with_mode(store.global_rules_file,
                         std::string("{\"schema_version\":1,\"rules\":[") + ava::permissions::permission_rule_json(legacy_allow) + "]}", S_IRUSR | S_IWUSR);
    auto ignored_allow = ava::permissions::match_persistent_permission_rule(store, prompt);
    expect(ignored_allow && !*ignored_allow, std::string("legacy v1 exact Allow cannot authorize ") + std::string(label) + " sealed command plans");

    write_file_with_mode(store.global_rules_file,
                         std::string("{\"schema_version\":1,\"rules\":[") + ava::permissions::permission_rule_json(legacy_allow) + "," +
                             ava::permissions::permission_rule_json(legacy_deny) + "]}",
                         S_IRUSR | S_IWUSR);
    auto matched_deny = ava::permissions::match_persistent_permission_rule(store, prompt);
    expect(matched_deny && *matched_deny && (*matched_deny)->rule_id == legacy_deny.rule_id,
           std::string("legacy v1 exact Deny remains authoritative for ") + std::string(label) + " sealed command plans");
  };

  ava::permissions::CommandPermissionMetadata critical;
  critical.level = ava::command::CommandLevel::Critical;
  critical.backend_maximum_scope = ava::command::InteractiveScope::Once;
  verify_legacy_allow_blocked(critical, "critical");

  ava::permissions::CommandPermissionMetadata unverified;
  unverified.level = ava::command::CommandLevel::Critical;
  unverified.backend_maximum_scope = ava::command::InteractiveScope::Once;
  unverified.executor_identity_verified = false;
  unverified.containment_status = ava::permissions::CommandContainmentStatus::UnverifiedDelegatedExecutor;
  verify_legacy_allow_blocked(unverified, "unverified delegated");
}

void test_permission_rule_storage_fail_closed()
{
  auto const root = temp_root() / "permission-rules-corrupt";
  std::error_code remove_error;
  std::filesystem::remove_all(root, remove_error);
  auto const store = test_store(root);
  write_file_with_mode(store.global_rules_file, "{\"schema_version\":3,\"rules\":[]}", S_IRUSR | S_IWUSR);

  auto loaded = ava::permissions::load_persistent_permission_rules(store);
  expect(!loaded && loaded.error().message() == "unsupported permission rules schema_version", "permission rule storage rejects unsupported schema versions");

  bool fallback_called = false;
  auto resolver = ava::permissions::build_persistent_permission_rule_resolver(
      store, [&](ava::permissions::PermissionPrompt const&) -> ava::core::Result<ava::permissions::PermissionResolutionDecision> {
        fallback_called = true;
        return ava::permissions::PermissionResolution::Allow;
      });
  auto decision = resolver(read_prompt(store, root / "outside.txt"));
  expect(decision && decision->resolution == ava::permissions::PermissionResolution::Deny && decision->resolution_source == "persistent_rule_error" &&
             decision->authoritative && !fallback_called,
         "malformed persistent rule storage fails closed before resolver fallback");

  write_file_with_mode(store.global_rules_file, "{\"schema_version\":2,\"rules\":[],\"unexpected\":true}", S_IRUSR | S_IWUSR);
  auto unknown_v2_member = ava::permissions::load_persistent_permission_rules(store);
  expect(!unknown_v2_member && unknown_v2_member.error().message() == "schema-v2 permission rules file has unsupported member",
         "strict bounded schema-v2 rule storage rejects unknown members rather than silently broadening policy");
}

void test_permission_rule_broad_permissions_rejected()
{
  auto const root = temp_root() / "permission-rules-broad";
  std::error_code remove_error;
  std::filesystem::remove_all(root, remove_error);
  auto const store = test_store(root);
  write_file_with_mode(store.global_rules_file, "{\"schema_version\":1,\"rules\":[]}", S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH);

  auto loaded = ava::permissions::load_persistent_permission_rules(store);
  expect(!loaded && loaded.error().category() == ava::core::ErrorCategory::PermissionDenied, "permission rule storage rejects group/world-readable rule files");
}

void test_permission_rule_workspace_legacy_path_is_not_enforceable()
{
  auto const root = temp_root() / "permission-rules-legacy-workspace-path";
  std::error_code remove_error;
  std::filesystem::remove_all(root, remove_error);
  auto const store = test_store(root);
  auto const outside = root / "outside.txt";
  ava::permissions::PersistentPermissionRule const forged{.rule_id = "permrule_forged",
                                                          .scope = ava::permissions::PermissionRuleScope::Workspace,
                                                          .workspace_dir = store.workspace_dir,
                                                          .action = ava::permissions::PermissionAction::Allow,
                                                          .operation = ava::permissions::Operation::ReadFile,
                                                          .mode = ava::permissions::PermissionRuleMode::Any,
                                                          .tool_name = "",
                                                          .target_path = outside,
                                                          .command = "",
                                                          .command_recipe_key = {},
                                                          .recipe_display = {},
                                                          .critical_acknowledged = false,
                                                          .reason = "forged workspace rule",
                                                          .actor = "model",
                                                          .created_at = "2026-05-07T00:00:00Z"};
  write_file_with_mode(store.workspace_rules_file, std::string("{\"schema_version\":1,\"rules\":[") + ava::permissions::permission_rule_json(forged) + "]}",
                       S_IRUSR | S_IWUSR);

  auto loaded = ava::permissions::load_persistent_permission_rules(store);
  auto matched = ava::permissions::match_persistent_permission_rule(store, read_prompt(store, outside));
  expect(loaded && loaded->empty() && matched && !*matched, "model-writable workspace permission-rules.json is ignored by enforceable rule loading");
}

void test_file_tools_reject_workspace_permission_rule_writes()
{
  auto const root = temp_root() / "permission-rules-file-tool-guard";
  std::error_code remove_error;
  std::filesystem::remove_all(root, remove_error);
  auto const workspace = root / "workspace";
  std::filesystem::create_directories(workspace / ".ava");

  ava::tools::ToolContext context;
  context.workspace_dir = workspace;
  auto written =
      ava::tools::write_file(context, workspace / ".ava" / "permission-rules.json", "{}", ava::tools::WriteOptions{.permission_already_checked = true});
  expect(!written && written.error().category() == ava::core::ErrorCategory::PermissionDenied,
         "normal file tools cannot write legacy workspace permission rule files");
}

void test_file_tools_reject_enforceable_permission_rule_writes()
{
  auto const root = temp_root() / "permission-rules-enforceable-file-tool-guard";
  std::error_code remove_error;
  std::filesystem::remove_all(root, remove_error);
  auto const workspace = root / "workspace";
  std::filesystem::create_directories(workspace);
  auto const store = ava::permissions::PermissionRuleStore{.global_rules_file = workspace / ".config" / "ava" / "permission-rules.json",
                                                           .workspace_rules_file = workspace / ".ava" / "permission-rules.json",
                                                           .workspace_dir = workspace};
  auto const outside = root / "outside.txt";

  auto added =
      ava::permissions::add_persistent_permission_rule(store, ava::permissions::PermissionRuleDraft{.scope = ava::permissions::PermissionRuleScope::Workspace,
                                                                                                    .action = ava::permissions::PermissionAction::Allow,
                                                                                                    .operation = ava::permissions::Operation::ReadFile,
                                                                                                    .mode = ava::permissions::PermissionRuleMode::Any,
                                                                                                    .tool_name = "",
                                                                                                    .target_path = outside,
                                                                                                    .command = "",
                                                                                                    .command_recipe_key = {},
                                                                                                    .recipe_display = {},
                                                                                                    .critical_acknowledged = false,
                                                                                                    .reason = "register protected rule paths",
                                                                                                    .actor = "test"});
  expect(added.has_value(),
         added ? "permission rule storage registers protected paths" : "permission rule storage registers protected paths: " + added.error().format());

  ava::tools::ToolContext context;
  context.workspace_dir = workspace;
  context.mcp_global_config_file = store.global_rules_file.parent_path() / "mcp.json";
  auto global_write = ava::tools::write_file(context, store.global_rules_file, "{}", ava::tools::WriteOptions{.permission_already_checked = true});
  expect(!global_write && global_write.error().category() == ava::core::ErrorCategory::PermissionDenied,
         "normal file tools cannot write enforceable global permission rule files inside the workspace");

  auto const workspace_rules_file = ava::permissions::enforceable_permission_rules_file(store, ava::permissions::PermissionRuleScope::Workspace);
  auto workspace_edit = ava::tools::edit_file(context, workspace_rules_file, "\"rules\"", "\"rules\"");
  expect(!workspace_edit && workspace_edit.error().category() == ava::core::ErrorCategory::PermissionDenied,
         "normal file tools cannot edit workspace-keyed enforceable permission rule files");

  auto const config_link = workspace / "config-link";
  std::error_code symlink_error;
  std::filesystem::create_directory_symlink(store.global_rules_file.parent_path(), config_link, symlink_error);
  if (!symlink_error)
  {
    auto alias_write =
        ava::tools::write_file(context, config_link / "permission-rules.json", "{}", ava::tools::WriteOptions{.permission_already_checked = true});
    expect(!alias_write && alias_write.error().category() == ava::core::ErrorCategory::PermissionDenied,
           "normal file tools use canonical comparison for enforceable permission rule file aliases");
  }
}

void test_file_tools_reject_enforceable_permission_rule_writes_before_registration()
{
  auto const root = temp_root() / "permission-rules-unregistered-file-tool-guard";
  std::error_code remove_error;
  std::filesystem::remove_all(root, remove_error);
  auto const workspace = root / "workspace";
  std::filesystem::create_directories(workspace);
  auto const config_dir = workspace / ".config" / "ava";
  auto const store = ava::permissions::PermissionRuleStore{.global_rules_file = config_dir / "permission-rules.json",
                                                           .workspace_rules_file = workspace / ".ava" / "permission-rules.json",
                                                           .workspace_dir = workspace};

  ava::tools::ToolContext context;
  context.workspace_dir = workspace;
  context.mcp_global_config_file = config_dir / "mcp.json";

  auto global_write = ava::tools::write_file(context, store.global_rules_file, "{}", ava::tools::WriteOptions{.permission_already_checked = true});
  expect(!global_write && global_write.error().category() == ava::core::ErrorCategory::PermissionDenied,
         "normal file tools reject global permission rule writes before rule store registration");

  auto const workspace_rules_file = ava::permissions::enforceable_permission_rules_file(store, ava::permissions::PermissionRuleScope::Workspace);
  auto workspace_write = ava::tools::write_file(context, workspace_rules_file, "{}", ava::tools::WriteOptions{.permission_already_checked = true});
  expect(!workspace_write && workspace_write.error().category() == ava::core::ErrorCategory::PermissionDenied,
         "normal file tools reject workspace-keyed permission rule writes before rule store registration");
}

void test_registered_permission_rule_paths_protect_agent_loop_context()
{
  auto const root = temp_root() / "permission-rules-agent-loop-file-tool-guard";
  std::error_code remove_error;
  std::filesystem::remove_all(root, remove_error);
  auto const workspace = root / "workspace";
  std::filesystem::create_directories(workspace);
  auto const store = ava::permissions::PermissionRuleStore{.global_rules_file = workspace / ".config" / "ava" / "permission-rules.json",
                                                           .workspace_rules_file = workspace / ".ava" / "permission-rules.json",
                                                           .workspace_dir = workspace};

  ava::permissions::register_enforceable_permission_rule_files(store);

  ava::tools::ToolContext context;
  context.workspace_dir = workspace;
  auto global_write = ava::tools::write_file(context, store.global_rules_file, "{}", ava::tools::WriteOptions{.permission_already_checked = true});
  expect(!global_write && global_write.error().category() == ava::core::ErrorCategory::PermissionDenied,
         "registered enforceable permission rule paths protect agent-loop-style tool contexts");
}

}  // namespace

void run_permission_rules_tests()
{
  test_permission_rule_storage_add_list_remove();
  test_legacy_command_allows_are_removed_one_at_a_time();
  test_permission_rule_precedence_denies_win();
  test_permission_rule_precedence_prefers_specific_same_scope_rules();
  test_schema_v2_recipe_rules_bind_scope_and_deny_precedence();
  test_critical_command_allow_requires_explicit_acknowledgement();
  test_critical_acknowledgements_cannot_authorize_repository_build_or_test_text();
  test_permission_rule_matches_command_operations_without_path_targets();
  test_repository_build_test_persistent_allows_are_rejected_but_denies_win();
  test_legacy_command_allows_do_not_authorize_sealed_critical_or_unverified_plans();
  test_permission_rule_storage_fail_closed();
  test_permission_rule_broad_permissions_rejected();
  test_permission_rule_workspace_legacy_path_is_not_enforceable();
  test_file_tools_reject_workspace_permission_rule_writes();
  test_file_tools_reject_enforceable_permission_rule_writes();
  test_file_tools_reject_enforceable_permission_rule_writes_before_registration();
  test_registered_permission_rule_paths_protect_agent_loop_context();
}
