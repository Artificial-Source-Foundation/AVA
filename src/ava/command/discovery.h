#pragma once

#include "ava/command/command.h"

namespace ava::command::detail {

struct SealedCommandContext
{
  std::filesystem::path workspace;
  std::filesystem::path cwd;
  PathMetadata workspace_metadata;
  PathMetadata cwd_metadata;
  std::vector<CommandPathEntry> path_entries;

  AVA_DEBUG_PRINT_MEMBERS_ON
};

[[nodiscard]] ava::core::Result<SealedCommandContext> discover_command_context(CommandIntent const& intent, CommandBuildOptions const& options);
[[nodiscard]] ava::core::Result<ResolvedExecutable> resolve_executable(std::vector<std::string> const& argv, std::vector<CommandPathEntry> const& path_entries,
                                                                       std::filesystem::path const& cwd, std::filesystem::path const& workspace,
                                                                       std::filesystem::path const& trusted_home, CommandLimits const& limits);
// Unsafe, missing, symlinked, or outside-workspace recipe arguments return
// nullopt so policy can downgrade them from Standard without executing them.
[[nodiscard]] std::optional<PathMetadata> seal_recipe_path_argument(std::string_view value, std::filesystem::path const& cwd,
                                                                    std::filesystem::path const& workspace);
[[nodiscard]] ava::core::Result<bool> path_metadata_is_fresh(PathMetadata const& recorded);
[[nodiscard]] ava::core::Result<bool> executable_metadata_is_fresh(ExecutableMetadata const& recorded);

}  // namespace ava::command::detail
