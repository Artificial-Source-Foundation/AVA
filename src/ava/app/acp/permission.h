#pragma once

#include "ava/debug/print_members_on.h"
#include "ava/app/acp/peer.h"
#include "ava/permissions/permission.h"

#include <chrono>
#include <expected>
#include <filesystem>
#include <functional>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>

namespace ava::app::acp {

using ClientRequestSender = std::function<ava::core::Result<PendingCall>(std::string method, std::optional<std::string> params_json,
                                                                         std::chrono::milliseconds timeout, OutboundCallPolicy policy)>;
using ClientRequestCanceler = std::function<bool(JsonRpcId const& id, std::string reason)>;
using ClientConnectionAborter = std::function<void(std::string reason)>;

class ClientRequestGateway
{
 public:
  void bind(ClientRequestSender sender, ClientRequestCanceler canceler, ClientConnectionAborter aborter = {});
  void unbind();
  [[nodiscard]] ava::core::Result<PendingCall> send(std::string method, std::optional<std::string> params_json, std::chrono::milliseconds timeout,
                                                    OutboundCallPolicy policy = OutboundCallPolicy::Normal) const;
  [[nodiscard]] bool cancel(JsonRpcId const& id, std::string reason) const;
  void abort(std::string reason) const;

 private:
  mutable std::mutex mutex_;
  ClientRequestSender sender_;
  ClientRequestCanceler canceler_;
  ClientConnectionAborter aborter_;
  AVA_DEBUG_PRINT_MEMBERS_OPT_OUT
};

enum class AcpPermissionSelection
{
  Cancelled,
  AllowOnce,
  AllowAlways,
  RejectOnce,
  RejectAlways,
};

[[nodiscard]] bool permission_request_offers_session_decisions(ava::permissions::PermissionPrompt const& prompt);
[[nodiscard]] ava::core::Result<std::string> encode_permission_request_params(std::string_view session_id, ava::permissions::PermissionPrompt const& prompt,
                                                                              std::filesystem::path const& workspace_root);
[[nodiscard]] std::expected<AcpPermissionSelection, JsonRpcError> decode_permission_response(std::string_view result_json);

}  // namespace ava::app::acp
