#pragma once

#include <filesystem>
#include <string>

namespace ava::tools {

[[nodiscard]] std::filesystem::path normalize_workspace_root(const std::filesystem::path& workspace_root);
[[nodiscard]] std::filesystem::path enforce_workspace_path(
    const std::filesystem::path& workspace_root,
    const std::string& raw_path,
    const std::string& tool_name
);

}  // namespace ava::tools
