#include "ava/mcp/client.hpp"

#include <algorithm>
#include <chrono>
#include <cctype>
#include <optional>
#include <stdexcept>
#include <string_view>
#include <utility>

namespace ava::mcp {

namespace {

constexpr std::size_t kMaxToolListPages = 64;

[[nodiscard]] bool is_empty_or_blank(std::string_view value) {
  if(value.empty()) {
    return true;
  }
  return std::all_of(value.begin(), value.end(), [](unsigned char ch) {
    return std::isspace(ch) != 0;
  });
}

void require_object_result(const nlohmann::json& result, const std::string& operation, ConnectionHealth& health) {
  if(!result.is_object()) {
    health.record_terminal_error();
    throw std::runtime_error("MCP server returned non-object result for " + operation);
  }
}

[[nodiscard]] ServerCapabilities parse_capabilities(const nlohmann::json& result, ConnectionHealth& health) {
  ServerCapabilities capabilities;
  const auto caps = result.value("capabilities", nlohmann::json::object());
  if(!caps.is_object()) {
    health.record_terminal_error();
    throw std::runtime_error("MCP initialize result capabilities must be an object");
  }
  if(caps.is_object()) {
    capabilities.tools = caps.contains("tools") && caps.at("tools").is_object();
    capabilities.resources = caps.contains("resources") && caps.at("resources").is_object();
    capabilities.prompts = caps.contains("prompts") && caps.at("prompts").is_object();
  }
  return capabilities;
}

[[nodiscard]] std::vector<McpTool> parse_tools(const nlohmann::json& result) {
  std::vector<McpTool> tools;
  if(!result.contains("tools") || !result.at("tools").is_array()) {
    throw std::runtime_error("MCP tools/list result must contain a tools array");
  }
  const auto& tool_values = result.at("tools");
  for(const auto& tool : tool_values) {
    if(!tool.is_object()) {
      throw std::runtime_error("MCP tool entry must be an object");
    }

    if(!tool.contains("name") || !tool.at("name").is_string()) {
      throw std::runtime_error("MCP tool entry must include a string name");
    }

    auto name = tool.at("name").get<std::string>();
    if(is_empty_or_blank(name)) {
      throw std::runtime_error("MCP tool name must not be empty or blank");
    }

    tools.push_back(McpTool{
        .name = std::move(name),
        .description = tool.value("description", std::string{}),
        .input_schema = tool.value("inputSchema", nlohmann::json::object()),
    });
  }
  return tools;
}

[[nodiscard]] std::optional<std::string> parse_next_cursor(const nlohmann::json& result) {
  if(!result.contains("nextCursor") || result.at("nextCursor").is_null()) {
    return std::nullopt;
  }
  return result.at("nextCursor").get<std::string>();
}

}  // namespace

void ConnectionHealth::record_success() {
  consecutive_terminal_errors_ = 0;
}

void ConnectionHealth::record_terminal_error() {
  ++consecutive_terminal_errors_;
}

bool ConnectionHealth::reconnect_needed() const {
  return consecutive_terminal_errors_ >= 3;
}

std::uint32_t ConnectionHealth::consecutive_terminal_errors() const {
  return consecutive_terminal_errors_;
}

McpClient::McpClient(
    std::unique_ptr<McpTransport> transport,
    std::string server_name,
    std::chrono::milliseconds response_timeout
)
    : transport_(std::move(transport)),
      server_name_(std::move(server_name)),
      response_timeout_(response_timeout) {
  if(!transport_) {
    throw std::runtime_error("MCP client requires a transport");
  }
  if(server_name_.empty()) {
    throw std::runtime_error("MCP client requires a server name");
  }
  if(response_timeout_.count() <= 0) {
    throw std::runtime_error("MCP client response timeout must be positive");
  }
}

const std::string& McpClient::server_name() const {
  return server_name_;
}

const ServerCapabilities& McpClient::capabilities() const {
  return capabilities_;
}

const ConnectionHealth& McpClient::health() const {
  return health_;
}

ServerCapabilities McpClient::initialize() {
  if(initialized_) {
    throw std::runtime_error("MCP client is already initialized");
  }
  try {
    const auto result = request(
        "initialize",
        nlohmann::json{
            {"protocolVersion", "2024-11-05"},
            {"capabilities", nlohmann::json::object()},
            {"clientInfo", nlohmann::json{{"name", "ava-cpp"}, {"version", "m25"}}},
        }
    );
    require_object_result(result, "initialize", health_);
    const auto protocol_version = result.value("protocolVersion", std::string{});
    if(protocol_version != "2024-11-05") {
      health_.record_terminal_error();
      throw std::runtime_error("MCP server '" + server_name_ + "' returned unsupported protocol version: " + protocol_version);
    }
    capabilities_ = parse_capabilities(result, health_);
    try {
      transport_->send(make_notification("notifications/initialized"));
    } catch(...) {
      health_.record_terminal_error();
      throw;
    }
    initialized_ = true;
    return capabilities_;
  } catch(const nlohmann::json::exception& e) {
    health_.record_terminal_error();
    throw std::runtime_error(std::string("MCP initialize response was malformed: ") + e.what());
  }
}

std::vector<McpTool> McpClient::list_tools() {
  require_initialized_with_tools("tools/list");
  try {
    std::vector<McpTool> tools;
    std::optional<std::string> cursor;
    std::size_t pages = 0;
    do {
      ++pages;
      if(pages > kMaxToolListPages) {
        health_.record_terminal_error();
        throw std::runtime_error("MCP tools/list exceeded page limit");
      }
      nlohmann::json params = nlohmann::json::object();
      if(cursor.has_value()) {
        params["cursor"] = *cursor;
      }
      const auto result = request("tools/list", std::move(params));
      require_object_result(result, "tools/list", health_);
      std::vector<McpTool> page;
      try {
        page = parse_tools(result);
        cursor = parse_next_cursor(result);
      } catch(const std::runtime_error&) {
        health_.record_terminal_error();
        throw;
      }
      tools.insert(tools.end(), page.begin(), page.end());
    } while(cursor.has_value() && !cursor->empty());
    return tools;
  } catch(const nlohmann::json::exception& e) {
    health_.record_terminal_error();
    throw std::runtime_error(std::string("MCP tools/list response was malformed: ") + e.what());
  }
}

nlohmann::json McpClient::call_tool(const std::string& name, const nlohmann::json& arguments) {
  require_initialized_with_tools("tools/call");
  if(name.empty()) {
    throw std::runtime_error("MCP tool name must not be empty");
  }
  return request("tools/call", nlohmann::json{{"name", name}, {"arguments", arguments}});
}

void McpClient::close() {
  try {
    transport_->close();
  } catch(...) {
    // Best effort close.
  }
}

std::uint64_t McpClient::next_id() {
  return next_id_++;
}

nlohmann::json McpClient::request(std::string method, nlohmann::json params) {
  const auto id = next_id();
  try {
    transport_->send(make_request(id, std::move(method), std::move(params)));
  } catch(...) {
    health_.record_terminal_error();
    throw;
  }
  auto response = receive_matching_response(id);
  health_.record_success();
  if(response.error.has_value()) {
    apply_error(response);
  }
  return response.result;
}

JsonRpcMessage McpClient::receive_matching_response(std::uint64_t id) {
  constexpr std::size_t kMaxSkippedMessages = 10000;
  const JsonRpcId expected_id{id};
  const auto deadline = std::chrono::steady_clock::now() + response_timeout_;
  std::size_t skipped_messages = 0;
  while(true) {
    if(std::chrono::steady_clock::now() >= deadline) {
      health_.record_terminal_error();
      throw std::runtime_error(
          "MCP server '" + server_name_ + "' timed out waiting for response id " +
          std::to_string(id) + " after " + std::to_string(response_timeout_.count()) + "ms"
      );
    }

    if(skipped_messages > kMaxSkippedMessages) {
      health_.record_terminal_error();
      throw std::runtime_error("MCP server '" + server_name_ + "' exceeded response wait message limit");
    }
    JsonRpcMessage response;
    try {
      response = transport_->receive();
    } catch(...) {
      health_.record_terminal_error();
      throw;
    }

    if(response.id.has_value() && response.method.has_value()) {
      if(*response.method == "ping") {
        JsonRpcMessage pong;
        pong.id = response.id;
        pong.result = nlohmann::json::object();
        try {
          transport_->send(pong);
        } catch(...) {
          health_.record_terminal_error();
          throw;
        }
        ++skipped_messages;
        continue;
      }
      JsonRpcMessage error;
      error.id = response.id;
      error.error = JsonRpcError{.code = -32601, .message = "unsupported MCP request from server"};
      try {
        transport_->send(error);
      } catch(...) {
        health_.record_terminal_error();
        throw;
      }
      ++skipped_messages;
      continue;
    }
    if(!response.id.has_value()) {
      if(response.method.has_value()) {
        ++skipped_messages;
        continue;
      }
      health_.record_terminal_error();
      throw std::runtime_error("MCP response missing id");
    }
    if(response.id != expected_id) {
      health_.record_terminal_error();
      throw std::runtime_error("MCP response id did not match request id");
    }
    return response;
  }
}

void McpClient::apply_error(const JsonRpcMessage& message) {
  throw std::runtime_error("MCP server '" + server_name_ + "' error: " + message.error->message);
}

void McpClient::require_initialized_with_tools(const char* operation) const {
  if(!initialized_) {
    throw std::runtime_error(std::string("MCP client must be initialized before ") + operation);
  }
  if(!capabilities_.tools) {
    throw std::runtime_error(std::string("MCP server '") + server_name_ + "' did not advertise tools capability for " + operation);
  }
}

}  // namespace ava::mcp
