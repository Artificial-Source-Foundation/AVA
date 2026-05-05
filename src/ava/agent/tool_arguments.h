#pragma once

#include <cstddef>
#include <filesystem>
#include <string>
#include <string_view>

#include "ava/core/result.h"
#include "ava/tools/file_tools.h"

namespace ava::agent {

[[nodiscard]] ava::core::Result<std::string> required_string_arg(std::string_view arguments, std::string_view field,
                                                                 std::string_view tool_name);
[[nodiscard]] ava::core::VoidResult reject_nul_arg(std::string_view value, std::string_view field,
                                                   std::string_view tool_name);
[[nodiscard]] ava::core::VoidResult reject_control_arg(std::string_view value, std::string_view field,
                                                       std::string_view tool_name);
[[nodiscard]] ava::core::VoidResult reject_control_value(std::string_view value, std::string_view field,
                                                         std::string_view message);
[[nodiscard]] ava::core::Result<std::string> required_text_arg(std::string_view arguments, std::string_view field,
                                                               std::string_view tool_name);
[[nodiscard]] ava::core::Result<std::string> required_safe_string_arg(std::string_view arguments,
                                                                      std::string_view field,
                                                                      std::string_view tool_name);
[[nodiscard]] std::filesystem::path workspace_path(ava::tools::ToolContext const& context, std::string_view path);
[[nodiscard]] std::size_t optional_size_arg(std::string_view arguments, std::string_view field, std::size_t fallback,
                                            std::size_t maximum);
[[nodiscard]] ava::core::Result<bool> optional_bool_arg(std::string_view arguments, std::string_view field,
                                                        bool fallback, std::string_view tool_name);
[[nodiscard]] ava::core::VoidResult reject_provider_no_ignore(std::string_view arguments, std::string_view tool_name);

}  // namespace ava::agent
