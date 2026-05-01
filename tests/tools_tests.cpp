#include <sys/stat.h>
#include <unistd.h>

#include <algorithm>
#include <chrono>
#include <climits>
#include <cstdlib>
#include <cwchar>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <optional>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

#include "ava/agent/agent_loop.h"
#include "ava/agent/mode.h"
#include "ava/agent/tool_dispatcher.h"
#include "ava/app/commands.h"
#include "ava/app/events.h"
#include "ava/app/headless_policy.h"
#include "ava/app/print_mode.h"
#include "ava/app/rpc_mode.h"
#include "ava/app/runtime.h"
#include "ava/config/auth.h"
#include "ava/config/model_config.h"
#include "ava/config/openai_oauth.h"
#include "ava/config/prompt_config.h"
#include "ava/config/xdg_paths.h"
#include "ava/context/context_loader.h"
#include "ava/core/ids.h"
#include "ava/core/json.h"
#include "ava/permissions/permission.h"
#include "ava/provider/openai_provider.h"
#include "ava/session/compaction.h"
#include "ava/session/export.h"
#include "ava/session/session_store.h"
#include "ava/tools/bash_tool.h"
#include "ava/tools/file_tools.h"
#include "ava/tools/search_tools.h"
#include "ava/tui/composer.h"
#include "ava/tui/terminal.h"
#include "tests/support/fake_transport.h"

#include "tests/support/test_harness.h"

namespace {

void test_file_tools() {
  std::error_code remove_error;
  std::filesystem::remove_all(temp_root(), remove_error);
  std::filesystem::create_directories(temp_root());

  const auto workspace = temp_root() / "workspace";
  std::filesystem::create_directories(workspace / "docs");
  const auto source_path = workspace / "src.txt";
  const ava::tools::ToolContext build_context{.workspace_dir = workspace, .mode = ava::agent::Mode::Build};
  const ava::tools::ToolContext plan_context{.workspace_dir = workspace, .mode = ava::agent::Mode::Plan};
  const auto has_write_temp = [](const std::filesystem::path& directory) {
    std::error_code iter_error;
    for (std::filesystem::directory_iterator it(directory, iter_error), end; !iter_error && it != end;
         it.increment(iter_error)) {
      if (it->path().filename().string().find(".ava-write-") != std::string::npos) return true;
    }
    return false;
  };
  const auto permission_bits = [](const std::filesystem::path& permission_path) {
    constexpr auto mask =
        std::filesystem::perms::owner_all | std::filesystem::perms::group_all | std::filesystem::perms::others_all;
    std::error_code status_error;
    return std::filesystem::status(permission_path, status_error).permissions() & mask;
  };

  auto write = ava::tools::write_file(build_context, source_path, "hello world");
  expect(write.has_value(), "write_file writes in build mode");
  expect(write && permission_bits(source_path) == (std::filesystem::perms::owner_read |
                                                   std::filesystem::perms::owner_write),
         "write_file creates new files with 0600 permissions");

  auto read = ava::tools::read_file(build_context, source_path, ava::tools::ReadOptions{.max_bytes = 5});
  expect(read.has_value(), "read_file reads content");
  if (read) {
    expect(read->content == "hello", "read_file truncates head");
    expect(read->truncated, "read_file reports truncation");
  }

  auto edit = ava::tools::edit_file(build_context, source_path, "world", "ava");
  expect(edit.has_value(), "edit_file edits unique text");

  auto edited = ava::tools::read_file(build_context, source_path);
  expect(edited && edited->content == "hello ava", "edit_file result is persisted");

  const auto atomic_path = workspace / "atomic.txt";
  {
    std::ofstream atomic_file(atomic_path, std::ios::binary | std::ios::trunc);
    atomic_file << "original content";
  }
  auto atomic_write = ava::tools::write_file(build_context, atomic_path, "replacement content");
  auto atomic_read = ava::tools::read_file(build_context, atomic_path);
  expect(atomic_write && atomic_read && atomic_read->content == "replacement content" && !has_write_temp(workspace),
         "write_file atomically replaces existing content and cleans the staging file on success");

  const auto protected_path = workspace / "protected.txt";
  {
    std::ofstream protected_file(protected_path, std::ios::binary | std::ios::trunc);
    protected_file << "private original";
  }
  std::error_code chmod_error;
  std::filesystem::permissions(protected_path, std::filesystem::perms::owner_read | std::filesystem::perms::owner_write,
                               std::filesystem::perm_options::replace, chmod_error);
  expect(!chmod_error, "test can set private file permissions");
  auto protected_write = ava::tools::write_file(build_context, protected_path, "private replacement");
  auto protected_read = ava::tools::read_file(build_context, protected_path);
  expect(
      protected_write && protected_read && protected_read->content == "private replacement" &&
          permission_bits(protected_path) == (std::filesystem::perms::owner_read | std::filesystem::perms::owner_write),
      "write_file preserves 0600 permissions when overwriting a file");

  const auto edit_private_path = workspace / "edit-private.txt";
  {
    std::ofstream edit_private_file(edit_private_path, std::ios::binary | std::ios::trunc);
    edit_private_file << "alpha beta";
  }
  chmod_error.clear();
  std::filesystem::permissions(edit_private_path,
                               std::filesystem::perms::owner_read | std::filesystem::perms::owner_write,
                               std::filesystem::perm_options::replace, chmod_error);
  expect(!chmod_error, "test can set private edit file permissions");
  auto edit_private = ava::tools::edit_file(build_context, edit_private_path, "beta", "gamma");
  auto edit_private_read = ava::tools::read_file(build_context, edit_private_path);
  expect(edit_private && edit_private_read && edit_private_read->content == "alpha gamma" &&
             permission_bits(edit_private_path) ==
                 (std::filesystem::perms::owner_read | std::filesystem::perms::owner_write),
         "edit_file preserves 0600 permissions through atomic write_file");

  const auto audit_edit_path = workspace / "audit-edit.txt";
  {
    std::ofstream audit_edit_file(audit_edit_path, std::ios::binary | std::ios::trunc);
    audit_edit_file << "read then edit";
  }
  std::vector<ava::tools::PermissionAuditEvent> edit_audits;
  const ava::tools::ToolContext audit_edit_context{
      .workspace_dir = workspace,
      .mode = ava::agent::Mode::Build,
      .permission_audit_sink = [&edit_audits](const ava::tools::PermissionAuditEvent& event) -> ava::core::VoidResult {
        edit_audits.push_back(event);
        return {};
      }};
  auto audited_edit = ava::tools::edit_file(audit_edit_context, audit_edit_path, "then", "before");
  expect(audited_edit && edit_audits.size() == 2 &&
             edit_audits[0].operation == ava::permissions::Operation::ReadFile &&
             edit_audits[1].operation == ava::permissions::Operation::EditFile,
         "edit_file audits read permission before edit permission");

  const auto rename_failure_path = workspace / "rename-failure-target";
  std::filesystem::create_directories(rename_failure_path);
  {
    std::ofstream sentinel(rename_failure_path / "sentinel.txt", std::ios::binary | std::ios::trunc);
    sentinel << "original sentinel";
  }
  auto failed_write = ava::tools::write_file(build_context, rename_failure_path, "replacement");
  auto sentinel_read = ava::tools::read_file(build_context, rename_failure_path / "sentinel.txt");
  expect(!failed_write && sentinel_read && sentinel_read->content == "original sentinel" && !has_write_temp(workspace),
         "write_file cleans the staging file and leaves existing data unchanged when commit rename fails");

  auto denied = ava::tools::write_file(plan_context, workspace / "main.cpp", "int main() {}\n");
  expect(!denied, "plan mode source write is denied");

  auto denied_edit = ava::tools::edit_file(build_context, workspace / ".env", "secret", "other");
  expect(!denied_edit, "edit_file checks read permission before reading");

  auto plan_denied_edit = ava::tools::edit_file(plan_context, workspace / "missing.cpp", "old", "new");
  expect(!plan_denied_edit && plan_denied_edit.error().category() == ava::core::ErrorCategory::PermissionDenied,
         "edit_file checks edit permission before reading target content");

  {
    std::ofstream secret_file(workspace / ".env", std::ios::binary | std::ios::trunc);
    secret_file << "secret";
  }
  auto direct_secret = ava::tools::read_file(build_context, workspace / ".env");
  expect(!direct_secret, "read_file denies direct .env reads");
  auto external_ssh_secret = ava::permissions::decide(ava::permissions::PermissionRequest{
      .operation = ava::permissions::Operation::ReadFile,
      .mode = ava::agent::Mode::Build,
      .workspace_dir = workspace,
      .target_path = "/home/user/.ssh/id_rsa",
      .command = "",
  });
  expect(external_ssh_secret.action == ava::permissions::PermissionAction::Deny,
         "external ssh key reads deny before outside-workspace ask");
  auto external_npmrc_secret = ava::permissions::decide(ava::permissions::PermissionRequest{
      .operation = ava::permissions::Operation::EditFile,
      .mode = ava::agent::Mode::Build,
      .workspace_dir = workspace,
      .target_path = "/home/user/.npmrc",
      .command = "",
  });
  expect(external_npmrc_secret.action == ava::permissions::PermissionAction::Deny,
         "external npm credential edits deny before outside-workspace ask");
  auto external_ava_auth = ava::permissions::decide(ava::permissions::PermissionRequest{
      .operation = ava::permissions::Operation::ReadFile,
      .mode = ava::agent::Mode::Build,
      .workspace_dir = workspace,
      .target_path = "/home/user/.config/ava/auth.json",
      .command = "",
  });
  expect(external_ava_auth.action == ava::permissions::PermissionAction::Deny,
         "external ava auth reads deny before outside-workspace ask");
  std::error_code symlink_error;
  const auto secret_link = workspace / "safe.txt";
  std::filesystem::create_symlink(workspace / ".env", secret_link, symlink_error);
  if (!symlink_error) {
    auto linked_secret = ava::tools::read_file(build_context, secret_link);
    expect(!linked_secret, "read_file denies symlink to secret file");
  }

  const auto source_link = workspace / "docs" / "plan-link.md";
  symlink_error.clear();
  std::filesystem::create_symlink(source_path, source_link, symlink_error);
  if (!symlink_error) {
    auto linked_source_edit = ava::tools::write_file(plan_context, source_link, "bad");
    expect(!linked_source_edit, "plan mode denies symlink edit to source file");
  }

  auto plan_doc = ava::tools::write_file(plan_context, workspace / "docs" / "plan.md", "# Plan\n");
  expect(plan_doc.has_value(), "plan mode markdown plan write is allowed");

  const auto large_path = workspace / "large.txt";
  {
    std::ofstream large(large_path, std::ios::binary | std::ios::trunc);
    large << std::string(8192, 'x');
  }
  auto large_read = ava::tools::read_file(build_context, large_path, ava::tools::ReadOptions{.max_bytes = 16});
  expect(large_read && large_read->content.size() == 16 && large_read->total_bytes == 8192,
         "read_file bounds output while counting bytes");

  const auto outside_path = temp_root() / "outside.txt";
  {
    std::ofstream outside_file(outside_path, std::ios::binary | std::ios::trunc);
    outside_file << "outside content";
  }
  auto outside_without_resolver = ava::tools::read_file(build_context, outside_path);
  expect(!outside_without_resolver &&
             outside_without_resolver.error().format().find("resolution: no_resolver") != std::string::npos,
         "read_file fails closed for ask decisions without a resolver");

  int allow_prompts = 0;
  ava::tools::ToolContext allow_context{
      .workspace_dir = workspace,
      .mode = ava::agent::Mode::Build,
      .permission_resolver = [&allow_prompts, &outside_path](const ava::permissions::PermissionPrompt& prompt)
          -> ava::core::Result<ava::permissions::PermissionResolution> {
        ++allow_prompts;
        expect(prompt.operation == ava::permissions::Operation::ReadFile, "file resolver receives read operation");
        expect(prompt.target_path == outside_path, "file resolver receives target path");
        return ava::permissions::PermissionResolution::Allow;
      }};
  auto outside_allowed = ava::tools::read_file(allow_context, outside_path);
  expect(outside_allowed && outside_allowed->content == "outside content" && allow_prompts == 1,
         "read_file allows ask decisions when resolver allows once");

  ava::tools::ToolContext deny_context{
      .workspace_dir = workspace,
      .mode = ava::agent::Mode::Build,
      .permission_resolver =
          [](const ava::permissions::PermissionPrompt&) -> ava::core::Result<ava::permissions::PermissionResolution> {
        return ava::permissions::PermissionResolution::Deny;
      }};
  auto outside_denied = ava::tools::read_file(deny_context, outside_path);
  expect(!outside_denied && outside_denied.error().format().find("resolution: deny") != std::string::npos,
         "read_file fails closed when resolver denies ask decisions");

  ava::tools::ToolContext failing_context{
      .workspace_dir = workspace,
      .mode = ava::agent::Mode::Build,
      .permission_resolver =
          [](const ava::permissions::PermissionPrompt&) -> ava::core::Result<ava::permissions::PermissionResolution> {
        return std::unexpected(ava::core::Error(ava::core::ErrorCategory::Io, "resolver failed"));
      }};
  auto outside_failed = ava::tools::read_file(failing_context, outside_path);
  expect(!outside_failed && outside_failed.error().format().find("resolution: resolver_failed") != std::string::npos,
         "read_file fails closed when resolver fails");

  const auto outside_write_path = temp_root() / "outside-write.txt";
  auto outside_write_without_resolver = ava::tools::write_file(build_context, outside_write_path, "bad");
  expect(!outside_write_without_resolver && !std::filesystem::exists(outside_write_path) &&
             outside_write_without_resolver.error().format().find("resolution: no_resolver") != std::string::npos,
         "write_file fails closed for external ask decisions without writing");

  int write_denials = 0;
  ava::tools::ToolContext write_deny_context{
      .workspace_dir = workspace,
      .mode = ava::agent::Mode::Build,
      .permission_resolver = [&write_denials](const ava::permissions::PermissionPrompt&)
          -> ava::core::Result<ava::permissions::PermissionResolution> {
        ++write_denials;
        return ava::permissions::PermissionResolution::Deny;
      }};
  auto outside_write_denied = ava::tools::write_file(write_deny_context, outside_write_path, "bad");
  expect(!outside_write_denied && write_denials == 1 && !std::filesystem::exists(outside_write_path) &&
             outside_write_denied.error().format().find("resolution: deny") != std::string::npos,
         "write_file fails closed when resolver denies external writes");

  int write_fail_prompts = 0;
  ava::tools::ToolContext write_fail_context{
      .workspace_dir = workspace,
      .mode = ava::agent::Mode::Build,
      .permission_resolver = [&write_fail_prompts](const ava::permissions::PermissionPrompt&)
          -> ava::core::Result<ava::permissions::PermissionResolution> {
        ++write_fail_prompts;
        return std::unexpected(ava::core::Error(ava::core::ErrorCategory::Io, "resolver failed"));
      }};
  auto outside_write_failed = ava::tools::write_file(write_fail_context, outside_write_path, "bad");
  expect(!outside_write_failed && write_fail_prompts == 1 && !std::filesystem::exists(outside_write_path) &&
             outside_write_failed.error().format().find("resolution: resolver_failed") != std::string::npos,
         "write_file fails closed when resolver fails external writes");

  auto outside_edit_without_resolver = ava::tools::edit_file(build_context, outside_path, "outside", "bad");
  auto outside_after_no_resolver_edit = ava::tools::read_file(allow_context, outside_path);
  expect(!outside_edit_without_resolver && outside_after_no_resolver_edit &&
             outside_after_no_resolver_edit->content == "outside content" &&
             outside_edit_without_resolver.error().format().find("resolution: no_resolver") != std::string::npos,
         "edit_file fails closed for external ask decisions without editing");

  int edit_prompts = 0;
  ava::tools::ToolContext edit_deny_context{
      .workspace_dir = workspace,
      .mode = ava::agent::Mode::Build,
      .permission_resolver = [&edit_prompts](const ava::permissions::PermissionPrompt& prompt)
          -> ava::core::Result<ava::permissions::PermissionResolution> {
        ++edit_prompts;
        expect(prompt.operation == ava::permissions::Operation::ReadFile,
               "edit_file resolver sees read operation before denied external edit");
        return ava::permissions::PermissionResolution::Deny;
      }};
  auto outside_edit_denied = ava::tools::edit_file(edit_deny_context, outside_path, "outside", "bad");
  auto outside_after_denied_edit = ava::tools::read_file(allow_context, outside_path);
  expect(!outside_edit_denied && edit_prompts == 1 && outside_after_denied_edit &&
             outside_after_denied_edit->content == "outside content" &&
             outside_edit_denied.error().format().find("resolution: deny") != std::string::npos,
         "edit_file leaves content unchanged when external read permission is denied");

  int edit_fail_prompts = 0;
  ava::tools::ToolContext edit_fail_context{
      .workspace_dir = workspace,
      .mode = ava::agent::Mode::Build,
      .permission_resolver = [&edit_fail_prompts](const ava::permissions::PermissionPrompt& prompt)
          -> ava::core::Result<ava::permissions::PermissionResolution> {
        ++edit_fail_prompts;
        expect(prompt.operation == ava::permissions::Operation::ReadFile,
               "edit_file failure resolver sees read operation before external edit");
        return std::unexpected(ava::core::Error(ava::core::ErrorCategory::Io, "resolver failed"));
      }};
  auto outside_edit_failed = ava::tools::edit_file(edit_fail_context, outside_path, "outside", "bad");
  auto outside_after_failed_edit = ava::tools::read_file(allow_context, outside_path);
  expect(!outside_edit_failed && edit_fail_prompts == 1 && outside_after_failed_edit &&
             outside_after_failed_edit->content == "outside content" &&
             outside_edit_failed.error().format().find("resolution: resolver_failed") != std::string::npos,
         "edit_file fails closed when resolver fails and leaves content unchanged");

  std::vector<ava::permissions::Operation> edit_allow_prompts;
  ava::tools::ToolContext edit_allow_context{
      .workspace_dir = workspace,
      .mode = ava::agent::Mode::Build,
      .permission_resolver = [&edit_allow_prompts](const ava::permissions::PermissionPrompt& prompt)
          -> ava::core::Result<ava::permissions::PermissionResolution> {
        edit_allow_prompts.push_back(prompt.operation);
        return ava::permissions::PermissionResolution::Allow;
      }};
  auto outside_edit_allowed = ava::tools::edit_file(edit_allow_context, outside_path, "outside", "external");
  auto outside_after_allowed_edit = ava::tools::read_file(allow_context, outside_path);
  expect(outside_edit_allowed && edit_allow_prompts.size() == 2 &&
             edit_allow_prompts[0] == ava::permissions::Operation::ReadFile &&
             edit_allow_prompts[1] == ava::permissions::Operation::EditFile && outside_after_allowed_edit &&
             outside_after_allowed_edit->content == "external content",
         "edit_file resolves external read permission before edit permission");
}

void test_permission_audit_persistence() {
  const auto root = temp_root() / "permission-audit";
  std::error_code remove_error;
  std::filesystem::remove_all(root, remove_error);
  const auto workspace = root / "workspace";
  std::filesystem::create_directories(workspace);

  const auto allowed_path = workspace / "allowed.txt";
  {
    std::ofstream file(allowed_path, std::ios::binary | std::ios::trunc);
    file << "allowed";
  }
  const auto secret_path = workspace / ".env";
  {
    std::ofstream file(secret_path, std::ios::binary | std::ios::trunc);
    file << "secret";
  }
  const auto outside_path = root / "outside.txt";
  {
    std::ofstream file(outside_path, std::ios::binary | std::ios::trunc);
    file << "outside";
  }

  ava::session::SessionStore store(ava::session::SessionStoreOptions{
      .root_dir = root / "sessions", .workspace_dir = workspace, .session_id = "audit"});
  auto sink = [&store](const ava::tools::PermissionAuditEvent& event) -> ava::core::VoidResult {
    return append_permission_audit_for_test(store, event);
  };
  const ava::tools::ToolContext context{
      .workspace_dir = workspace, .mode = ava::agent::Mode::Build, .permission_audit_sink = sink};

  auto allowed = ava::tools::read_file(context, allowed_path);
  expect(allowed && allowed->content == "allowed", "permission audit allows normal read");
  auto loaded = store.load();
  auto audits = loaded ? permission_entries(*loaded) : std::vector<ava::session::SessionEntry>{};
  expect(audits.size() == 1, "allowed policy decision appends one audit entry");
  if (!audits.empty()) {
    expect(ava::core::json::string_field(audits[0].data_json, "operation") == "read" &&
               ava::core::json::string_field(audits[0].data_json, "action") == "allow" &&
               ava::core::json::string_field(audits[0].data_json, "resolution") == "allow" &&
               ava::core::json::string_field(audits[0].data_json, "resolution_source") == "policy" &&
               ava::core::json::string_field(audits[0].data_json, "target_path") == allowed_path.string(),
           "allowed audit records policy resolution and target path");
  }

  auto denied = ava::tools::read_file(context, secret_path);
  expect(!denied, "permission audit denied read still fails closed");
  loaded = store.load();
  audits = loaded ? permission_entries(*loaded) : std::vector<ava::session::SessionEntry>{};
  expect(audits.size() == 2, "denied policy decision appends an audit entry");
  if (audits.size() >= 2) {
    expect(ava::core::json::string_field(audits[1].data_json, "action") == "deny" &&
               ava::core::json::string_field(audits[1].data_json, "resolution") == "deny" &&
               ava::core::json::string_field(audits[1].data_json, "resolution_source") == "policy",
           "denied audit records policy denial without resolver");
  }

  int prompts = 0;
  const ava::tools::ToolContext resolving_context{
      .workspace_dir = workspace,
      .mode = ava::agent::Mode::Build,
      .permission_resolver = [&prompts](const ava::permissions::PermissionPrompt&)
          -> ava::core::Result<ava::permissions::PermissionResolution> {
        ++prompts;
        return ava::permissions::PermissionResolution::Allow;
      },
      .permission_audit_sink = sink};
  auto outside = ava::tools::read_file(resolving_context, outside_path);
  expect(outside && outside->content == "outside" && prompts == 1,
         "permission audit preserves resolver-approved read behavior");
  loaded = store.load();
  audits = loaded ? permission_entries(*loaded) : std::vector<ava::session::SessionEntry>{};
  expect(audits.size() == 4, "ask policy and resolver outcome append separate audit entries");
  if (audits.size() >= 4) {
    expect(ava::core::json::string_field(audits[2].data_json, "action") == "ask" &&
               !ava::core::json::string_field(audits[2].data_json, "resolution") &&
               ava::core::json::string_field(audits[2].data_json, "resolution_source") == "policy",
           "ask audit records policy request before resolver outcome");
    expect(ava::core::json::string_field(audits[3].data_json, "action") == "ask" &&
               ava::core::json::string_field(audits[3].data_json, "resolution") == "allow" &&
               ava::core::json::string_field(audits[3].data_json, "resolution_source") == "resolver",
           "ask audit records resolver allow outcome");
  }

  auto bash_denied = ava::tools::run_bash(context, "rm -rf important");
  expect(!bash_denied, "permission audit preserves bash policy denial");
  loaded = store.load();
  audits = loaded ? permission_entries(*loaded) : std::vector<ava::session::SessionEntry>{};
  expect(audits.size() == 5, "bash policy decision appends command audit entry");
  if (audits.size() >= 5) {
    expect(ava::core::json::string_field(audits[4].data_json, "operation") == "bash" &&
               ava::core::json::string_field(audits[4].data_json, "command") == "rm -rf important" &&
               !ava::core::json::string_field(audits[4].data_json, "target_path"),
           "bash audit records command without path-only target field");
  }

  const auto exported = ava::session::format_session_markdown(audits);
  expect(exported.find("## Permission Decision") != std::string::npos &&
             exported.find("\"operation\":\"read\"") != std::string::npos &&
             exported.find("\"resolution_source\":\"resolver\"") != std::string::npos,
         "session export includes permission decision audit data");
}

void test_search_tools() {
  std::error_code remove_error;
  std::filesystem::remove_all(temp_root(), remove_error);

  const auto workspace = temp_root() / "workspace";
  const ava::tools::ToolContext context{.workspace_dir = workspace, .mode = ava::agent::Mode::Build};

  expect(ava::tools::write_file(context, workspace / "src" / "main.cpp", "int main() { return 0; }\n").has_value(),
         "search setup writes source");
  expect(ava::tools::write_file(context, workspace / "root.cpp", "int root() { return 0; }\n").has_value(),
         "search setup writes root source");
  expect(ava::tools::write_file(context, workspace / "docs" / "plan.md", "hello ava\nhello again\n").has_value(),
         "search setup writes docs");
  expect(ava::tools::write_file(context, workspace / "build" / "ignored.txt", "hello hidden\n").has_value(),
         "search setup writes ignored file");
  {
    std::ofstream secret_file(workspace / ".env", std::ios::binary | std::ios::trunc);
    secret_file << "hello secret\n";
  }
  std::filesystem::create_directories(workspace / ".ssh");
  {
    std::ofstream key_file(workspace / ".ssh" / "id_rsa", std::ios::binary | std::ios::trunc);
    key_file << "hello key\n";
  }

  auto glob = ava::tools::glob_files(context, "**/*.cpp");
  expect(glob.has_value(), "glob_files succeeds");
  if (glob) {
    expect(glob->paths.size() == 2, "glob_files returns nested and root source files");
    expect(glob->paths.size() < 2 || glob->paths[0].generic_string() < glob->paths[1].generic_string(),
           "glob_files sorts results deterministically by generic path string");
  }

  auto bracket_glob = ava::tools::glob_files(context, "*.[ch]");
  expect(!bracket_glob && bracket_glob.error().category() == ava::core::ErrorCategory::InvalidArgument &&
             bracket_glob.error().message().find("bracket") != std::string::npos,
         "glob_files rejects unsupported bracket character classes instead of silently mis-matching them");

  auto result_capped = ava::tools::glob_files(context, "**/*.cpp", ava::tools::GlobOptions{.max_results = 1});
  expect(result_capped && result_capped->paths.size() == 1 && result_capped->total_matches == 2 &&
             result_capped->truncated,
         "glob_files reports result-count truncation while counting all matches");

  auto capped = ava::tools::glob_files(context, "**/*", ava::tools::GlobOptions{.max_results = 2000, .max_visited = 1});
  expect(capped && capped->truncated, "glob_files reports traversal cap truncation");

  auto grep = ava::tools::grep_files(context, "hello", "**/*.md");
  expect(grep.has_value(), "grep_files succeeds");
  if (grep) {
    expect(grep->matches.size() == 2, "grep_files returns matching markdown lines");
    expect(grep->matches[0].line_number == 1, "grep_files records line numbers");
  }

  auto punctuation = ava::tools::grep_files(context, "main()", "**/*.cpp");
  expect(punctuation && !punctuation->matches.empty(), "grep_files literal search accepts punctuation");

  auto truncated = ava::tools::grep_files(context, "int", "**/*.cpp", ava::tools::GrepOptions{.max_line_length = 5});
  expect(truncated && !truncated->matches.empty() && truncated->matches[0].line_truncated,
         "grep_files reports line truncation metadata");

  auto ignored = ava::tools::grep_files(context, "hidden", "**/*");
  expect(ignored && ignored->matches.empty(), "grep_files skips generated folders");

  auto glob_secrets = ava::tools::glob_files(context, "**/*");
  expect(glob_secrets &&
             std::ranges::none_of(glob_secrets->paths,
                                  [](const std::filesystem::path& path) { return path.filename() == "id_rsa"; }),
         "glob_files skips files denied by read policy");

  auto secret = ava::tools::grep_files(context, "secret", "**/*");
  expect(secret && secret->matches.empty(), "grep_files skips files denied by read policy");

  const auto outside_search_path = temp_root() / "outside-search.txt";
  {
    std::ofstream outside_file(outside_search_path, std::ios::binary | std::ios::trunc);
    outside_file << "outside hello\n";
  }
  std::error_code symlink_error;
  const auto outside_search_link = workspace / "outside-link.txt";
  std::filesystem::create_symlink(outside_search_path, outside_search_link, symlink_error);
  if (!symlink_error) {
    int search_prompts = 0;
    std::vector<ava::tools::PermissionAuditEvent> search_audits;
    const ava::tools::ToolContext resolving_search_context{
        .workspace_dir = workspace,
        .mode = ava::agent::Mode::Build,
        .permission_resolver = [&search_prompts, &outside_search_link](const ava::permissions::PermissionPrompt& prompt)
            -> ava::core::Result<ava::permissions::PermissionResolution> {
          ++search_prompts;
          expect(prompt.operation == ava::permissions::Operation::ReadFile,
                 "search resolver receives read operation for symlink escapes");
          expect(prompt.target_path == outside_search_link, "search resolver receives the matching symlink path");
          return ava::permissions::PermissionResolution::Allow;
        },
        .permission_audit_sink =
            [&search_audits](const ava::tools::PermissionAuditEvent& event) -> ava::core::VoidResult {
          search_audits.push_back(event);
          return {};
        }};
    auto resolved_glob = ava::tools::glob_files(resolving_search_context, "**/*");
    const bool resolved_includes_link =
        resolved_glob && std::ranges::any_of(resolved_glob->paths, [&outside_search_link](const auto& path) {
          return path == outside_search_link;
        });
    expect(resolved_includes_link && search_prompts == 1,
           "glob_files resolves ask decisions for symlinked matches instead of silently skipping them");
    expect(search_audits.size() == 3 && search_audits[0].operation == ava::permissions::Operation::SearchFiles &&
               search_audits[0].action == ava::permissions::PermissionAction::Allow &&
               search_audits[1].operation == ava::permissions::Operation::ReadFile &&
               search_audits[1].action == ava::permissions::PermissionAction::Ask &&
               search_audits[2].resolution == "allow" && search_audits[2].resolution_source == "resolver",
           "glob_files audits the search root and ask resolver outcome without per-file allow audits");

    int denied_search_prompts = 0;
    const ava::tools::ToolContext denying_search_context{
        .workspace_dir = workspace,
        .mode = ava::agent::Mode::Build,
        .permission_resolver = [&denied_search_prompts](const ava::permissions::PermissionPrompt&)
            -> ava::core::Result<ava::permissions::PermissionResolution> {
          ++denied_search_prompts;
          return ava::permissions::PermissionResolution::Deny;
        }};
    auto denied_glob = ava::tools::glob_files(denying_search_context, "**/*");
    const bool denied_excludes_link =
        denied_glob && std::ranges::none_of(denied_glob->paths, [&outside_search_link](const auto& path) {
          return path == outside_search_link;
        });
    expect(denied_excludes_link && denied_search_prompts == 1, "glob_files keeps resolver-denied ask matches excluded");

    int failing_search_prompts = 0;
    const ava::tools::ToolContext failing_search_context{
        .workspace_dir = workspace,
        .mode = ava::agent::Mode::Build,
        .permission_resolver = [&failing_search_prompts](const ava::permissions::PermissionPrompt&)
            -> ava::core::Result<ava::permissions::PermissionResolution> {
          ++failing_search_prompts;
          return std::unexpected(ava::core::Error(ava::core::ErrorCategory::Io, "search resolver failed"));
        }};
    auto failing_glob = ava::tools::glob_files(failing_search_context, "**/*");
    const bool failing_skips_link =
        failing_glob && std::ranges::none_of(failing_glob->paths, [&outside_search_link](const auto& path) {
          return path == outside_search_link;
        });
    const bool failing_keeps_readable_match =
        failing_glob && std::ranges::any_of(failing_glob->paths, [&workspace](const auto& path) {
          return path == workspace / "docs" / "plan.md";
        });
    expect(failing_skips_link && failing_keeps_readable_match && failing_search_prompts == 1,
           "glob_files skips ask matches with resolver errors while continuing search");
  }

  {
    std::ofstream binary_file(workspace / "binary.bin", std::ios::binary | std::ios::trunc);
    binary_file << std::string("binary", 6) << '\0' << " marker\nhello from binary\n";
  }
  auto binary = ava::tools::grep_files(context, "hello", "**/*.bin");
  expect(binary && binary->matches.empty() && binary->total_matches == 0,
         "grep_files skips an entire file after detecting binary content");

  {
    std::ofstream binary_file(workspace / "overlong-binary.bin", std::ios::binary | std::ios::trunc);
    binary_file << std::string(32, 'x') << '\0' << " hello after nul\n";
  }
  auto overlong_binary =
      ava::tools::grep_files(context, "hello", "**/overlong-binary.bin", ava::tools::GrepOptions{.max_line_length = 5});
  expect(overlong_binary && overlong_binary->matches.empty() && overlong_binary->total_matches == 0,
         "grep_files treats NUL after an overlong truncation point as binary content");
}

void test_bash_tool() {
  std::error_code remove_error;
  std::filesystem::remove_all(temp_root(), remove_error);
  std::filesystem::create_directories(temp_root());

  const ava::tools::ToolContext context{.workspace_dir = temp_root(), .mode = ava::agent::Mode::Build};

  auto pwd = ava::tools::run_bash(context, "pwd", ava::tools::BashOptions{.timeout = std::chrono::milliseconds(1000)});
  expect(pwd.has_value(), "run_bash allows safe command");
  if (pwd) {
    expect(pwd->exit_code == 0, "run_bash records exit code");
    expect(pwd->output.find(temp_root().string()) != std::string::npos, "run_bash uses workspace directory");
  }

  auto capped_output = ava::tools::run_bash(
      context, "pwd", ava::tools::BashOptions{.timeout = std::chrono::milliseconds(1000), .max_bytes = 4});
  expect(capped_output && capped_output->exit_code == 0 && capped_output->truncated &&
             capped_output->output.size() == 4 && capped_output->total_bytes > capped_output->output.size(),
         "run_bash bounds retained output while reporting total bytes");

  const auto hijack_path = temp_root() / "pwd";
  {
    std::ofstream hijack(hijack_path, std::ios::binary | std::ios::trunc);
    hijack << "#!/bin/sh\nprintf hijacked-path\n";
  }
  expect(chmod(hijack_path.c_str(), 0700) == 0, "test can create executable PATH hijack fixture");
  {
    const ScopedEnvVar path_guard("PATH", temp_root().string() + ":.:relative");
    auto sanitized_pwd =
        ava::tools::run_bash(context, "pwd", ava::tools::BashOptions{.timeout = std::chrono::milliseconds(1000)});
    expect(sanitized_pwd && sanitized_pwd->exit_code == 0 &&
               sanitized_pwd->output.find("hijacked-path") == std::string::npos &&
               sanitized_pwd->output.find(temp_root().string()) != std::string::npos,
           "run_bash does not inherit unsafe PATH entries for auto-allowed commands");
  }

  auto denied = ava::tools::run_bash(context, "rm -rf important");
  expect(!denied, "run_bash denies destructive command");

  auto chained = ava::tools::run_bash(context, "pwd; rm -rf important");
  expect(!chained, "run_bash rejects shell metacharacters");
  auto allowed_prefix_chain = ava::tools::run_bash(context, "pwd; whoami");
  expect(!allowed_prefix_chain, "run_bash rejects shell metacharacters after allowed prefix");

  expect(!ava::tools::run_bash(context, "cmake -E cat ~/.config/ava/auth.json"),
         "run_bash denies cmake -E file helper before execution");
  expect(!ava::tools::run_bash(context, "cmake -P docs/plan.md"),
         "run_bash denies cmake script execution before execution");
  expect(!ava::tools::run_bash(context, "cmake -E copy docs/plan.md src/new.cpp"),
         "run_bash denies cmake copy helper before execution");

  auto timeout =
      ava::tools::run_bash(context, "sleep 2", ava::tools::BashOptions{.timeout = std::chrono::milliseconds(50)});
  expect(timeout && timeout->timed_out, "run_bash times out long command");

  auto ask_without_resolver = ava::tools::run_bash(context, "true");
  expect(!ask_without_resolver &&
             ask_without_resolver.error().format().find("resolution: no_resolver") != std::string::npos,
         "run_bash fails closed for ask decisions without a resolver");

  int bash_prompts = 0;
  ava::tools::ToolContext allow_context{
      .workspace_dir = temp_root(),
      .mode = ava::agent::Mode::Build,
      .permission_resolver = [&bash_prompts](const ava::permissions::PermissionPrompt& prompt)
          -> ava::core::Result<ava::permissions::PermissionResolution> {
        ++bash_prompts;
        expect(prompt.operation == ava::permissions::Operation::RunCommand, "bash resolver receives run operation");
        expect(prompt.command == "true", "bash resolver receives command text");
        return ava::permissions::PermissionResolution::Allow;
      }};
  auto ask_allowed = ava::tools::run_bash(allow_context, "true");
  expect(ask_allowed && ask_allowed->exit_code == 0 && bash_prompts == 1,
         "run_bash allows ask decisions when resolver allows once");

  ava::tools::ToolContext deny_context{
      .workspace_dir = temp_root(),
      .mode = ava::agent::Mode::Build,
      .permission_resolver =
          [](const ava::permissions::PermissionPrompt&) -> ava::core::Result<ava::permissions::PermissionResolution> {
        return ava::permissions::PermissionResolution::Deny;
      }};
  auto ask_denied = ava::tools::run_bash(deny_context, "true");
  expect(!ask_denied && ask_denied.error().format().find("resolution: deny") != std::string::npos,
         "run_bash fails closed when resolver denies ask decisions");

  ava::tools::ToolContext failing_context{
      .workspace_dir = temp_root(),
      .mode = ava::agent::Mode::Build,
      .permission_resolver =
          [](const ava::permissions::PermissionPrompt&) -> ava::core::Result<ava::permissions::PermissionResolution> {
        return std::unexpected(ava::core::Error(ava::core::ErrorCategory::Io, "resolver failed"));
      }};
  auto ask_failed = ava::tools::run_bash(failing_context, "true");
  expect(!ask_failed && ask_failed.error().format().find("resolution: resolver_failed") != std::string::npos,
         "run_bash fails closed when resolver fails");
}

}  // namespace

void run_tools_tests() {
  test_file_tools();
  test_permission_audit_persistence();
  test_search_tools();
  test_bash_tool();
}
