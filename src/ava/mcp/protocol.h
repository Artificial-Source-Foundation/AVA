#pragma once

#include <optional>
#include <string>
#include <string_view>

namespace ava::mcp {

inline constexpr int kMaxMcpJsonDepth = 128;

[[nodiscard]] std::optional<bool> mcp_bool_field(std::string_view object, std::string_view key);
[[nodiscard]] bool mcp_json_depth_within_limit(std::string_view value, int max_depth = kMaxMcpJsonDepth);
[[nodiscard]] std::optional<std::string> mcp_response_id(std::string_view message);
[[nodiscard]] std::optional<std::string> mcp_error_message_from_response(std::string_view error_json);
[[nodiscard]] bool is_valid_mcp_tool_name(std::string_view name);
[[nodiscard]] bool is_valid_mcp_resource_uri(std::string_view uri);
[[nodiscard]] std::string mcp_text_content_from_result(std::string_view result_json);
[[nodiscard]] std::string mcp_prompt_text_from_result(std::string_view result_json);
[[nodiscard]] std::string mcp_resource_text_from_result(std::string_view result_json);

}  // namespace ava::mcp
