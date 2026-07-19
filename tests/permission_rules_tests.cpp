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
                                                                                                    .reason = "allow exact verification command",
                                                                                                    .actor = "test"});
  expect(allow_echo.has_value(), "permission rule storage accepts exact command rules for non-file operations");

  // Until the separate stable recipe identity/store exists, every planned
  // command is one-shot only. Legacy v1 RunCommand Allows are ignored; only
  // authoritative Denies are retained.
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
  write_file_with_mode(store.global_rules_file, "{\"schema_version\":2,\"rules\":[]}", S_IRUSR | S_IWUSR);

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
  test_permission_rule_precedence_denies_win();
  test_permission_rule_precedence_prefers_specific_same_scope_rules();
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
