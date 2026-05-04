#pragma once

#include <iosfwd>
#include <optional>
#include <string>

#include "ava/app/headless_policy.h"
#include "ava/app/runtime.h"
#include "ava/core/result.h"
#include "ava/provider/provider.h"

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

struct RpcModeOptions {
  RuntimeOpenOptions open_options;
  HeadlessPermissionPolicyOptions permission_policy;
};

[[nodiscard]] ava::core::Result<RpcCommand> parse_rpc_command_line(std::string_view line);
[[nodiscard]] std::string serialize_rpc_success_jsonl(std::string_view id, std::string_view result_json);
[[nodiscard]] std::string serialize_rpc_error_jsonl(std::string_view id, const ava::core::Error& error);

[[nodiscard]] ava::core::VoidResult run_rpc_loop(RuntimeSession& session, const RuntimeOpenOptions& open_options,
                                                 const ava::provider::Provider& provider,
                                                 ava::provider::Transport& transport, RuntimeRunOptions runtime_options,
                                                 std::istream& in, std::ostream& out);
[[nodiscard]] ava::core::VoidResult run_rpc_loop(RuntimeSession& session, const RuntimeOpenOptions& open_options,
                                                 const ava::provider::Provider& provider,
                                                 ava::provider::Transport& transport,
                                                 ava::provider::Transport& auth_transport,
                                                 RuntimeRunOptions runtime_options, std::istream& in,
                                                 std::ostream& out);

[[nodiscard]] int run_rpc_mode(const RpcModeOptions& options, std::istream& in, std::ostream& out, std::ostream& err);

}  // namespace ava::app
