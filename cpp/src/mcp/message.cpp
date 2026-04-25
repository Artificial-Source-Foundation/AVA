#include "ava/mcp/message.hpp"

#include <stdexcept>
#include <utility>
#include <variant>

namespace ava::mcp {

JsonRpcMessage make_request(std::uint64_t id, std::string method, nlohmann::json params) {
  return JsonRpcMessage{.id = id, .method = std::move(method), .params = std::move(params)};
}

JsonRpcMessage make_notification(std::string method, nlohmann::json params) {
  return JsonRpcMessage{.method = std::move(method), .params = std::move(params)};
}

JsonRpcMessage make_result(std::uint64_t id, nlohmann::json result) {
  return JsonRpcMessage{.id = id, .result = std::move(result)};
}

JsonRpcMessage make_error(std::uint64_t id, int code, std::string message, nlohmann::json data) {
  return JsonRpcMessage{
      .id = id,
      .error = JsonRpcError{.code = code, .message = std::move(message), .data = std::move(data)},
  };
}

nlohmann::json encode_message(const JsonRpcMessage& message) {
  nlohmann::json encoded{{"jsonrpc", "2.0"}};
  if(message.id.has_value()) {
    std::visit([&encoded](const auto& id) { encoded["id"] = id; }, *message.id);
  }
  if(message.method.has_value()) {
    encoded["method"] = *message.method;
    if(!message.params.is_null()) {
      encoded["params"] = message.params;
    }
  } else if(message.error.has_value()) {
    encoded["error"] = nlohmann::json{{"code", message.error->code}, {"message", message.error->message}};
    if(!message.error->data.is_null()) {
      encoded["error"]["data"] = message.error->data;
    }
  } else {
    encoded["result"] = message.result;
  }
  return encoded;
}

JsonRpcMessage decode_message(const nlohmann::json& value) {
  try {
    if(!value.is_object()) {
      throw std::runtime_error("MCP JSON-RPC message must be an object");
    }
    if(value.value("jsonrpc", std::string{}) != "2.0") {
      throw std::runtime_error("MCP JSON-RPC message must use jsonrpc 2.0");
    }

    JsonRpcMessage message;
    if(value.contains("id")) {
      if(value.at("id").is_null()) {
        throw std::runtime_error("MCP JSON-RPC id must not be null");
      }
      if(value.at("id").is_number_unsigned()) {
        message.id = value.at("id").get<std::uint64_t>();
      } else if(value.at("id").is_number_integer()) {
        const auto signed_id = value.at("id").get<std::int64_t>();
        if(signed_id < 0) {
          throw std::runtime_error("MCP JSON-RPC id must not be negative");
        }
        message.id = static_cast<std::uint64_t>(signed_id);
      } else if(value.at("id").is_string()) {
        message.id = value.at("id").get<std::string>();
      } else {
        throw std::runtime_error("MCP JSON-RPC id must be a string or integer");
      }
    }

    const auto has_method = value.contains("method");
    const auto has_result = value.contains("result");
    const auto has_error = value.contains("error");
    if((has_method ? 1 : 0) + (has_result ? 1 : 0) + (has_error ? 1 : 0) != 1) {
      throw std::runtime_error("MCP JSON-RPC message must contain exactly one of method, result, or error");
    }

    if(value.contains("method")) {
      message.method = value.at("method").get<std::string>();
      message.params = value.value("params", nlohmann::json::object());
      return message;
    }
    if(value.contains("error")) {
      const auto& error = value.at("error");
      if(!error.is_object()) {
        throw std::runtime_error("MCP JSON-RPC error must be an object");
      }
      message.error = JsonRpcError{
          .code = error.at("code").get<int>(),
          .message = error.at("message").get<std::string>(),
          .data = error.value("data", nlohmann::json(nullptr)),
      };
      return message;
    }
    if(value.contains("result")) {
      message.result = value.at("result");
      return message;
    }

    throw std::runtime_error("unreachable MCP JSON-RPC message state");
  } catch(const nlohmann::json::exception& e) {
    throw std::runtime_error(std::string("MCP JSON-RPC message was malformed: ") + e.what());
  }
}

}  // namespace ava::mcp
