#pragma once

#include <cstddef>
#include <iosfwd>
#include <optional>
#include <string>
#include <string_view>

#include "ava/core/result.h"

namespace ava::app {

struct RpcCommand {
  std::string id;
  std::string type;
  std::optional<long long> protocol_version;
  std::optional<std::string> message;
  std::optional<std::string> session_id;
  std::optional<std::string> provider;
  std::optional<std::string> model;
  std::optional<std::string> instructions;
  std::optional<std::string> reasoning_level;
  std::optional<long long> reasoning_budget_tokens;
  std::optional<std::string> reasoning_display;
  std::optional<std::string> request_id;
  std::optional<std::string> correlation_id;
  std::optional<std::string> decision;
  std::optional<std::string> answer;
  std::optional<std::string> selected;
  std::optional<std::string> plugin_id;
  std::optional<std::string> name;
  std::optional<std::string> arguments;
  std::optional<std::string> server_id;
  std::optional<std::string> path;
};

[[nodiscard]] ava::core::Result<RpcCommand> parse_rpc_command_line(std::string_view line);
[[nodiscard]] std::string serialize_rpc_success_jsonl(std::string_view id, std::string_view result_json);
[[nodiscard]] std::string serialize_rpc_error_jsonl(std::string_view id, ava::core::Error const& error);

}  // namespace ava::app

namespace ava::app::rpc {

inline constexpr std::size_t kMaxRpcLineBytes = 1024 * 1024;
inline constexpr std::size_t kMaxRpcMessagesResponseBytes = 1024 * 1024;
inline constexpr std::size_t kMaxRpcMessagesEntries = 1000;
inline constexpr std::size_t kMaxRpcQueuedMessages = 64;
inline constexpr std::size_t kMaxRpcQueuedMessageBytes = 64 * 1024;
inline constexpr std::size_t kMaxRpcQueueEventMessageBytes = 512;
inline constexpr std::size_t kMaxRpcIdentifierBytes = 256;
inline constexpr long long kRpcProtocolVersion = 1;

[[nodiscard]] ava::core::Error invalid_rpc(std::string message);

[[nodiscard]] ava::core::VoidResult validate_protocol_version(RpcCommand const& command);
[[nodiscard]] std::string rpc_protocol_result_json();
[[nodiscard]] std::string parse_error_response_id(std::string_view line);
[[nodiscard]] ava::core::Result<bool> read_rpc_line_bounded(std::istream& in, std::string& line);

}  // namespace ava::app::rpc
