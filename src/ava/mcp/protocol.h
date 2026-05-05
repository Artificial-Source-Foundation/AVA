#pragma once

#include <cstddef>
#include <optional>
#include <string>
#include <string_view>

#include "ava/core/result.h"

namespace ava::mcp {

inline constexpr int kMcpMaxJsonDepth = 128;
inline constexpr std::size_t kMcpMaxHeaderBytes = 16 * 1024;

[[nodiscard]] std::string mcp_json_string(std::string_view value);
[[nodiscard]] std::string mcp_initialize_params_json(std::string_view client_version);
[[nodiscard]] std::string mcp_initialized_notification_json();
[[nodiscard]] std::string mcp_request_json(std::string_view request_id, std::string_view method,
                                           std::string_view params_json);
[[nodiscard]] std::string mcp_tool_call_params_json(std::string_view tool_name, std::string_view arguments_json);

[[nodiscard]] std::optional<bool> mcp_bool_field(std::string_view object, std::string_view key);
[[nodiscard]] bool mcp_json_depth_within_limit(std::string_view value, int max_depth);
[[nodiscard]] ava::core::Result<std::size_t> parse_mcp_content_length(std::string_view headers,
                                                                      std::size_t max_message_bytes);
[[nodiscard]] std::optional<std::size_t> mcp_header_end_offset(std::string_view buffer);
[[nodiscard]] std::optional<std::string> mcp_response_id(std::string_view message);
[[nodiscard]] std::optional<std::string> mcp_error_message_from_response(std::string_view error_json);
[[nodiscard]] bool is_valid_mcp_tool_name(std::string_view name);
[[nodiscard]] std::string mcp_text_content_from_result(std::string_view result_json);

}  // namespace ava::mcp
