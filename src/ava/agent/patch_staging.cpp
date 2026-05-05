#include "ava/agent/patch_staging.h"

#include <utility>

#include "ava/core/ids.h"
#include "ava/tools/file_tools.h"

namespace ava::agent {
namespace {

std::filesystem::path unique_patch_temp_path(std::filesystem::path const& target)
{
  auto const stem = target.filename().string() + ".ava-patch-";
  for (int attempt = 0; attempt < 8; ++attempt) {
    auto candidate = target.parent_path() / (stem + ava::core::make_id("tmp") + ".tmp");
    std::error_code exists_error;
    if (!std::filesystem::exists(candidate, exists_error) && !exists_error) return candidate;
  }
  return target.parent_path() / (stem + ava::core::make_id("tmp") + ".tmp");
}

ava::core::VoidResult apply_existing_target_permissions_to_staged_file(std::filesystem::path const& target,
                                                                       std::filesystem::path const& temp)
{
  std::error_code status_error;
  auto const target_status = std::filesystem::status(target, status_error);
  if (status_error) {
    auto error = ava::core::Error(ava::core::ErrorCategory::Io, "failed to read target permissions for patch write");
    error.with_context("path", target.string());
    error.with_context("temp_path", temp.string());
    error.with_context("cause", status_error.message());
    return std::unexpected(std::move(error));
  }
  if (!std::filesystem::exists(target_status)) return {};

  std::error_code permissions_error;
  std::filesystem::permissions(temp, target_status.permissions(), std::filesystem::perm_options::replace,
                               permissions_error);
  if (permissions_error) {
    auto error = ava::core::Error(ava::core::ErrorCategory::Io, "failed to apply target permissions to patch write");
    error.with_context("path", target.string());
    error.with_context("temp_path", temp.string());
    error.with_context("cause", permissions_error.message());
    return std::unexpected(std::move(error));
  }
  return {};
}

}  // namespace

std::filesystem::path permission_dedupe_path(std::filesystem::path const& path)
{
  std::error_code error;
  auto const canonical = std::filesystem::weakly_canonical(path, error);
  if (!error) return canonical;
  return std::filesystem::absolute(path).lexically_normal();
}

void cleanup_staged_patch_writes(std::vector<StagedPatchWrite> const& writes, std::size_t start_index)
{
  for (std::size_t index = start_index; index < writes.size(); ++index) {
    ava::tools::remove_staged_file_best_effort(writes[index].temp);
  }
}

ava::core::Result<std::vector<StagedPatchWrite>> stage_patch_writes(
    ava::tools::ToolContext const& context, std::vector<std::filesystem::path> const& paths,
    std::map<std::filesystem::path, std::string> const& final_contents)
{
  std::vector<StagedPatchWrite> staged;
  staged.reserve(paths.size());

  for (auto const& path : paths) {
    auto const temp = unique_patch_temp_path(path);
    auto const content = final_contents.find(path);
    if (content == final_contents.end()) {
      auto error = ava::core::Error(ava::core::ErrorCategory::Unknown, "missing staged patch content");
      error.with_context("path", path.string());
      cleanup_staged_patch_writes(staged);
      return std::unexpected(std::move(error));
    }

    auto written = ava::tools::write_file(context, temp, content->second,
                                          ava::tools::WriteOptions{.permission_already_checked = true});
    if (!written) {
      ava::tools::remove_staged_file_best_effort(temp);
      cleanup_staged_patch_writes(staged);
      auto error = written.error();
      error.with_context("stage", "temporary_patch_write");
      error.with_context("target_path", path.string());
      error.with_context("temp_path", temp.string());
      return std::unexpected(std::move(error));
    }

    if (auto permissions = apply_existing_target_permissions_to_staged_file(path, temp); !permissions) {
      ava::tools::remove_staged_file_best_effort(temp);
      cleanup_staged_patch_writes(staged);
      auto error = permissions.error();
      error.with_context("stage", "temporary_patch_permissions");
      return std::unexpected(std::move(error));
    }

    staged.push_back(StagedPatchWrite{.target = path, .temp = temp, .bytes_written = written->bytes_written});
  }

  return staged;
}

ava::core::VoidResult commit_staged_patch_writes(std::vector<StagedPatchWrite> const& staged)
{
  for (std::size_t index = 0; index < staged.size(); ++index) {
    auto const& write = staged[index];
    if (auto committed = ava::tools::replace_file_with_staged_file(write.temp, write.target); !committed) {
      cleanup_staged_patch_writes(staged, index);
      auto error = committed.error();
      error.with_context("stage", "commit_staged_patch_write");
      error.with_context("atomicity", "all edits are staged before commit, but cross-file rename commit is not atomic");
      return std::unexpected(std::move(error));
    }
  }
  return {};
}

}  // namespace ava::agent
