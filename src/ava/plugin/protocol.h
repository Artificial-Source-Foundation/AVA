#pragma once

#include <optional>
#include <string>
#include <string_view>

namespace ava::plugin {

inline constexpr int kPluginMaxJsonDepth = 128;

struct PluginInitialization {
  std::string api_version;
  std::string plugin_version;
  std::string contributions_json;
  std::string raw_json;
};

struct PluginToolCallResult {
  bool ok = false;
  std::string content;
  std::string metadata_json;
  std::string raw_json;
};

struct PluginCommandCallResult {
  bool ok = false;
  std::string content;
  std::string metadata_json;
  std::string raw_json;
};

struct PluginEventObserveResult {
  bool ok = false;
  std::string content;
  std::string metadata_json;
  std::string raw_json;
};

[[nodiscard]] std::string plugin_json_string(std::string_view value);
[[nodiscard]] std::string plugin_initialize_request_json(std::string_view api_version, std::string_view plugin_id,
                                                         std::string_view workspace);
[[nodiscard]] std::string plugin_tool_call_request_json(std::string_view request_id, std::string_view tool_name,
                                                        std::string_view arguments_json, std::string_view call_id,
                                                        std::string_view workspace);
[[nodiscard]] std::string plugin_command_call_request_json(std::string_view request_id, std::string_view command_name,
                                                           std::string_view arguments_json, std::string_view call_id,
                                                           std::string_view workspace);
[[nodiscard]] std::string plugin_event_observe_request_json(std::string_view request_id, std::string_view event_name,
                                                            std::string_view payload_json, std::string_view call_id,
                                                            std::string_view workspace);

[[nodiscard]] bool plugin_json_depth_within_limit(std::string_view value, int max_depth);
[[nodiscard]] std::optional<PluginInitialization> parse_plugin_initialized_response(std::string_view record);
[[nodiscard]] std::optional<PluginToolCallResult> parse_plugin_tool_result_response(std::string_view record,
                                                                                    std::string_view request_id);
[[nodiscard]] std::optional<PluginCommandCallResult> parse_plugin_command_result_response(std::string_view record,
                                                                                          std::string_view request_id);
[[nodiscard]] std::optional<PluginEventObserveResult> parse_plugin_event_observed_response(std::string_view record,
                                                                                           std::string_view request_id);

}  // namespace ava::plugin
