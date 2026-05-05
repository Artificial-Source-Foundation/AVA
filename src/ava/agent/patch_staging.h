#pragma once

#include <cstddef>
#include <filesystem>
#include <map>
#include <string>
#include <vector>

#include "ava/core/result.h"

namespace ava::tools {
struct ToolContext;
}  // namespace ava::tools

namespace ava::agent {

struct StagedPatchWrite {
  std::filesystem::path target;
  std::filesystem::path temp;
  std::size_t bytes_written = 0;
};

[[nodiscard]] std::filesystem::path permission_dedupe_path(std::filesystem::path const& path);
[[nodiscard]] ava::core::Result<std::vector<StagedPatchWrite>> stage_patch_writes(
    ava::tools::ToolContext const& context, std::vector<std::filesystem::path> const& paths,
    std::map<std::filesystem::path, std::string> const& final_contents);
void cleanup_staged_patch_writes(std::vector<StagedPatchWrite> const& writes, std::size_t start_index = 0);
[[nodiscard]] ava::core::VoidResult commit_staged_patch_writes(std::vector<StagedPatchWrite> const& staged);

}  // namespace ava::agent
