#pragma once

#include "ava/app/acp/protocol.h"

#include <expected>
#include <optional>
#include <string>
#include <string_view>

namespace ava::app::acp {

using DecodeResult = std::expected<Message, JsonRpcError>;
using InitializeDecodeResult = std::expected<InitializeRequest, JsonRpcError>;

[[nodiscard]] DecodeResult decode_message(std::string_view record);
[[nodiscard]] InitializeDecodeResult decode_initialize_params(Request const& request);
[[nodiscard]] std::expected<JsonRpcId, JsonRpcError> decode_cancel_request_params(Notification const& notification);

[[nodiscard]] ava::core::Result<std::string> encode_message(Message const& message);
[[nodiscard]] ava::core::Result<std::string> encode_success(JsonRpcId const& id, std::string_view result_json);
[[nodiscard]] ava::core::Result<std::string> encode_error(std::optional<JsonRpcId> const& id, int code, std::string_view message,
                                                          std::optional<std::string_view> data_json = std::nullopt);
[[nodiscard]] ava::core::Result<std::string> encode_notification(std::string_view method, std::optional<std::string_view> params_json);
[[nodiscard]] ava::core::Result<std::string> encode_request(JsonRpcId const& id, std::string_view method, std::optional<std::string_view> params_json);
[[nodiscard]] ava::core::Result<std::string> cancel_request_params_json(JsonRpcId const& id);
[[nodiscard]] ava::core::Result<std::string> initialize_result_json(std::string_view agent_version, bool image_prompt_capability);
[[nodiscard]] ava::core::Result<std::string> encode_initialize_result(JsonRpcId const& id, std::string_view agent_version, bool image_prompt_capability);

}  // namespace ava::app::acp
