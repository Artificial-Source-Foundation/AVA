#pragma once

#include "ava/debug/print_members_on.h"

#include <span>
#include <string_view>

namespace ava::app::rpc {

struct RpcProtocolVersions
{
  long long protocol = 1;
  int event_schema = 1;
  AVA_DEBUG_PRINT_MEMBERS_ON
};

inline constexpr RpcProtocolVersions kRpcProtocolVersions{};

[[nodiscard]] RpcProtocolVersions rpc_protocol_versions() noexcept;
[[nodiscard]] std::span<std::string_view const> rpc_command_types() noexcept;
[[nodiscard]] std::span<std::string_view const> rpc_event_names() noexcept;
[[nodiscard]] std::span<std::string_view const> rpc_stable_error_codes() noexcept;
[[nodiscard]] bool is_rpc_command_type(std::string_view type) noexcept;
[[nodiscard]] bool is_rpc_event_name(std::string_view name) noexcept;
[[nodiscard]] std::string_view stable_rpc_error_code(std::string_view candidate) noexcept;

}  // namespace ava::app::rpc
