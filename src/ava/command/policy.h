#pragma once

#include "ava/command/discovery.h"

namespace ava::command::detail {

[[nodiscard]] CommandClassification classify_raw_shell(ResolvedExecutable const& executable);
[[nodiscard]] CommandClassification classify_command(std::vector<std::string> const& argv, ResolvedExecutable const& executable,
                                                     std::filesystem::path const& cwd, std::filesystem::path const& workspace,
                                                     std::vector<WorkspaceScriptRecipe> const& workspace_recipes);

}  // namespace ava::command::detail
