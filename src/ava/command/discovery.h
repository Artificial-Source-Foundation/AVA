#pragma once

#include "ava/command/command.h"

namespace ava::command::detail {

struct SealedCommandContext
{
  std::filesystem::path workspace;
  std::filesystem::path cwd;
  PathMetadata workspace_metadata;
  PathMetadata cwd_metadata;
  PathMetadata trusted_home_metadata;
  std::vector<std::filesystem::path> ava_authority_roots;
  std::vector<PathMetadata> ava_authority_root_metadata;
  SyntheticEnvironmentRoots synthetic_environment_roots;
  std::vector<CommandPathEntry> path_entries;

  AVA_DEBUG_PRINT_MEMBERS_ON
};

[[nodiscard]] ava::core::Result<SealedCommandContext> discover_command_context(CommandIntent const& intent, CommandBuildOptions const& options);
[[nodiscard]] ava::core::Result<ResolvedExecutable> resolve_executable(std::vector<std::string> const& argv, std::vector<CommandPathEntry> const& path_entries,
                                                                       std::filesystem::path const& cwd, std::filesystem::path const& workspace,
                                                                       std::filesystem::path const& trusted_home, CommandLimits const& limits,
                                                                       std::shared_ptr<ava::core::AnchorSet const> const& anchor_set);
// Unsafe, missing, symlinked, or outside-workspace recipe arguments return
// nullopt so policy can downgrade them from Standard without executing them.
[[nodiscard]] std::optional<PathMetadata> seal_recipe_path_argument(std::string_view value, std::filesystem::path const& cwd,
                                                                    std::filesystem::path const& workspace,
                                                                    std::shared_ptr<ava::core::AnchorSet const> const& anchor_set);
[[nodiscard]] ava::core::Result<bool> path_metadata_is_fresh(PathMetadata const& recorded, std::shared_ptr<ava::core::AnchorSet const> const& anchor_set);
// Trusted home is an authority/discovery boundary rather than mutable command
// content. Its final directory may gain or lose unrelated entries while its
// stable identity and safety properties remain exact.
[[nodiscard]] ava::core::Result<bool> trusted_home_metadata_is_fresh(PathMetadata const& recorded,
                                                                     std::shared_ptr<ava::core::AnchorSet const> const& anchor_set);
// User-tool freshness is valid only for a final logical path strictly
// beneath this exact sealed trusted home. Only that home's non-symlink ancestor
// may ignore directory link-count/ctime churn; all descendant and final
// metadata remains exact. An outside or malformed scope fails closed.
[[nodiscard]] bool is_sealed_user_tool_path(std::filesystem::path const& path, PathMetadata const& trusted_home);
[[nodiscard]] ava::core::Result<bool> user_tool_path_metadata_is_fresh(PathMetadata const& recorded, PathMetadata const& trusted_home,
                                                                       std::shared_ptr<ava::core::AnchorSet const> const& anchor_set);
[[nodiscard]] ava::core::Result<bool> executable_metadata_is_fresh(ExecutableMetadata const& recorded,
                                                                   std::shared_ptr<ava::core::AnchorSet const> const& anchor_set);
[[nodiscard]] ava::core::Result<bool> user_tool_executable_metadata_is_fresh(ExecutableMetadata const& recorded, PathMetadata const& trusted_home,
                                                                             std::shared_ptr<ava::core::AnchorSet const> const& anchor_set);

}  // namespace ava::command::detail
