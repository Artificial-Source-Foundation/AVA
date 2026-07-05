#pragma once

#include <filesystem>
#include <string_view>

namespace ava::core {

[[nodiscard]] bool is_workspace_relative_process_arg(std::string_view value);
[[nodiscard]] std::filesystem::path safe_global_process_cwd(std::filesystem::path const& config_source, std::filesystem::path const& workspace_dir);

}  // namespace ava::core
