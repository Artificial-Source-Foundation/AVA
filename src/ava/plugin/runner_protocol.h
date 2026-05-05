#pragma once

#include "ava/plugin/runner.h"

#include <optional>
#include <string_view>

namespace ava::plugin {

[[nodiscard]] std::optional<PluginInitialization> parse_initialized_response(std::string_view record);
[[nodiscard]] std::optional<PluginToolCallResult> parse_tool_result_response(std::string_view record,
                                                                             std::string_view request_id);
[[nodiscard]] std::optional<PluginCommandCallResult> parse_command_result_response(std::string_view record,
                                                                                   std::string_view request_id);
[[nodiscard]] std::optional<PluginEventObserveResult> parse_event_observed_response(std::string_view record,
                                                                                    std::string_view request_id);

}  // namespace ava::plugin
