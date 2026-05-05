#pragma once

#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "ava/core/result.h"
#include "ava/provider/provider.h"

namespace ava::provider::detail {

[[nodiscard]] std::string normalized_openai_compatible_finish_reason(std::string_view reason);
[[nodiscard]] std::string sanitized_openai_compatible_snippet(std::string_view body);

[[nodiscard]] std::vector<StreamEvent> finish_reasoning_if_open(bool& reasoning_open,
                                                                std::string_view reasoning_format);
void append_finish_reasoning_if_open(std::vector<StreamEvent>& events, bool& reasoning_open,
                                     std::string_view reasoning_format);
void append_openai_compatible_done(std::vector<StreamEvent>& events, std::optional<TokenUsage> usage,
                                   std::string stop_reason);
void append_openai_compatible_tool_call_end_events(std::vector<StreamEvent>& events,
                                                   std::map<int, std::string>& open_tool_call_ids);
void append_openai_compatible_tool_call_delta_events(std::vector<StreamEvent>& events,
                                                     std::map<int, std::string>& open_tool_call_ids,
                                                     std::string_view delta);
void append_openai_compatible_choice_delta_events(std::vector<StreamEvent>& events,
                                                  std::map<int, std::string>& open_tool_call_ids, bool& reasoning_open,
                                                  std::string& stop_reason, std::string_view choice,
                                                  std::string_view reasoning_format);
void append_openai_compatible_event_for_data(std::vector<StreamEvent>& events,
                                             std::map<int, std::string>& open_tool_call_ids, bool& reasoning_open,
                                             std::optional<TokenUsage>& usage, std::string& stop_reason, bool& saw_data,
                                             bool& done_seen, bool& error_seen, std::string_view data,
                                             std::string_view reasoning_format);
void append_openai_compatible_events_for_sse_line(std::vector<StreamEvent>& events,
                                                  std::map<int, std::string>& open_tool_call_ids, bool& reasoning_open,
                                                  std::optional<TokenUsage>& usage, std::string& stop_reason,
                                                  bool& saw_data, bool& done_seen, bool& error_seen, std::string& data,
                                                  std::string line, std::string_view reasoning_format);

[[nodiscard]] ava::core::Result<std::vector<StreamEvent>> parse_openai_compatible_chat_response(
    std::string_view body, std::string_view reasoning_format);

}  // namespace ava::provider::detail
