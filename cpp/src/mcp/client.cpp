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

constexpr std::size_t kMaxMcpListPages = 64;
constexpr std::size_t kMaxMcpOutputChars = 100'000;
constexpr std::string_view kMcpTruncationMarker = "\n[ava: truncated MCP output to 100000 bytes]";

[[nodiscard]] bool is_empty_or_blank(std::string_view value) {
  if(value.empty()) {
    return true;
  }
  return std::all_of(value.begin(), value.end(), [](unsigned char ch) {
    return std::isspace(ch) != 0;
  });
}

[[nodiscard]] bool is_utf8_continuation_byte(unsigned char ch) {
  return (ch & 0b1100'0000U) == 0b1000'0000U;
}

[[nodiscard]] std::size_t utf8_prefix_boundary(std::string_view value, std::size_t max_bytes) {
  if(max_bytes >= value.size()) {
    return value.size();
  }
  auto boundary = max_bytes;
  while(boundary > 0 && is_utf8_continuation_byte(static_cast<unsigned char>(value[boundary]))) {
    --boundary;
  }
  return boundary == max_bytes ? max_bytes : boundary;
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

void truncate_large_strings(
    nlohmann::json& value,
    ConnectionHealth& health,
    std::string_view field_name = {},
    bool binary_payload_field = false
) {
  if(value.is_string()) {
    auto text = value.get<std::string>();
    if(text.size() > kMaxMcpOutputChars) {
      if(binary_payload_field) {
        health.record_terminal_error();
        throw std::runtime_error(
            "MCP binary payload field '" + std::string(field_name) + "' exceeded " +
            std::to_string(kMaxMcpOutputChars) + " bytes"
        );
      }
      const auto max_keep = kMaxMcpOutputChars > kMcpTruncationMarker.size() ? kMaxMcpOutputChars - kMcpTruncationMarker.size() : 0U;
      const auto keep = utf8_prefix_boundary(text, max_keep);
      text.resize(keep);
      text += kMcpTruncationMarker;
      value = std::move(text);
    }
    return;
  }
  if(value.is_array()) {
    for(auto& item : value) {
      truncate_large_strings(item, health, field_name, binary_payload_field);
    }
    return;
  }
  if(value.is_object()) {
    const auto object_type = value.value("type", std::string{});
    for(auto& item : value.items()) {
      const auto& key = item.key();
      if(key == "uri" || key == "mimeType" || key == "type" || key == "role" || key == "name") {
        continue;
      }
      const auto is_binary_field = key == "blob" || (key == "data" && (object_type == "image" || object_type == "audio"));
      truncate_large_strings(item.value(), health, key, is_binary_field);
    }
  }
}

[[nodiscard]] nlohmann::json bounded_mcp_result(nlohmann::json result, ConnectionHealth& health) {
  truncate_large_strings(result, health);
  return result;
}

[[nodiscard]] std::vector<McpResource> parse_resources(const nlohmann::json& result) {
  if(!result.contains("resources") || !result.at("resources").is_array()) {
    throw std::runtime_error("MCP resources/list result must contain a resources array");
  }
  std::vector<McpResource> resources;
  for(const auto& resource : result.at("resources")) {
    if(!resource.is_object() || !resource.contains("uri") || !resource.at("uri").is_string()) {
      throw std::runtime_error("MCP resource entry must include a string uri");
    }
    if(!resource.contains("name") || !resource.at("name").is_string()) {
      throw std::runtime_error("MCP resource entry must include a string name");
    }
    auto uri = resource.at("uri").get<std::string>();
    if(is_empty_or_blank(uri)) {
      throw std::runtime_error("MCP resource uri must not be empty or blank");
    }
    auto name = resource.at("name").get<std::string>();
    if(is_empty_or_blank(name)) {
      throw std::runtime_error("MCP resource name must not be empty or blank");
    }
    resources.push_back(McpResource{
        .uri = std::move(uri),
        .name = std::move(name),
        .description = resource.value("description", std::string{}),
        .mime_type = resource.value("mimeType", std::string{}),
    });
  }
  return resources;
}

[[nodiscard]] std::vector<McpPrompt> parse_prompts(const nlohmann::json& result) {
  if(!result.contains("prompts") || !result.at("prompts").is_array()) {
    throw std::runtime_error("MCP prompts/list result must contain a prompts array");
  }
  std::vector<McpPrompt> prompts;
  for(const auto& prompt : result.at("prompts")) {
    if(!prompt.is_object() || !prompt.contains("name") || !prompt.at("name").is_string()) {
      throw std::runtime_error("MCP prompt entry must include a string name");
    }
    auto name = prompt.at("name").get<std::string>();
    if(is_empty_or_blank(name)) {
      throw std::runtime_error("MCP prompt name must not be empty or blank");
    }
    auto arguments = nlohmann::json::array();
    if(prompt.contains("arguments")) {
      if(!prompt.at("arguments").is_array()) {
        throw std::runtime_error("MCP prompt arguments must be an array when present");
      }
      arguments = prompt.at("arguments");
    }
    prompts.push_back(McpPrompt{
        .name = std::move(name),
        .description = prompt.value("description", std::string{}),
        .arguments = std::move(arguments),
    });
  }
  return prompts;
}

void require_array_field_result(const nlohmann::json& result, const std::string& operation, const char* field, ConnectionHealth& health) {
  require_object_result(result, operation, health);
  if(!result.contains(field) || !result.at(field).is_array()) {
    health.record_terminal_error();
    throw std::runtime_error("MCP " + operation + " result must contain a " + field + " array");
  }
}

[[nodiscard]] std::optional<std::string> parse_next_cursor(const nlohmann::json& result) {
  if(!result.contains("nextCursor") || result.at("nextCursor").is_null()) {
    return std::nullopt;
  }
  if(!result.at("nextCursor").is_string()) {
    throw std::runtime_error("MCP list nextCursor must be a string when present");
  }
  return result.at("nextCursor").get<std::string>();
}

[[nodiscard]] nlohmann::json prompt_get_params(const std::string& name, const nlohmann::json& arguments) {
  if(!arguments.is_object()) {
    throw std::runtime_error("MCP prompt arguments must be an object with string values");
  }
  nlohmann::json params{{"name", name}};
  if(!arguments.empty()) {
    for(const auto& argument : arguments.items()) {
      if(!argument.value().is_string()) {
        throw std::runtime_error("MCP prompt arguments must be an object with string values");
      }
    }
    params["arguments"] = arguments;
  }
  return params;
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
            {"clientInfo", nlohmann::json{{"name", "ava-cpp"}, {"version", "0.1.0"}}},
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
  std::vector<McpTool> tools;
  std::optional<std::string> cursor;
  std::size_t pages = 0;
  do {
    ++pages;
    if(pages > kMaxMcpListPages) {
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
    } catch(const nlohmann::json::exception& e) {
      health_.record_terminal_error();
      throw std::runtime_error(std::string("MCP tools/list response was malformed: ") + e.what());
    }
    tools.insert(tools.end(), page.begin(), page.end());
  } while(cursor.has_value());
  return tools;
}

nlohmann::json McpClient::call_tool(const std::string& name, const nlohmann::json& arguments) {
  require_initialized_with_tools("tools/call");
  if(is_empty_or_blank(name)) {
    throw std::runtime_error("MCP tool name must not be empty or blank");
  }
  return bounded_mcp_result(request("tools/call", nlohmann::json{{"name", name}, {"arguments", arguments}}), health_);
}

std::vector<McpResource> McpClient::list_resources() {
  require_initialized_with_resources("resources/list");
  std::vector<McpResource> resources;
  std::optional<std::string> cursor;
  std::size_t pages = 0;
  do {
    ++pages;
    if(pages > kMaxMcpListPages) {
      health_.record_terminal_error();
      throw std::runtime_error("MCP resources/list exceeded page limit");
    }
    nlohmann::json params = nlohmann::json::object();
    if(cursor.has_value()) {
      params["cursor"] = *cursor;
    }
    const auto result = request("resources/list", std::move(params));
    require_object_result(result, "resources/list", health_);
    std::vector<McpResource> page;
    try {
      page = parse_resources(result);
      cursor = parse_next_cursor(result);
    } catch(const std::runtime_error&) {
      health_.record_terminal_error();
      throw;
    } catch(const nlohmann::json::exception& e) {
      health_.record_terminal_error();
      throw std::runtime_error(std::string("MCP resources/list response was malformed: ") + e.what());
    }
    resources.insert(resources.end(), page.begin(), page.end());
  } while(cursor.has_value());
  return resources;
}

nlohmann::json McpClient::read_resource(const std::string& uri) {
  require_initialized_with_resources("resources/read");
  if(is_empty_or_blank(uri)) {
    throw std::runtime_error("MCP resource uri must not be empty or blank");
  }
  auto result = request("resources/read", nlohmann::json{{"uri", uri}});
  require_array_field_result(result, "resources/read", "contents", health_);
  return bounded_mcp_result(std::move(result), health_);
}

std::vector<McpPrompt> McpClient::list_prompts() {
  require_initialized_with_prompts("prompts/list");
  std::vector<McpPrompt> prompts;
  std::optional<std::string> cursor;
  std::size_t pages = 0;
  do {
    ++pages;
    if(pages > kMaxMcpListPages) {
      health_.record_terminal_error();
      throw std::runtime_error("MCP prompts/list exceeded page limit");
    }
    nlohmann::json params = nlohmann::json::object();
    if(cursor.has_value()) {
      params["cursor"] = *cursor;
    }
    const auto result = request("prompts/list", std::move(params));
    require_object_result(result, "prompts/list", health_);
    std::vector<McpPrompt> page;
    try {
      page = parse_prompts(result);
      cursor = parse_next_cursor(result);
    } catch(const std::runtime_error&) {
      health_.record_terminal_error();
      throw;
    } catch(const nlohmann::json::exception& e) {
      health_.record_terminal_error();
      throw std::runtime_error(std::string("MCP prompts/list response was malformed: ") + e.what());
    }
    prompts.insert(prompts.end(), page.begin(), page.end());
  } while(cursor.has_value());
  return prompts;
}

nlohmann::json McpClient::get_prompt(const std::string& name, const nlohmann::json& arguments) {
  require_initialized_with_prompts("prompts/get");
  if(is_empty_or_blank(name)) {
    throw std::runtime_error("MCP prompt name must not be empty or blank");
  }
  auto result = request("prompts/get", prompt_get_params(name, arguments));
  require_array_field_result(result, "prompts/get", "messages", health_);
  return bounded_mcp_result(std::move(result), health_);
}

void McpClient::close() {
  try {
    transport_->close();
  } catch(...) {
    // Best effort close.
  }
  initialized_ = false;
  capabilities_ = ServerCapabilities{};
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

void McpClient::require_initialized_with_resources(const char* operation) const {
  if(!initialized_) {
    throw std::runtime_error(std::string("MCP client must be initialized before ") + operation);
  }
  if(!capabilities_.resources) {
    throw std::runtime_error(std::string("MCP server '") + server_name_ + "' did not advertise resources capability for " + operation);
  }
}

void McpClient::require_initialized_with_prompts(const char* operation) const {
  if(!initialized_) {
    throw std::runtime_error(std::string("MCP client must be initialized before ") + operation);
  }
  if(!capabilities_.prompts) {
    throw std::runtime_error(std::string("MCP server '") + server_name_ + "' did not advertise prompts capability for " + operation);
  }
}

}  // namespace ava::mcp
