#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <variant>

#include <nlohmann/json.hpp>

namespace ava::mcp {

struct JsonRpcError {
  int code{0};
  std::string message;
  nlohmann::json data{nullptr};
};

using JsonRpcId = std::variant<std::uint64_t, std::string>;

struct JsonRpcMessage {
  std::optional<JsonRpcId> id;
  std::optional<std::string> method;
  nlohmann::json params{nlohmann::json::object()};
  nlohmann::json result{nullptr};
  std::optional<JsonRpcError> error;

  [[nodiscard]] bool is_request() const { return id.has_value() && method.has_value(); }
  [[nodiscard]] bool is_notification() const { return !id.has_value() && method.has_value(); }
  [[nodiscard]] bool is_response() const { return id.has_value() && !method.has_value(); }
};

[[nodiscard]] JsonRpcMessage make_request(std::uint64_t id, std::string method, nlohmann::json params = nlohmann::json::object());
[[nodiscard]] JsonRpcMessage make_notification(std::string method, nlohmann::json params = nlohmann::json::object());
[[nodiscard]] JsonRpcMessage make_result(std::uint64_t id, nlohmann::json result);
[[nodiscard]] JsonRpcMessage make_error(std::uint64_t id, int code, std::string message, nlohmann::json data = nullptr);

[[nodiscard]] nlohmann::json encode_message(const JsonRpcMessage& message);
[[nodiscard]] JsonRpcMessage decode_message(const nlohmann::json& value);

}  // namespace ava::mcp
