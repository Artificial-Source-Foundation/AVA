#include "sys.h"
#include "tests/support/test_harness.h"
#include "ava/agent/mode.h"
#include "ava/tools/bash_tool.h"
#include "ava/tools/file_tools.h"
#include "ava/tools/mutation_queue.h"
#include "ava/tools/secure_workspace.h"
#include "ava/session/export.h"
#include "ava/session/session_store.h"
#include "ava/permissions/permission.h"
#include "ava/core/json.h"

#include <algorithm>
#include <array>
#include <cerrno>
#include <charconv>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <map>
#include <memory>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>
#include <dirent.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

namespace {

std::string read_text_file_for_test(std::filesystem::path const& path)
{
  std::ifstream file(path, std::ios::binary);
  std::ostringstream out;
  out << file.rdbuf();
  return out.str();
}

bool has_secure_write_temp_for(std::filesystem::path const& directory, std::string_view target_filename = {})
{
  auto const prefix = target_filename.empty() ? std::string{} : "." + std::string(target_filename) + ".ava-write-";
  std::error_code iter_error;
  for (std::filesystem::directory_iterator it(directory, iter_error), end; !iter_error && it != end; it.increment(iter_error))
  {
    auto const name = it->path().filename().string();
    if ((prefix.empty() && name.find(".ava-write-") != std::string::npos) || (!prefix.empty() && name.starts_with(prefix)))
      return true;
  }
  return false;
}

bool has_error_context(ava::core::Error const& error, std::string_view key, std::string_view value)
{
  return std::ranges::any_of(error.context(), [key, value](ava::core::ErrorContext const& item) { return item.key == key && item.value == value; });
}

#ifdef __linux__
std::optional<std::vector<int>> proc_open_file_descriptors()
{
  DIR* directory = ::opendir("/proc/self/fd");
  if (directory == nullptr)
    return std::nullopt;
  int const listing_fd = ::dirfd(directory);
  if (listing_fd < 0)
  {
    ::closedir(directory);
    return std::nullopt;
  }

  std::vector<int> descriptors;
  errno = 0;
  while (auto* entry = ::readdir(directory))
  {
    auto const* begin = entry->d_name;
    auto const* end = begin + std::strlen(begin);
    int descriptor = -1;
    auto const parsed = std::from_chars(begin, end, descriptor);
    if (parsed.ec == std::errc{} && parsed.ptr == end && descriptor != listing_fd)
      descriptors.push_back(descriptor);
  }
  int const read_error = errno;
  ::closedir(directory);
  if (read_error != 0)
    return std::nullopt;

  std::ranges::sort(descriptors);
  return descriptors;
}

std::optional<std::vector<int>> added_file_descriptors(std::vector<int> const& before)
{
  auto after = proc_open_file_descriptors();
  if (!after)
    return std::nullopt;
  std::vector<int> added;
  std::ranges::set_difference(*after, before, std::back_inserter(added));
  return added;
}
#endif

class MemoryExactFileAccess final : public ava::tools::ExactFileAccess
{
 public:
  [[nodiscard]] bool supports_read_text_file() const noexcept override { return supports_reads; }
  [[nodiscard]] bool supports_write_text_file() const noexcept override { return supports_writes; }

  [[nodiscard]] ava::core::Result<std::string> read_text_file(std::filesystem::path const& path, ava::tools::ToolIoCancelCallback) const override
  {
    ++read_calls;
    paths.push_back(path);
    if (fail_reads)
      return std::unexpected(ava::core::Error(ava::core::ErrorCategory::Io, "injected exact read failed"));
    auto found = files.find(path);
    if (found == files.end())
      return std::unexpected(ava::core::Error(ava::core::ErrorCategory::NotFound, "injected exact file is missing"));
    return found->second;
  }

  [[nodiscard]] ava::core::VoidResult write_text_file(std::filesystem::path const& path, std::string_view content,
                                                      ava::tools::ToolIoCancelCallback) const override
  {
    ++write_calls;
    paths.push_back(path);
    if (fail_writes)
      return std::unexpected(ava::core::Error(ava::core::ErrorCategory::Io, "injected exact write failed"));
    files[path] = std::string(content);
    return {};
  }

  mutable std::map<std::filesystem::path, std::string> files;
  mutable std::vector<std::filesystem::path> paths;
  mutable int read_calls = 0;
  mutable int write_calls = 0;
  bool supports_reads = true;
  bool supports_writes = true;
  bool fail_reads = false;
  bool fail_writes = false;
};

void test_mutation_queue_cleans_drained_path_entries()
{
  std::error_code remove_error;
  std::filesystem::remove_all(temp_root(), remove_error);
  auto const workspace = temp_root() / "mutation-queue";
  std::filesystem::create_directories(workspace);

  ava::tools::MutationQueue queue;
  expect(queue.tracked_path_count() == 0, "mutation queue starts without tracked path entries");

  auto const single_path = workspace / "single.txt";
  {
    [[maybe_unused]] auto lock = queue.lock_path(single_path);
    expect(queue.tracked_path_count() == 1, "mutation queue tracks a locked path entry");
  }
  expect(queue.tracked_path_count() == 0, "mutation queue removes a path entry after queued work drains");

  std::array const paths{workspace / "multi-a.txt", workspace / "." / "multi-a.txt", workspace / "multi-b.txt"};
  {
    [[maybe_unused]] auto lock = queue.lock_paths(paths);
    expect(queue.tracked_path_count() == 2, "mutation queue deduplicates normalized aliases while locked");
  }
  expect(queue.tracked_path_count() == 0, "mutation queue removes deduped multi-path entries after queued work drains");
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
    expect(read->truncated && read->byte_limited && !read->line_limited && read->totals_known && read->total_bytes == 11 && read->total_lines == 1 &&
               read->output_lines == 1,
           "local read_file reports byte-cap truncation with exact full-file totals");
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
  expect(ranged && ranged->content == "two\nthree\n" && ranged->start_line == 2 && ranged->end_line == 3 && ranged->output_lines == 2 && ranged->totals_known &&
             ranged->total_bytes == 19 && ranged->total_lines == 4 && ranged->line_limited && !ranged->byte_limited && ranged->next_offset_line == 4,
         "local read_file supports line offset and continuation metadata with exact full-file totals");

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

  auto const audit_write_path = workspace / "audit-write.txt";
  std::vector<ava::tools::PermissionAuditEvent> write_audits;
  ava::tools::ToolContext const audit_write_context{
      .workspace_dir = workspace,
      .mode = ava::agent::Mode::Build,
      .permission_resolver = [](ava::permissions::PermissionPrompt const& prompt) -> ava::core::Result<ava::permissions::PermissionResolutionDecision> {
        expect(prompt.tool_name == "write_file", "write_file permission prompt carries write_file tool metadata");
        return ava::permissions::PermissionResolution::Allow;
      },
      .permission_audit_sink = [&write_audits](ava::tools::PermissionAuditEvent const& event) -> ava::core::VoidResult {
        write_audits.push_back(event);
        return {};
      },
      .permission_tool_name = "write_file",
      .current_tool_name = "write_file"};
  auto audited_write = ava::tools::write_file(audit_write_context, audit_write_path, "written");
  expect(audited_write && std::ranges::any_of(write_audits,
                                              [](ava::tools::PermissionAuditEvent const& event) {
                                                return event.operation == ava::permissions::Operation::EditFile && event.tool_name == "write_file";
                                              }),
         "write_file audits its file-mutation permission with write_file tool metadata");

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
    auto linked_source_read = ava::tools::read_file(build_context, source_link);
    expect(!linked_source_read && linked_source_read.error().message().find("symlink") != std::string::npos,
           "read_file rejects symlinks before opening file contents");
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
  expect(diffed_write && diffed_write->diff.find("-old line") != std::string::npos && diffed_write->diff.find("+new line") != std::string::npos &&
             !diffed_write->diff_truncated,
         "write_file returns the backend-generated unified diff after successful mutation");

  auto const large_path = workspace / "large.txt";
  {
    std::ofstream large(large_path, std::ios::binary | std::ios::trunc);
    large << std::string(8192, 'x');
  }
  auto large_read = ava::tools::read_file(build_context, large_path, ava::tools::ReadOptions{.max_bytes = 16});
  expect(large_read && large_read->content.size() == 16 && large_read->total_bytes == 8192 && large_read->byte_limited && large_read->output_lines == 1,
         "read_file bounds output while counting bytes");

  auto const oversized_path = workspace / "oversized.txt";
  {
    std::ofstream oversized(oversized_path, std::ios::binary | std::ios::trunc);
    oversized.seekp(10 * 1024 * 1024);
    oversized.put('x');
  }
  auto oversized_read = ava::tools::read_file(build_context, oversized_path, ava::tools::ReadOptions{.max_bytes = 16});
  expect(!oversized_read && oversized_read.error().message().find("too large") != std::string::npos,
         "read_file rejects oversized files instead of scanning unbounded input");

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

void test_secure_workspace_staged_write_contracts()
{
  auto const root = temp_root() / "secure-workspace-staged-write";
  std::error_code cleanup;
  std::filesystem::remove_all(root, cleanup);
  auto const workspace = root / "workspace";
  std::filesystem::create_directories(workspace);
  auto secure = ava::tools::SecureWorkspace::open(std::filesystem::canonical(workspace));
  expect(secure.has_value(), "staged write contract test anchors a canonical workspace");
  if (!secure)
    return;

#ifdef __linux__
  auto const fd_baseline = proc_open_file_descriptors();
  expect(fd_baseline.has_value(), "staged write contract test can enumerate /proc/self/fd without counting its own directory descriptor");
  if (!fd_baseline)
    return;
  auto const fds_restored = [&fd_baseline] {
    auto current = proc_open_file_descriptors();
    return current && *current == *fd_baseline;
  };
#else
  auto const fds_restored = [] { return true; };
  expect(true, "staged write descriptor accounting explicitly requires Linux /proc/self/fd");
#endif

  auto const abandoned_path = workspace / "abandoned.txt";
  bool abandoned_held_parent_fd = true;
  {
    auto abandoned = (*secure)->stage_write(abandoned_path, "abandoned bytes");
    expect(abandoned.has_value(), "staged write can be abandoned after staging");
#ifdef __linux__
    if (abandoned)
    {
      auto added = added_file_descriptors(*fd_baseline);
      abandoned_held_parent_fd = added && added->size() == 1;
    }
#endif
  }
  expect(abandoned_held_parent_fd && !std::filesystem::exists(abandoned_path) && !has_secure_write_temp_for(workspace, "abandoned.txt") && fds_restored(),
         "abandoned staged write removes its temp and restores the file descriptor baseline");

  auto const committed_path = workspace / "committed.txt";
  bool successful_commit_contract = false;
  {
    auto staged = (*secure)->stage_write(committed_path, "committed bytes");
    if (staged)
    {
      auto committed = staged->commit();
      successful_commit_contract = committed.has_value() && staged->target_changed() && staged->path() == committed_path && staged->bytes_written() == 15;
    }
  }
  expect(successful_commit_contract && read_text_file_for_test(committed_path) == "committed bytes" && !has_secure_write_temp_for(workspace, "committed.txt") &&
             fds_restored(),
         "successful staged commit publishes bytes, cleans its temp, and restores the file descriptor baseline");

  auto const mode_path = workspace / "mode-preserved.txt";
  {
    std::ofstream original(mode_path, std::ios::binary | std::ios::trunc);
    original << "mode original";
  }
  bool const mode_setup = ::chmod(mode_path.c_str(), 0640) == 0;
  auto mode_stage = (*secure)->stage_write(mode_path, "mode replacement");
  bool mode_committed = false;
  if (mode_stage)
  {
    auto committed = mode_stage->commit();
    mode_committed = committed.has_value() && mode_stage->target_changed();
  }
  struct stat mode_status{};
  bool const mode_stat = ::stat(mode_path.c_str(), &mode_status) == 0;
  expect(mode_setup && mode_committed && read_text_file_for_test(mode_path) == "mode replacement" && mode_stat && (mode_status.st_mode & 07777) == 0640 &&
             !has_secure_write_temp_for(workspace, "mode-preserved.txt") && fds_restored(),
         "descriptor-secure stage and commit preserve an existing file's mode");

  auto const move_source_path = workspace / "move-source.txt";
  auto const move_destination_path = workspace / "move-destination.txt";
  bool move_assignment_contract = false;
  {
    auto move_source = (*secure)->stage_write(move_source_path, "moved bytes");
    auto move_destination = (*secure)->stage_write(move_destination_path, "discarded bytes");
    if (move_source && move_destination)
    {
      bool two_staged_parent_fds = true;
#ifdef __linux__
      auto two_added = added_file_descriptors(*fd_baseline);
      two_staged_parent_fds = two_added && two_added->size() == 2;
#endif
      bool const both_temps = has_secure_write_temp_for(workspace, "move-source.txt") && has_secure_write_temp_for(workspace, "move-destination.txt");
      *move_destination = std::move(*move_source);
      bool sole_staged_parent_fd = true;
#ifdef __linux__
      auto sole_added = added_file_descriptors(*fd_baseline);
      sole_staged_parent_fd = sole_added && sole_added->size() == 1;
#endif
      bool const old_temp_cleaned = has_secure_write_temp_for(workspace, "move-source.txt") && !has_secure_write_temp_for(workspace, "move-destination.txt");
      auto moved_from_commit = move_source->commit();
      auto moved_commit = move_destination->commit();
      move_assignment_contract = two_staged_parent_fds && both_temps && sole_staged_parent_fd && old_temp_cleaned && !moved_from_commit && moved_commit &&
                                 move_destination->path() == move_source_path && move_destination->target_changed();
    }
  }
  expect(move_assignment_contract && read_text_file_for_test(move_source_path) == "moved bytes" && !std::filesystem::exists(move_destination_path) &&
             !has_secure_write_temp_for(workspace) && fds_restored(),
         "staged write move assignment cleans its old temp and transfers sole ownership without leaking descriptors");

  auto const rename_failure_target = workspace / "rename-failure.txt";
  auto const preserved_original = workspace / "rename-failure-original.txt";
  auto const non_target = workspace / "rename-failure-non-target.txt";
  {
    std::ofstream original(rename_failure_target, std::ios::binary | std::ios::trunc);
    original << "rename original";
    std::ofstream unrelated(non_target, std::ios::binary | std::ios::trunc);
    unrelated << "unrelated safe bytes";
  }
  bool rename_failure_contract = false;
  {
    auto staged = (*secure)->stage_write(rename_failure_target, "rename replacement");
    if (staged)
    {
      std::error_code rename_error;
      std::filesystem::rename(rename_failure_target, preserved_original, rename_error);
      std::error_code directory_error;
      bool const directory_created = std::filesystem::create_directory(rename_failure_target, directory_error);
      if (directory_created)
      {
        std::ofstream sentinel(rename_failure_target / "sentinel.txt", std::ios::binary | std::ios::trunc);
        sentinel << "directory sentinel";
      }
      auto committed = staged->commit();
      rename_failure_contract = !rename_error && directory_created && !directory_error && !committed &&
                                committed.error().message().find("staged write commit") != std::string::npos &&
                                has_error_context(committed.error(), "path", rename_failure_target.string()) && !staged->target_changed() &&
                                has_secure_write_temp_for(workspace, "rename-failure.txt") &&
                                read_text_file_for_test(preserved_original) == "rename original" &&
                                read_text_file_for_test(rename_failure_target / "sentinel.txt") == "directory sentinel" &&
                                read_text_file_for_test(non_target) == "unrelated safe bytes";
    }
  }
  expect(rename_failure_contract && !has_secure_write_temp_for(workspace, "rename-failure.txt") && fds_restored(),
         "failed staged rename reports operation/path context, preserves original and non-target data, cleans up, and restores descriptors");

#ifdef __linux__
  auto const sync_parent = workspace / "sync-parent";
  std::filesystem::create_directories(sync_parent);
  auto const sync_target = sync_parent / "sync-target.txt";
  {
    std::ofstream original(sync_target, std::ios::binary | std::ios::trunc);
    original << "sync original";
  }
  bool parent_sync_failure_contract = false;
  {
    auto before_stage = proc_open_file_descriptors();
    auto staged = (*secure)->stage_write(sync_target, "sync replacement");
    if (before_stage && staged)
    {
      auto added = added_file_descriptors(*before_stage);
      if (added && added->size() == 1)
      {
        int const path_parent = ::open(sync_parent.c_str(), O_PATH | O_DIRECTORY | O_CLOEXEC);
        bool const parent_replaced = path_parent >= 0 && ::dup2(path_parent, added->front()) == added->front();
        if (path_parent >= 0)
          ::close(path_parent);
        if (parent_replaced)
        {
          auto committed = staged->commit();
          parent_sync_failure_contract = !committed && committed.error().message().find("write parent sync") != std::string::npos &&
                                         has_error_context(committed.error(), "path", sync_target.string()) &&
                                         has_error_context(committed.error(), "cause", std::generic_category().message(EBADF)) &&
                                         has_error_context(committed.error(), "target_changed", "true") && staged->target_changed();
        }
      }
    }
  }
  expect(parent_sync_failure_contract && read_text_file_for_test(sync_target) == "sync replacement" &&
             !has_secure_write_temp_for(sync_parent, "sync-target.txt") && fds_restored(),
         "post-rename parent fsync failure reports target_changed while leaving replacement bytes visible and restoring descriptors");
#else
  expect(true, "post-rename parent fsync fault injection is explicitly Linux-only because it requires O_PATH and /proc/self/fd");
#endif
}

void test_injected_exact_file_access()
{
  auto const root = temp_root() / "injected-exact-file-access";
  std::error_code cleanup;
  std::filesystem::remove_all(root, cleanup);
  auto const workspace = root / "workspace";
  auto const outside = root / "outside";
  std::filesystem::create_directories(workspace);
  std::filesystem::create_directories(outside);
  auto secure = ava::tools::SecureWorkspace::open(std::filesystem::canonical(workspace));
  expect(secure.has_value(), "injected exact file test anchors its workspace");
  if (!secure)
    return;

  auto adapter = std::make_shared<MemoryExactFileAccess>();
  auto const remote_path = std::filesystem::canonical(workspace) / "remote.txt";
  adapter->files[remote_path] = "one\ntwo\nthree\n";
  ava::tools::ToolContext context{
      .workspace_dir = std::filesystem::canonical(workspace), .mode = ava::agent::Mode::Build, .secure_workspace = *secure, .exact_file_access = adapter};

  auto read = ava::tools::read_file(context, remote_path, ava::tools::ReadOptions{.max_bytes = 1024, .offset_line = 2, .max_lines = 1});
  auto write = ava::tools::write_file(context, workspace / "created.txt", "created remotely");
  auto edit = ava::tools::edit_file(context, remote_path, "two", "changed");
  expect(read && read->content == "two\n" && read->line_limited && !read->totals_known && read->total_bytes == 0 && read->total_lines == 0 && write && edit &&
             adapter->files[remote_path] == "one\nchanged\nthree\n" &&
             adapter->files[std::filesystem::canonical(workspace) / "created.txt"] == "created remotely" && !std::filesystem::exists(workspace / "created.txt"),
         "injected exact access keeps bounded window totals unknown while coherently handling writes and edits without local I/O");

  auto const local_only = workspace / "local-only.txt";
  {
    std::ofstream file(local_only, std::ios::binary | std::ios::trunc);
    file << "must not fall back";
  }
  adapter->fail_reads = true;
  auto failed = ava::tools::read_file(context, local_only);
  expect(!failed && failed.error().message() == "injected exact read failed", "an installed exact adapter failure never falls back to readable local bytes");
  adapter->fail_reads = false;

  adapter->supports_writes = false;
  int partial_permission_requests = 0;
  ava::tools::ToolContext partial_context = context;
  partial_context.require_explicit_file_permissions = true;
  partial_context.permission_resolver = [&](ava::permissions::PermissionPrompt const&) -> ava::core::Result<ava::permissions::PermissionResolutionDecision> {
    ++partial_permission_requests;
    return ava::permissions::PermissionResolution::Allow;
  };
  auto const partial_calls_before = adapter->read_calls + adapter->write_calls;
  auto partial_edit = ava::tools::edit_file(partial_context, remote_path, "two", "partial");
  expect(!partial_edit && partial_edit.error().message().find("capabilities are partial") != std::string::npos && partial_permission_requests == 0 &&
             adapter->read_calls + adapter->write_calls == partial_calls_before && adapter->files[remote_path] == "one\nchanged\nthree\n",
         "partial exact-file capabilities reject edit_file before permissions or I/O");
  adapter->supports_writes = true;

  auto const calls_before_denial = adapter->read_calls + adapter->write_calls;
  auto outside_read = ava::tools::read_file(context, outside / "secret.txt");
  ava::tools::ToolContext plan_context = context;
  plan_context.mode = ava::agent::Mode::Plan;
  auto denied_write = ava::tools::write_file(plan_context, workspace / "source.cpp", "int changed;\n");
  expect(!outside_read && !denied_write && adapter->read_calls + adapter->write_calls == calls_before_denial,
         "workspace-root and permission denials happen before injected exact-file callbacks");
}

void test_secure_workspace_file_tools()
{
  auto const root = temp_root() / "secure-workspace-files";
  std::error_code cleanup;
  std::filesystem::remove_all(root, cleanup);
  auto const workspace = root / "workspace";
  auto const outside = root / "outside";
  std::filesystem::create_directories(workspace);
  std::filesystem::create_directories(outside);
  {
    std::ofstream file(outside / "secret.txt", std::ios::binary | std::ios::trunc);
    file << "outside secret";
  }
  auto secure = ava::tools::SecureWorkspace::open(std::filesystem::canonical(workspace));
  expect(secure.has_value(), "secure workspace anchors a canonical root descriptor");
  if (!secure)
    return;
  ava::tools::ToolContext secure_context{.workspace_dir = std::filesystem::canonical(workspace), .mode = ava::agent::Mode::Build, .secure_workspace = *secure};

  auto nested_write = ava::tools::write_file(secure_context, workspace / "nested" / "deeper" / "note.txt", "alpha beta");
  auto nested_edit = ava::tools::edit_file(secure_context, workspace / "nested" / "deeper" / "note.txt", "beta", "gamma");
  auto nested_read = ava::tools::read_file(secure_context, workspace / "nested" / "deeper" / "note.txt");
  expect(nested_write && nested_edit && nested_read && nested_read->content == "alpha gamma",
         "secure workspace creates nested parents and preserves ordinary read/edit regressions");

  std::error_code symlink_error;
  std::filesystem::create_directory_symlink(outside, workspace / "linked-parent", symlink_error);
  if (!symlink_error)
  {
    auto linked_read = ava::tools::read_file(secure_context, workspace / "linked-parent" / "secret.txt");
    auto linked_write = ava::tools::write_file(secure_context, workspace / "linked-parent" / "created.txt", "escape");
    auto linked_edit = ava::tools::edit_file(secure_context, workspace / "linked-parent" / "secret.txt", "outside", "changed");
    expect(!linked_read && !linked_write && !linked_edit && !std::filesystem::exists(outside / "created.txt") &&
               read_text_file_for_test(outside / "secret.txt") == "outside secret",
           "secure file reads, writes, and edits reject symlinked parents without touching outside files");
  }

  std::filesystem::create_directories(workspace / "race-parent");
  std::filesystem::create_directories(outside / "race-parent");
  {
    std::ofstream inside_file(workspace / "race-parent" / "target.txt", std::ios::binary | std::ios::trunc);
    inside_file << "inside original";
    std::ofstream outside_file(outside / "race-parent" / "target.txt", std::ios::binary | std::ios::trunc);
    outside_file << "outside original";
  }
  int permission_requests = 0;
  ava::tools::ToolContext race_context{
      .workspace_dir = std::filesystem::canonical(workspace),
      .mode = ava::agent::Mode::Build,
      .permission_resolver = [&](ava::permissions::PermissionPrompt const& prompt) -> ava::core::Result<ava::permissions::PermissionResolutionDecision> {
        ++permission_requests;
        expect(prompt.target_path == workspace / "race-parent" / "target.txt", "secure permission identity is canonical and root-relative before the grant");
        std::filesystem::rename(workspace / "race-parent", workspace / "race-parent-original");
        std::error_code link_error;
        std::filesystem::create_directory_symlink(outside / "race-parent", workspace / "race-parent", link_error);
        expect(!link_error, "retarget race installs a parent symlink after permission validation");
        return ava::permissions::PermissionResolution::Allow;
      },
      .require_explicit_file_permissions = true,
      .secure_workspace = *secure};
  auto raced_write = ava::tools::write_file(race_context, workspace / "race-parent" / "target.txt", "attacker content");
  expect(!raced_write && permission_requests == 1 && read_text_file_for_test(outside / "race-parent" / "target.txt") == "outside original" &&
             read_text_file_for_test(workspace / "race-parent-original" / "target.txt") == "inside original",
         "retargeting a parent symlink after a grant fails closed at descriptor-anchored I/O");
}

void test_secure_workspace_symlinked_root()
{
  // Regression for a workspace whose configured path traverses an absolute
  // symlink (e.g. /home/user/projects/github -> /usr/src/projects_github).
  // The anchor descriptor is opened at startup and must follow such symlinks;
  // only the relative paths resolved against the anchor are contained.
  auto const root = temp_root() / "symlinked-root";
  std::error_code cleanup;
  std::filesystem::remove_all(root, cleanup);
  auto const real_target = root / "real-projects-github";
  auto const projects = root / "projects";
  auto const real_workspace = real_target / "ai-cli" / "AVA";
  std::filesystem::create_directories(real_workspace / "src");
  std::filesystem::create_directories(projects);
  {
    std::ofstream file(real_workspace / "src" / "main.cpp", std::ios::binary | std::ios::trunc);
    file << "int main()\n";
  }
  std::error_code link_error;
  std::filesystem::create_directory_symlink(real_target, projects / "github", link_error);
  expect(!link_error, "test creates absolute symlink in workspace path");
  if (link_error)
    return;
  auto const workspace_via_link = projects / "github" / "ai-cli" / "AVA";

  auto secure = ava::tools::SecureWorkspace::open(workspace_via_link);
  expect(secure.has_value(), "secure workspace opens a root path that traverses an absolute symlink");
  if (!secure)
    return;
  ava::tools::ToolContext context{.workspace_dir = workspace_via_link, .mode = ava::agent::Mode::Build, .secure_workspace = *secure};

  auto read_back = ava::tools::read_file(context, workspace_via_link / "src" / "main.cpp");
  expect(read_back.has_value() && read_back->content == "int main()\n", "contained read works through a symlinked workspace root");

  auto const outside = root / "outside";
  std::filesystem::create_directories(outside);
  {
    std::ofstream file(outside / "secret.txt", std::ios::binary | std::ios::trunc);
    file << "outside secret";
  }
  std::filesystem::create_directory_symlink(outside, real_workspace / "linked-parent", link_error);
  if (!link_error)
  {
    auto escaping_read = ava::tools::read_file(context, workspace_via_link / "linked-parent" / "secret.txt");
    expect(!escaping_read, "contained reads still reject a symlink that escapes the anchored workspace");
  }
}

// Verify that the descriptor-anchored workspace follows non-escaping symlinks
// in the relative path (both intermediate directory components and the final
// file component) and rejects symlinks that escape the anchor — for reads,
// writes, and AllowMissing resolution alike. The anchor is the workspace root
// fd; every symlink below is evaluated against that single anchor.
void test_secure_workspace_symlink_containment()
{
  auto const root = temp_root() / "symlink-containment";
  std::error_code cleanup;
  std::filesystem::remove_all(root, cleanup);
  // Anchor: ws is the workspace root opened as the SecureWorkspace descriptor.
  auto const ws = root / "ws";
  auto const outside = root / "outside";
  std::filesystem::create_directories(ws / "real_dir");
  std::filesystem::create_directories(outside);
  {
    std::ofstream f(ws / "real_dir" / "file.txt", std::ios::binary | std::ios::trunc);
    f << "real content";
  }
  {
    std::ofstream f(ws / "real_file.txt", std::ios::binary | std::ios::trunc);
    f << "real file content";
  }
  {
    std::ofstream f(outside / "secret.txt", std::ios::binary | std::ios::trunc);
    f << "outside secret";
  }
  {
    std::ofstream f(outside / "file.txt", std::ios::binary | std::ios::trunc);
    f << "outside file";
  }

  auto secure = ava::tools::SecureWorkspace::open(ws);
  expect(secure.has_value(), "secure workspace opens the anchor directory");
  if (!secure)
    return;
  ava::tools::ToolContext context{.workspace_dir = ws, .mode = ava::agent::Mode::Build, .secure_workspace = *secure};

  std::error_code link_error;

  // linked_dir is a relative symlink to real_dir (both inside ws, no "..").
  // Anchor: ws. Symlink location: ws/linked_dir (intermediate component).
  // Symlink target: ws/real_dir — inside the anchor, so this must be ACCEPTED.
  std::filesystem::create_directory_symlink("real_dir", ws / "linked_dir", link_error);
  expect(!link_error, "create non-escaping intermediate directory symlink");
  if (!link_error)
  {
    auto read = ava::tools::read_file(context, ws / "linked_dir" / "file.txt");
    expect(read.has_value() && read->content == "real content",
           "read through non-escaping intermediate directory symlink is accepted");
  }

  // linked_file.txt is a relative symlink to real_file.txt (both inside ws).
  // Anchor: ws. Symlink location: ws/linked_file.txt (final component).
  // Symlink target: ws/real_file.txt — inside the anchor, so this must be ACCEPTED.
  std::filesystem::create_symlink("real_file.txt", ws / "linked_file.txt", link_error);
  expect(!link_error, "create non-escaping final-component file symlink");
  if (!link_error)
  {
    auto read = ava::tools::read_file(context, ws / "linked_file.txt");
    expect(read.has_value() && read->content == "real file content",
           "read through non-escaping final-component symlink is accepted");
  }

  // escape_dir is a relative symlink to ../outside (the ".." leaves ws).
  // Anchor: ws. Symlink location: ws/escape_dir (intermediate component).
  // Symlink target: root/outside — outside the anchor, so this must be REJECTED.
  std::filesystem::create_directory_symlink("../outside", ws / "escape_dir", link_error);
  expect(!link_error, "create escaping intermediate directory symlink");
  if (!link_error)
  {
    auto read = ava::tools::read_file(context, ws / "escape_dir" / "secret.txt");
    expect(!read, "read through escaping intermediate directory symlink is rejected");
  }

  // escape_file.txt is a relative symlink to ../outside/file.txt (leaves ws).
  // Anchor: ws. Symlink location: ws/escape_file.txt (final component).
  // Symlink target: root/outside/file.txt — outside the anchor, so this must be REJECTED.
  std::filesystem::create_symlink("../outside/file.txt", ws / "escape_file.txt", link_error);
  expect(!link_error, "create escaping final-component file symlink");
  if (!link_error)
  {
    auto read = ava::tools::read_file(context, ws / "escape_file.txt");
    expect(!read, "read through escaping final-component symlink is rejected");
  }

  // Writing a new file through a non-escaping intermediate directory symlink.
  // Anchor: ws. Symlink: ws/linked_dir -> real_dir (intermediate, non-escaping).
  // The parent directory resolves inside the anchor, so the write must be ACCEPTED.
  auto written = ava::tools::write_file(context, ws / "linked_dir" / "new.txt", "new content");
  expect(written.has_value(), "write through non-escaping intermediate directory symlink is accepted");

  // Writing a new file through an escaping intermediate directory symlink.
  // Anchor: ws. Symlink: ws/escape_dir -> ../outside (intermediate, escaping).
  // The parent directory escapes the anchor, so the write must be REJECTED.
  auto escape_write = ava::tools::write_file(context, ws / "escape_dir" / "new.txt", "bad");
  expect(!escape_write, "write through escaping intermediate directory symlink is rejected");

  // resolve(AllowMissing) through a non-escaping symlinked parent directory.
  // Anchor: ws. Symlink: ws/linked_dir -> real_dir (intermediate, non-escaping).
  // The file does not exist yet, but the parent path is contained, so this must be ACCEPTED.
  auto resolved = (*secure)->resolve(ws / "linked_dir" / "nonexistent.txt", ava::tools::SecureWorkspaceResolveMode::AllowMissing);
  expect(resolved.has_value() && !resolved->exists,
         "resolve AllowMissing through non-escaping symlinked parent is accepted");

  // resolve(AllowMissing) through an escaping symlinked parent directory.
  // Anchor: ws. Symlink: ws/escape_dir -> ../outside (intermediate, escaping).
  // The parent path escapes the anchor, so this must be REJECTED.
  auto escape_resolved = (*secure)->resolve(ws / "escape_dir" / "nonexistent.txt", ava::tools::SecureWorkspaceResolveMode::AllowMissing);
  expect(!escape_resolved, "resolve AllowMissing through escaping symlinked parent is rejected");
}

}  // namespace

void run_tools_file_tests()
{
  test_mutation_queue_cleans_drained_path_entries();
  test_file_tools();
  test_permission_audit_persistence();
  test_secure_workspace_staged_write_contracts();
  test_injected_exact_file_access();
  test_secure_workspace_file_tools();
  test_secure_workspace_symlinked_root();
  test_secure_workspace_symlink_containment();
}
