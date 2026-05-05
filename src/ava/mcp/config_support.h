#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "ava/core/error.h"
#include "ava/core/result.h"
#include "ava/mcp/config.h"

namespace ava::mcp::detail {

inline constexpr std::size_t kMaxMcpConfigBytes = 256 * 1024;
inline constexpr std::size_t kMaxMcpArgBytes = 4096;

[[nodiscard]] ava::core::Error config_error(std::string message);
[[nodiscard]] bool has_forbidden_byte(std::string_view value);
[[nodiscard]] std::optional<bool> bool_field(std::string_view object, std::string_view key);
[[nodiscard]] std::optional<std::string> array_field(std::string_view object, std::string_view key);
[[nodiscard]] ava::core::Result<std::vector<std::string>> string_array_field(std::string_view object,
                                                                             std::string_view key);
[[nodiscard]] ava::core::Result<std::string> read_mcp_config_file(std::filesystem::path const& path);
[[nodiscard]] ava::core::Result<McpConfig> load_optional_mcp_config(std::filesystem::path const& path,
                                                                    McpServerScope scope);
[[nodiscard]] ava::core::VoidResult append_mcp_config(McpConfig& target, McpConfig source);

}  // namespace ava::mcp::detail
