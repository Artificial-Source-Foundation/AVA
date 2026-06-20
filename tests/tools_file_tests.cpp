#include "tests/support/test_harness.h"
#include "ava/agent/mode.h"
#include "ava/tools/bash_tool.h"
#include "ava/tools/file_tools.h"
#include "ava/tools/mutation_queue.h"
#include "ava/session/export.h"
#include "ava/session/session_store.h"
#include "ava/permissions/permission.h"
#include "ava/core/json.h"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <memory>
#include <optional>
#include <sstream>
#include <string>
#include <utility>
#include <vector>
#include <sys/stat.h>

namespace {

std::string read_text_file_for_test(std::filesystem::path const& path)
{
  std::ifstream file(path, std::ios::binary);
  std::ostringstream out;
  out << file.rdbuf();
  return out.str();
}

void test_file_tools()
{
  std::error_code remove_error;
  std::filesystem::remove_all(temp_root(), remove_error);
  std::filesystem::create_directories(temp_root());

  auto const workspace = temp_root() / "workspace";
  std::filesystem::create_directories(workspace / "docs");
  auto const source_path = workspace / "src.txt";
  ava::tools::ToolContext const build_context{.workspace_dir = workspace, .mode = ava::agent::Mode::Build};
  ava::tools::ToolContext const plan_context{.workspace_dir = workspace, .mode = ava::agent::Mode::Plan};
  auto const has_write_temp = [](std::filesystem::path const& directory) {
    std::error_code iter_error;
    for (std::filesystem::directory_iterator it(directory, iter_error), end; !iter_error && it != end; it.increment(iter_error))
    {
      if (it->path().filename().string().find(".ava-write-") != std::string::npos)
        return true;
    }
    return false;
  };
  auto const permission_bits = [](std::filesystem::path const& permission_path) {
    constexpr auto mask = std::filesystem::perms::owner_all | std::filesystem::perms::group_all | std::filesystem::perms::others_all;
    std::error_code status_error;
    return std::filesystem::status(permission_path, status_error).permissions() & mask;
  };

  auto write = ava::tools::write_file(build_context, source_path, "hello world");
  expect(write.has_value(), "write_file writes in build mode");
  expect(write && permission_bits(source_path) == (std::filesystem::perms::owner_read | std::filesystem::perms::owner_write),
         "write_file creates new files with 0600 permissions");

  auto read = ava::tools::read_file(build_context, source_path, ava::tools::ReadOptions{.max_bytes = 5});
  expect(read.has_value(), "read_file reads content");
  if (read)
  {
    expect(read->content == "hello", "read_file truncates head");
    expect(read->truncated && read->byte_limited && !read->line_limited && read->output_lines == 1,
           "read_file reports byte-cap truncation as line-aware metadata");
  }
  auto const ranged_path = workspace / "range.txt";
  {
    std::ofstream ranged_file(ranged_path, std::ios::binary | std::ios::trunc);
    ranged_file << "one\n"
                   "two\n"
                   "three\n"
                   "four\n";
  }
  auto ranged = ava::tools::read_file(build_context, ranged_path, ava::tools::ReadOptions{.max_bytes = 1024, .offset_line = 2, .max_lines = 2});
  expect(ranged && ranged->content == "two\nthree\n" && ranged->start_line == 2 && ranged->end_line == 3 && ranged->output_lines == 2 &&
             ranged->total_lines == 4 && ranged->line_limited && !ranged->byte_limited && ranged->next_offset_line == 4,
         "read_file supports line offset and limit continuation metadata");

  auto edit = ava::tools::edit_file(build_context, source_path, "world", "ava");
  expect(edit.has_value(), "edit_file edits unique text");

  auto edited = ava::tools::read_file(build_context, source_path);
  expect(edited && edited->content == "hello ava", "edit_file result is persisted");

  auto const overlapping_path = workspace / "overlapping.txt";
  {
    std::ofstream overlapping_file(overlapping_path, std::ios::binary | std::ios::trunc);
    overlapping_file << "aaa";
  }
  auto overlapping_edit = ava::tools::edit_file(build_context, overlapping_path, "aa", "b");
  auto overlapping_read = ava::tools::read_file(build_context, overlapping_path);
  expect(
      !overlapping_edit && overlapping_read && overlapping_read->content == "aaa" && overlapping_edit.error().format().find("not unique") != std::string::npos,
      "edit_file rejects overlapping old_text matches as ambiguous");

  auto const atomic_path = workspace / "atomic.txt";
  {
    std::ofstream atomic_file(atomic_path, std::ios::binary | std::ios::trunc);
    atomic_file << "original content";
  }
  auto atomic_write = ava::tools::write_file(build_context, atomic_path, "replacement content");
  auto atomic_read = ava::tools::read_file(build_context, atomic_path);
  expect(atomic_write && atomic_read && atomic_read->content == "replacement content" && !has_write_temp(workspace),
         "write_file atomically replaces existing content and cleans the staging file on success");

  auto const protected_path = workspace / "protected.txt";
  {
    std::ofstream protected_file(protected_path, std::ios::binary | std::ios::trunc);
    protected_file << "private original";
  }
  std::error_code chmod_error;
  std::filesystem::permissions(protected_path, std::filesystem::perms::owner_read | std::filesystem::perms::owner_write, std::filesystem::perm_options::replace,
                               chmod_error);
  expect(!chmod_error, "test can set private file permissions");
  auto protected_write = ava::tools::write_file(build_context, protected_path, "private replacement");
  auto protected_read = ava::tools::read_file(build_context, protected_path);
  expect(protected_write && protected_read && protected_read->content == "private replacement" &&
             permission_bits(protected_path) == (std::filesystem::perms::owner_read | std::filesystem::perms::owner_write),
         "write_file preserves 0600 permissions when overwriting a file");

  auto const edit_private_path = workspace / "edit-private.txt";
  {
    std::ofstream edit_private_file(edit_private_path, std::ios::binary | std::ios::trunc);
    edit_private_file << "alpha beta";
  }
  chmod_error.clear();
  std::filesystem::permissions(edit_private_path, std::filesystem::perms::owner_read | std::filesystem::perms::owner_write,
                               std::filesystem::perm_options::replace, chmod_error);
  expect(!chmod_error, "test can set private edit file permissions");
  auto edit_private = ava::tools::edit_file(build_context, edit_private_path, "beta", "gamma");
  auto edit_private_read = ava::tools::read_file(build_context, edit_private_path);
  expect(edit_private && edit_private_read && edit_private_read->content == "alpha gamma" &&
             permission_bits(edit_private_path) == (std::filesystem::perms::owner_read | std::filesystem::perms::owner_write),
         "edit_file preserves 0600 permissions through atomic write_file");

  auto const crlf_path = workspace / "crlf.txt";
  {
    std::ofstream crlf_file(crlf_path, std::ios::binary | std::ios::trunc);
    crlf_file << "alpha\r\nbeta\r\n";
  }
  auto crlf_lf_only_edit = ava::tools::edit_file(build_context, crlf_path, "alpha\nbeta\n", "gamma\n");
  expect(!crlf_lf_only_edit && crlf_lf_only_edit.error().format().find("CRLF") != std::string::npos, "edit_file explains CRLF-sensitive exact match failures");
  auto crlf_exact_edit = ava::tools::edit_file(build_context, crlf_path, "alpha\r\nbeta\r\n", "gamma\r\ndelta\r\n");
  expect(crlf_exact_edit && read_text_file_for_test(crlf_path) == "gamma\r\ndelta\r\n" && crlf_exact_edit->line_endings == "CRLF",
         "edit_file preserves CRLF bytes when the provider supplies an exact CRLF match");

  auto const bom_path = workspace / "bom.txt";
  {
    std::ofstream bom_file(bom_path, std::ios::binary | std::ios::trunc);
    bom_file << "\xEF\xBB\xBF"
                "alpha\n";
  }
  auto bom_edit = ava::tools::edit_file(build_context, bom_path, "alpha\n", "beta\n");
  expect(bom_edit && bom_edit->had_utf8_bom &&
             read_text_file_for_test(bom_path) ==
                 "\xEF\xBB\xBF"
                 "beta\n",
         "edit_file preserves UTF-8 BOM bytes across exact replacements");

  auto const shared_queue = std::make_shared<ava::tools::MutationQueue>();
  ava::tools::ToolContext const queued_context{.workspace_dir = workspace, .mode = ava::agent::Mode::Build, .mutation_queue = shared_queue};
  auto const queued_path = workspace / "queued-edit.txt";
  auto queued_write = ava::tools::write_file(queued_context, queued_path, "one two");
  auto queued_edit = ava::tools::edit_file(queued_context, queued_path, "two", "three");
  auto queued_read = ava::tools::read_file(queued_context, queued_path);
  expect(queued_write && queued_edit && queued_read && queued_read->content == "one three",
         "write_file and edit_file can share a mutation queue without nested edit deadlock");

  auto const audit_edit_path = workspace / "audit-edit.txt";
  {
    std::ofstream audit_edit_file(audit_edit_path, std::ios::binary | std::ios::trunc);
    audit_edit_file << "read then edit";
  }
  std::vector<ava::tools::PermissionAuditEvent> edit_audits;
  ava::tools::ToolContext const audit_edit_context{
      .workspace_dir = workspace,
      .mode = ava::agent::Mode::Build,
      .permission_audit_sink = [&edit_audits](ava::tools::PermissionAuditEvent const& event) -> ava::core::VoidResult {
        edit_audits.push_back(event);
        return {};
      }};
  auto audited_edit = ava::tools::edit_file(audit_edit_context, audit_edit_path, "then", "before");
  expect(audited_edit && edit_audits.size() == 2 && edit_audits[0].operation == ava::permissions::Operation::ReadFile &&
             edit_audits[1].operation == ava::permissions::Operation::EditFile,
         "edit_file audits read permission before edit permission");

  auto const rename_failure_path = workspace / "rename-failure-target";
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
  expect(external_ssh_secret.action == ava::permissions::PermissionAction::Deny, "external ssh key reads deny before outside-workspace ask");
  auto external_npmrc_secret = ava::permissions::decide(ava::permissions::PermissionRequest{
      .operation = ava::permissions::Operation::EditFile,
      .mode = ava::agent::Mode::Build,
      .workspace_dir = workspace,
      .target_path = "/home/user/.npmrc",
      .command = "",
  });
  expect(external_npmrc_secret.action == ava::permissions::PermissionAction::Deny, "external npm credential edits deny before outside-workspace ask");
  auto external_ava_auth = ava::permissions::decide(ava::permissions::PermissionRequest{
      .operation = ava::permissions::Operation::ReadFile,
      .mode = ava::agent::Mode::Build,
      .workspace_dir = workspace,
      .target_path = "/home/user/.config/ava/auth.json",
      .command = "",
  });
  expect(external_ava_auth.action == ava::permissions::PermissionAction::Deny, "external ava auth reads deny before outside-workspace ask");
  std::error_code symlink_error;
  auto const secret_link = workspace / "safe.txt";
  std::filesystem::create_symlink(workspace / ".env", secret_link, symlink_error);
  if (!symlink_error)
  {
    auto linked_secret = ava::tools::read_file(build_context, secret_link);
    expect(!linked_secret, "read_file denies symlink to secret file");
  }

  auto const source_link = workspace / "docs" / "plan-link.md";
  symlink_error.clear();
  std::filesystem::create_symlink(source_path, source_link, symlink_error);
  if (!symlink_error)
  {
    auto linked_source_edit = ava::tools::write_file(plan_context, source_link, "bad");
    expect(!linked_source_edit, "plan mode denies symlink edit to source file");
  }

  auto plan_doc = ava::tools::write_file(plan_context, workspace / "docs" / "plan.md", "# Plan\n");
  expect(plan_doc.has_value(), "plan mode markdown plan write is allowed");

  auto const diffed_write_path = workspace / "diffed-write.txt";
  {
    std::ofstream file(diffed_write_path, std::ios::binary | std::ios::trunc);
    file << "old line\n";
  }
  auto diffed_write = ava::tools::write_file(build_context, diffed_write_path, "new line\n");
  expect(diffed_write && diffed_write->diff.find("-old line") != std::string::npos &&
             diffed_write->diff.find("+new line") != std::string::npos && !diffed_write->diff_truncated,
         "write_file returns the backend-generated unified diff after successful mutation");

  auto const large_path = workspace / "large.txt";
  {
    std::ofstream large(large_path, std::ios::binary | std::ios::trunc);
    large << std::string(8192, 'x');
  }
  auto large_read = ava::tools::read_file(build_context, large_path, ava::tools::ReadOptions{.max_bytes = 16});
  expect(large_read && large_read->content.size() == 16 && large_read->total_bytes == 8192 && large_read->byte_limited && large_read->output_lines == 1,
         "read_file bounds output while counting bytes");

  auto const canceled_existing_path = workspace / "canceled-existing.txt";
  {
    std::ofstream canceled_file(canceled_existing_path, std::ios::binary | std::ios::trunc);
    canceled_file << "keep original";
  }
  auto const canceled_outside_path = temp_root() / "canceled-outside.txt";
  {
    std::ofstream canceled_outside(canceled_outside_path, std::ios::binary | std::ios::trunc);
    canceled_outside << "outside original";
  }
  int canceled_file_prompts = 0;
  ava::tools::ToolContext const canceled_file_context{
      .workspace_dir = workspace,
      .mode = ava::agent::Mode::Build,
      .permission_resolver =
          [&canceled_file_prompts](ava::permissions::PermissionPrompt const&) -> ava::core::Result<ava::permissions::PermissionResolutionDecision> {
        ++canceled_file_prompts;
        return ava::permissions::PermissionResolution::Allow;
      },
      .cancel_requested = [] { return true; }};
  auto canceled_read = ava::tools::read_file(canceled_file_context, canceled_outside_path);
  expect(!canceled_read && canceled_read.error().message() == "tool canceled" && canceled_file_prompts == 0,
         "read_file observes cancellation before prompting for permission");
  auto canceled_write = ava::tools::write_file(canceled_file_context, workspace / "canceled-write.txt", "bad");
  expect(!canceled_write && canceled_write.error().message() == "tool canceled" && !std::filesystem::exists(workspace / "canceled-write.txt") &&
             !has_write_temp(workspace),
         "write_file observes cancellation before mutating the filesystem");
  auto canceled_edit = ava::tools::edit_file(canceled_file_context, canceled_existing_path, "original", "changed");
  expect(!canceled_edit && canceled_edit.error().message() == "tool canceled" && read_text_file_for_test(canceled_existing_path) == "keep original",
         "edit_file observes cancellation before mutating the filesystem");

  int write_cancel_checks = 0;
  ava::tools::ToolContext const cancel_during_write_context{
      .workspace_dir = workspace, .mode = ava::agent::Mode::Build, .cancel_requested = [&write_cancel_checks] {
        ++write_cancel_checks;
        return write_cancel_checks >= 8;
      }};
  auto const canceled_large_write_path = workspace / "canceled-large-write.txt";
  auto canceled_large_write = ava::tools::write_file(cancel_during_write_context, canceled_large_write_path, std::string(64 * 1024, 'x'));
  expect(!canceled_large_write && canceled_large_write.error().message() == "tool canceled" && !std::filesystem::exists(canceled_large_write_path) &&
             !has_write_temp(workspace),
         "write_file cleans staged data when cancellation arrives during chunked writes");

  auto const outside_path = temp_root() / "outside.txt";
  {
    std::ofstream outside_file(outside_path, std::ios::binary | std::ios::trunc);
    outside_file << "outside content";
  }
  auto outside_without_resolver = ava::tools::read_file(build_context, outside_path);
  expect(!outside_without_resolver && outside_without_resolver.error().format().find("resolution: no_resolver") != std::string::npos,
         "read_file fails closed for ask decisions without a resolver");

  int allow_prompts = 0;
  ava::tools::ToolContext allow_context{.workspace_dir = workspace,
                                        .mode = ava::agent::Mode::Build,
                                        .permission_resolver = [&allow_prompts, &outside_path](ava::permissions::PermissionPrompt const& prompt)
                                            -> ava::core::Result<ava::permissions::PermissionResolutionDecision> {
                                          ++allow_prompts;
                                          expect(prompt.operation == ava::permissions::Operation::ReadFile, "file resolver receives read operation");
                                          expect(prompt.target_path == outside_path, "file resolver receives target path");
                                          return ava::permissions::PermissionResolution::Allow;
                                        }};
  auto outside_allowed = ava::tools::read_file(allow_context, outside_path);
  expect(outside_allowed && outside_allowed->content == "outside content" && allow_prompts == 1, "read_file allows ask decisions when resolver allows once");

  ava::tools::ToolContext deny_context{
      .workspace_dir = workspace,
      .mode = ava::agent::Mode::Build,
      .permission_resolver = [](ava::permissions::PermissionPrompt const&) -> ava::core::Result<ava::permissions::PermissionResolutionDecision> {
        return ava::permissions::PermissionResolutionDecision{ava::permissions::PermissionResolution::Deny, "not approved by test resolver"};
      }};
  auto outside_denied = ava::tools::read_file(deny_context, outside_path);
  expect(!outside_denied && outside_denied.error().format().find("resolution: deny") != std::string::npos &&
             outside_denied.error().format().find("resolution_reason: not approved by test resolver") != std::string::npos,
         "read_file fails closed with resolver denial reasons when resolver denies ask decisions");

  ava::tools::ToolContext failing_context{
      .workspace_dir = workspace,
      .mode = ava::agent::Mode::Build,
      .permission_resolver = [](ava::permissions::PermissionPrompt const&) -> ava::core::Result<ava::permissions::PermissionResolutionDecision> {
        return std::unexpected(ava::core::Error(ava::core::ErrorCategory::Io, "resolver failed"));
      }};
  auto outside_failed = ava::tools::read_file(failing_context, outside_path);
  expect(!outside_failed && outside_failed.error().format().find("resolution: resolver_failed") != std::string::npos,
         "read_file fails closed when resolver fails");

  auto const outside_write_path = temp_root() / "outside-write.txt";
  auto outside_write_without_resolver = ava::tools::write_file(build_context, outside_write_path, "bad");
  expect(!outside_write_without_resolver && !std::filesystem::exists(outside_write_path) &&
             outside_write_without_resolver.error().format().find("resolution: no_resolver") != std::string::npos,
         "write_file fails closed for external ask decisions without writing");

  std::vector<ava::permissions::PermissionPrompt> write_denials;
  ava::tools::ToolContext write_deny_context{
      .workspace_dir = workspace,
      .mode = ava::agent::Mode::Build,
      .permission_resolver =
          [&write_denials](ava::permissions::PermissionPrompt const& prompt) -> ava::core::Result<ava::permissions::PermissionResolutionDecision> {
        write_denials.push_back(prompt);
        return ava::permissions::PermissionResolution::Deny;
      }};
  auto outside_write_denied = ava::tools::write_file(write_deny_context, outside_write_path, "bad");
  expect(!outside_write_denied && write_denials.size() == 1 && !std::filesystem::exists(outside_write_path) &&
             outside_write_denied.error().format().find("resolution: deny") != std::string::npos,
         "write_file fails closed when resolver denies external writes");
  if (!write_denials.empty())
  {
    expect(write_denials[0].operation == ava::permissions::Operation::EditFile && write_denials[0].diff_preview.find("+bad") != std::string::npos &&
               !write_denials[0].diff_truncated,
           "write_file includes backend-generated diff preview for denied new-file mutation prompts");
  }

  auto const outside_existing_write_path = temp_root() / "outside-existing-write.txt";
  {
    std::ofstream file(outside_existing_write_path, std::ios::binary | std::ios::trunc);
    file << "external secret";
  }
  std::vector<ava::permissions::PermissionPrompt> existing_write_denials;
  ava::tools::ToolContext existing_write_deny_context{
      .workspace_dir = workspace,
      .mode = ava::agent::Mode::Build,
      .permission_resolver =
          [&existing_write_denials](ava::permissions::PermissionPrompt const& prompt) -> ava::core::Result<ava::permissions::PermissionResolutionDecision> {
        existing_write_denials.push_back(prompt);
        return ava::permissions::PermissionResolution::Deny;
      }};
  auto existing_write_denied = ava::tools::write_file(existing_write_deny_context, outside_existing_write_path, "replacement");
  expect(!existing_write_denied && existing_write_denials.size() == 1 && read_text_file_for_test(outside_existing_write_path) == "external secret",
         "write_file fails closed when resolver denies existing external writes");
  if (!existing_write_denials.empty())
  {
    expect(existing_write_denials[0].diff_preview.empty(), "write_file does not leak existing external file content into a diff before read approval");
  }

  int write_fail_prompts = 0;
  ava::tools::ToolContext write_fail_context{
      .workspace_dir = workspace,
      .mode = ava::agent::Mode::Build,
      .permission_resolver =
          [&write_fail_prompts](ava::permissions::PermissionPrompt const&) -> ava::core::Result<ava::permissions::PermissionResolutionDecision> {
        ++write_fail_prompts;
        return std::unexpected(ava::core::Error(ava::core::ErrorCategory::Io, "resolver failed"));
      }};
  auto outside_write_failed = ava::tools::write_file(write_fail_context, outside_write_path, "bad");
  expect(!outside_write_failed && write_fail_prompts == 1 && !std::filesystem::exists(outside_write_path) &&
             outside_write_failed.error().format().find("resolution: resolver_failed") != std::string::npos,
         "write_file fails closed when resolver fails external writes");

  auto outside_edit_without_resolver = ava::tools::edit_file(build_context, outside_path, "outside", "bad");
  auto outside_after_no_resolver_edit = ava::tools::read_file(allow_context, outside_path);
  expect(!outside_edit_without_resolver && outside_after_no_resolver_edit && outside_after_no_resolver_edit->content == "outside content" &&
             outside_edit_without_resolver.error().format().find("resolution: no_resolver") != std::string::npos,
         "edit_file fails closed for external ask decisions without editing");

  int edit_prompts = 0;
  ava::tools::ToolContext edit_deny_context{
      .workspace_dir = workspace,
      .mode = ava::agent::Mode::Build,
      .permission_resolver =
          [&edit_prompts](ava::permissions::PermissionPrompt const& prompt) -> ava::core::Result<ava::permissions::PermissionResolutionDecision> {
        ++edit_prompts;
        expect(prompt.operation == ava::permissions::Operation::ReadFile, "edit_file resolver sees read operation before denied external edit");
        return ava::permissions::PermissionResolution::Deny;
      }};
  auto outside_edit_denied = ava::tools::edit_file(edit_deny_context, outside_path, "outside", "bad");
  auto outside_after_denied_edit = ava::tools::read_file(allow_context, outside_path);
  expect(!outside_edit_denied && edit_prompts == 1 && outside_after_denied_edit && outside_after_denied_edit->content == "outside content" &&
             outside_edit_denied.error().format().find("resolution: deny") != std::string::npos,
         "edit_file leaves content unchanged when external read permission is denied");

  int edit_fail_prompts = 0;
  ava::tools::ToolContext edit_fail_context{
      .workspace_dir = workspace,
      .mode = ava::agent::Mode::Build,
      .permission_resolver =
          [&edit_fail_prompts](ava::permissions::PermissionPrompt const& prompt) -> ava::core::Result<ava::permissions::PermissionResolutionDecision> {
        ++edit_fail_prompts;
        expect(prompt.operation == ava::permissions::Operation::ReadFile, "edit_file failure resolver sees read operation before external edit");
        return std::unexpected(ava::core::Error(ava::core::ErrorCategory::Io, "resolver failed"));
      }};
  auto outside_edit_failed = ava::tools::edit_file(edit_fail_context, outside_path, "outside", "bad");
  auto outside_after_failed_edit = ava::tools::read_file(allow_context, outside_path);
  expect(!outside_edit_failed && edit_fail_prompts == 1 && outside_after_failed_edit && outside_after_failed_edit->content == "outside content" &&
             outside_edit_failed.error().format().find("resolution: resolver_failed") != std::string::npos,
         "edit_file fails closed when resolver fails and leaves content unchanged");

  std::vector<ava::permissions::PermissionPrompt> edit_diff_denials;
  ava::tools::ToolContext edit_diff_deny_context{
      .workspace_dir = workspace,
      .mode = ava::agent::Mode::Build,
      .permission_resolver =
          [&edit_diff_denials](ava::permissions::PermissionPrompt const& prompt) -> ava::core::Result<ava::permissions::PermissionResolutionDecision> {
        edit_diff_denials.push_back(prompt);
        if (prompt.operation == ava::permissions::Operation::ReadFile)
        {
          return ava::permissions::PermissionResolution::Allow;
        }
        return ava::permissions::PermissionResolution::Deny;
      }};
  auto outside_edit_diff_denied = ava::tools::edit_file(edit_diff_deny_context, outside_path, "outside", "external");
  auto outside_after_diff_denied_edit = ava::tools::read_file(allow_context, outside_path);
  expect(!outside_edit_diff_denied && edit_diff_denials.size() == 2 && outside_after_diff_denied_edit &&
             outside_after_diff_denied_edit->content == "outside content" &&
             outside_edit_diff_denied.error().format().find("resolution: deny") != std::string::npos,
         "edit_file leaves content unchanged when external edit permission is denied after read approval");
  if (edit_diff_denials.size() >= 2)
  {
    expect(edit_diff_denials[0].operation == ava::permissions::Operation::ReadFile && edit_diff_denials[1].operation == ava::permissions::Operation::EditFile &&
               edit_diff_denials[1].diff_preview.find("-outside content") != std::string::npos &&
               edit_diff_denials[1].diff_preview.find("+external content") != std::string::npos && !edit_diff_denials[1].diff_truncated,
           "edit_file includes backend-generated diff preview before denied external edit approval");
  }

  std::vector<ava::permissions::Operation> edit_allow_prompts;
  ava::tools::ToolContext edit_allow_context{
      .workspace_dir = workspace,
      .mode = ava::agent::Mode::Build,
      .permission_resolver =
          [&edit_allow_prompts](ava::permissions::PermissionPrompt const& prompt) -> ava::core::Result<ava::permissions::PermissionResolutionDecision> {
        edit_allow_prompts.push_back(prompt.operation);
        return ava::permissions::PermissionResolution::Allow;
      }};
  auto outside_edit_allowed = ava::tools::edit_file(edit_allow_context, outside_path, "outside", "external");
  auto outside_after_allowed_edit = ava::tools::read_file(allow_context, outside_path);
  expect(outside_edit_allowed && edit_allow_prompts.size() == 2 && edit_allow_prompts[0] == ava::permissions::Operation::ReadFile &&
             edit_allow_prompts[1] == ava::permissions::Operation::EditFile && outside_after_allowed_edit &&
             outside_after_allowed_edit->content == "external content",
         "edit_file resolves external read permission before edit permission");
}

void test_permission_audit_persistence()
{
  auto const root = temp_root() / "permission-audit";
  std::error_code remove_error;
  std::filesystem::remove_all(root, remove_error);
  auto const workspace = root / "workspace";
  std::filesystem::create_directories(workspace);

  auto const allowed_path = workspace / "allowed.txt";
  {
    std::ofstream file(allowed_path, std::ios::binary | std::ios::trunc);
    file << "allowed";
  }
  auto const secret_path = workspace / ".env";
  {
    std::ofstream file(secret_path, std::ios::binary | std::ios::trunc);
    file << "secret";
  }
  auto const outside_path = root / "outside.txt";
  {
    std::ofstream file(outside_path, std::ios::binary | std::ios::trunc);
    file << "outside";
  }

  ava::session::SessionStore store(ava::session::SessionStoreOptions{.root_dir = root / "sessions", .workspace_dir = workspace, .session_id = "audit"});
  auto sink = [&store](ava::tools::PermissionAuditEvent const& event) -> ava::core::VoidResult { return append_permission_audit_for_test(store, event); };
  ava::tools::ToolContext const context{.workspace_dir = workspace, .mode = ava::agent::Mode::Build, .permission_audit_sink = sink};

  auto allowed = ava::tools::read_file(context, allowed_path);
  expect(allowed && allowed->content == "allowed", "permission audit allows normal read");
  auto loaded = store.load();
  auto audits = loaded ? permission_entries(*loaded) : std::vector<ava::session::SessionEntry>{};
  expect(audits.size() == 1, "allowed policy decision appends one audit entry");
  if (!audits.empty())
  {
    expect(ava::core::json::string_field(audits[0].data_json, "operation") == "read" &&
               ava::core::json::string_field(audits[0].data_json, "permission_request_id").value_or("").starts_with("permreq_") &&
               ava::core::json::string_field(audits[0].data_json, "action") == "allow" &&
               ava::core::json::string_field(audits[0].data_json, "resolution") == "allow" &&
               ava::core::json::string_field(audits[0].data_json, "resolution_source") == "policy" &&
               ava::core::json::string_field(audits[0].data_json, "risk") == "low" &&
               ava::core::json::string_field(audits[0].data_json, "target_path") == allowed_path.string(),
           "allowed audit records policy resolution, risk, and target path");
  }

  auto denied = ava::tools::read_file(context, secret_path);
  expect(!denied, "permission audit denied read still fails closed");
  loaded = store.load();
  audits = loaded ? permission_entries(*loaded) : std::vector<ava::session::SessionEntry>{};
  expect(audits.size() == 2, "denied policy decision appends an audit entry");
  if (audits.size() >= 2)
  {
    expect(ava::core::json::string_field(audits[1].data_json, "action") == "deny" &&
               ava::core::json::string_field(audits[1].data_json, "permission_request_id").value_or("").starts_with("permreq_") &&
               ava::core::json::string_field(audits[1].data_json, "resolution") == "deny" &&
               ava::core::json::string_field(audits[1].data_json, "resolution_source") == "policy" &&
               ava::core::json::string_field(audits[1].data_json, "risk") == "critical",
           "denied audit records policy denial and critical risk without resolver");
  }

  int prompts = 0;
  std::string prompt_permission_request_id;
  ava::tools::ToolContext const resolving_context{
      .workspace_dir = workspace,
      .mode = ava::agent::Mode::Build,
      .permission_resolver = [&prompts, &prompt_permission_request_id](
                                 ava::permissions::PermissionPrompt const& prompt) -> ava::core::Result<ava::permissions::PermissionResolutionDecision> {
        ++prompts;
        prompt_permission_request_id = prompt.permission_request_id;
        return ava::permissions::PermissionResolution::Allow;
      },
      .permission_audit_sink = sink};
  auto outside = ava::tools::read_file(resolving_context, outside_path);
  expect(outside && outside->content == "outside" && prompts == 1 && prompt_permission_request_id.starts_with("permreq_"),
         "permission audit preserves resolver-approved read behavior and stable request id");
  loaded = store.load();
  audits = loaded ? permission_entries(*loaded) : std::vector<ava::session::SessionEntry>{};
  expect(audits.size() == 4, "ask policy and resolver outcome append separate audit entries");
  if (audits.size() >= 4)
  {
    auto const ask_permission_request_id = ava::core::json::string_field(audits[2].data_json, "permission_request_id").value_or("");
    expect(ava::core::json::string_field(audits[2].data_json, "action") == "ask" && ask_permission_request_id.starts_with("permreq_") &&
               ask_permission_request_id == prompt_permission_request_id && !ava::core::json::string_field(audits[2].data_json, "resolution") &&
               ava::core::json::string_field(audits[2].data_json, "resolution_source") == "policy" &&
               ava::core::json::string_field(audits[2].data_json, "risk") == "high",
           "ask audit records policy request and high risk before resolver outcome");
    expect(ava::core::json::string_field(audits[3].data_json, "action") == "ask" &&
               ava::core::json::string_field(audits[3].data_json, "permission_request_id") == ask_permission_request_id &&
               ava::core::json::string_field(audits[3].data_json, "resolution") == "allow" &&
               ava::core::json::string_field(audits[3].data_json, "resolution_source") == "resolver" &&
               ava::core::json::string_field(audits[3].data_json, "risk") == "high",
           "ask audit records resolver allow outcome and original risk");
  }

  auto bash_denied = ava::tools::run_bash(context, "rm -rf important");
  expect(!bash_denied, "permission audit preserves bash policy denial");
  loaded = store.load();
  audits = loaded ? permission_entries(*loaded) : std::vector<ava::session::SessionEntry>{};
  expect(audits.size() == 5, "bash policy decision appends command audit entry");
  if (audits.size() >= 5)
  {
    expect(ava::core::json::string_field(audits[4].data_json, "operation") == "bash" &&
               ava::core::json::string_field(audits[4].data_json, "command") == "rm -rf important" &&
               ava::core::json::string_field(audits[4].data_json, "risk") == "critical" && !ava::core::json::string_field(audits[4].data_json, "target_path"),
           "bash audit records command risk without path-only target field");
  }

  ava::tools::ToolContext const denying_context{
      .workspace_dir = workspace,
      .mode = ava::agent::Mode::Build,
      .permission_resolver = [](ava::permissions::PermissionPrompt const&) -> ava::core::Result<ava::permissions::PermissionResolutionDecision> {
        return ava::permissions::PermissionResolutionDecision{ava::permissions::PermissionResolution::Deny, "manual resolver denial"};
      },
      .permission_audit_sink = sink};
  auto resolver_denied = ava::tools::read_file(denying_context, outside_path);
  expect(!resolver_denied, "permission audit preserves resolver denial behavior");
  loaded = store.load();
  audits = loaded ? permission_entries(*loaded) : std::vector<ava::session::SessionEntry>{};
  expect(audits.size() == 7, "resolver denial appends ask and outcome audit entries");
  if (audits.size() >= 7)
  {
    expect(ava::core::json::string_field(audits[6].data_json, "resolution") == "deny" &&
               ava::core::json::string_field(audits[6].data_json, "resolution_source") == "resolver" &&
               ava::core::json::string_field(audits[6].data_json, "resolution_reason") == "manual resolver denial",
           "resolver denial audit records the client-supplied resolution reason");
  }

  auto const exported = ava::session::format_session_markdown(audits);
  expect(exported.find("## Permission Decision") != std::string::npos && exported.find("\"operation\":\"read\"") != std::string::npos &&
             exported.find("\"risk\":\"high\"") != std::string::npos && exported.find("\"resolution_source\":\"resolver\"") != std::string::npos &&
             exported.find("\"resolution_reason\":\"manual resolver denial\"") != std::string::npos,
         "session export includes permission decision audit data");
}

}  // namespace

void run_tools_file_tests()
{
  test_file_tools();
  test_permission_audit_persistence();
}
