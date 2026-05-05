#pragma once

#include <cstddef>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "ava/provider/anthropic_response.h"
#include "ava/provider/provider.h"

namespace ava::provider::detail {

inline constexpr std::size_t kMaxAnthropicReasoningOpaqueBytes = 64 * 1024;

[[nodiscard]] std::string normalized_anthropic_stop_reason(std::string_view reason);
[[nodiscard]] std::string stream_error_message(std::string_view message);
[[nodiscard]] std::string sanitized_anthropic_body_snippet(std::string_view body);
[[nodiscard]] std::string stop_details_explanation(std::string_view object);
[[nodiscard]] bool has_stop_details(std::string_view object);
void append_stream_error(std::vector<StreamEvent>& events, std::string_view message);
[[nodiscard]] std::optional<long long> non_negative_integer_field(std::string_view object, std::string_view key);
void merge_usage(TokenUsage& target, TokenUsage const& source);
void append_anthropic_event_for_data(std::vector<StreamEvent>& events,
                                     std::map<long long, AnthropicStreamParser::ToolBlock>& tools,
                                     std::map<long long, AnthropicStreamParser::ReasoningBlock>& reasoning_blocks,
                                     std::optional<TokenUsage>& usage, std::string& stop_reason, bool& saw_data,
                                     bool& message_stop_seen, bool& error_seen, std::string_view data);
void append_anthropic_events_for_sse_line(std::vector<StreamEvent>& events,
                                          std::map<long long, AnthropicStreamParser::ToolBlock>& tools,
                                          std::map<long long, AnthropicStreamParser::ReasoningBlock>& reasoning_blocks,
                                          std::optional<TokenUsage>& usage, std::string& stop_reason, std::string& data,
                                          bool& saw_data, bool& message_stop_seen, bool& error_seen, std::string line);

}  // namespace ava::provider::detail
