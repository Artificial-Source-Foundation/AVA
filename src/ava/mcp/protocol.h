#pragma once

#include "ava/mcp/config.h"
#include "ava/core/result.h"

#include <cstddef>
#include <optional>
#include <string>
#include <string_view>

namespace ava::mcp {

inline constexpr int kMaxMcpJsonDepth = 128;
inline constexpr std::size_t kMaxMcpHeaderBytes = 16 * 1024;

[[nodiscard]] std::optional<bool> mcp_bool_field(std::string_view object, std::string_view key);
[[nodiscard]] bool mcp_json_depth_within_limit(std::string_view value, int max_depth = kMaxMcpJsonDepth);
[[nodiscard]] std::optional<std::size_t> mcp_header_end_offset(std::string_view buffer);
[[nodiscard]] ava::core::Result<std::size_t> parse_mcp_content_length(std::string_view headers, McpServerConfig const& server, std::size_t max_message_bytes);
[[nodiscard]] std::optional<std::string> mcp_response_id(std::string_view message);
[[nodiscard]] std::optional<std::string> mcp_error_message_from_response(std::string_view error_json);
[[nodiscard]] bool is_valid_mcp_tool_name(std::string_view name);
[[nodiscard]] bool is_valid_mcp_resource_uri(std::string_view uri);
[[nodiscard]] std::string mcp_text_content_from_result(std::string_view result_json);
[[nodiscard]] std::string mcp_prompt_text_from_result(std::string_view result_json);
[[nodiscard]] std::string mcp_resource_text_from_result(std::string_view result_json);

}  // namespace ava::mcp
