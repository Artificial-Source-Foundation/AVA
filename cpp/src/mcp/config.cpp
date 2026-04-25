#include "ava/mcp/config.hpp"

#include <cstdint>
#include <fstream>
#include <limits>
#include <stdexcept>

namespace ava::mcp {
namespace {

constexpr std::uint32_t kDefaultReceiveTimeoutMs = 5000;
constexpr std::uint32_t kMaxReceiveTimeoutMs = 120000;

[[nodiscard]] std::vector<std::string> parse_string_array(const nlohmann::json& value, const char* field) {
  std::vector<std::string> result;
  if(!value.contains(field)) {
    return result;
  }
  const auto& array = value.at(field);
  if(!array.is_array()) {
    throw std::runtime_error(std::string("MCP stdio transport field must be an array: ") + field);
  }
  for(const auto& entry : array) {
    result.push_back(entry.get<std::string>());
  }
  return result;
}

[[nodiscard]] std::map<std::string, std::string> parse_env(const nlohmann::json& value) {
  std::map<std::string, std::string> env;
  if(!value.contains("env")) {
    return env;
  }
  const auto& env_value = value.at("env");
  if(!env_value.is_object()) {
    throw std::runtime_error("MCP stdio transport env must be an object");
  }
  for(const auto& [key, item] : env_value.items()) {
    env.emplace(key, item.get<std::string>());
  }
  return env;
}

[[nodiscard]] std::uint32_t parse_receive_timeout_ms(const nlohmann::json& transport) {
  if(!transport.contains("receiveTimeoutMs")) {
    return kDefaultReceiveTimeoutMs;
  }

  const auto& raw_timeout = transport.at("receiveTimeoutMs");
  if(!raw_timeout.is_number_integer()) {
    throw std::runtime_error("MCP stdio transport receiveTimeoutMs must be a positive integer");
  }

  const auto value = raw_timeout.get<std::int64_t>();
  if(value <= 0) {
    throw std::runtime_error("MCP stdio transport receiveTimeoutMs must be positive");
  }
  if(value > static_cast<std::int64_t>(kMaxReceiveTimeoutMs)) {
    throw std::runtime_error(
        "MCP stdio transport receiveTimeoutMs must not exceed " + std::to_string(kMaxReceiveTimeoutMs)
    );
  }
  return static_cast<std::uint32_t>(value);
}

[[nodiscard]] McpServerConfig parse_server(const nlohmann::json& value) {
  if(!value.is_object()) {
    throw std::runtime_error("MCP server config must be an object");
  }
  McpServerConfig server;
  server.name = value.at("name").get<std::string>();
  if(server.name.empty()) {
    throw std::runtime_error("MCP server name must not be empty");
  }
  server.enabled = value.value("enabled", true);

  const auto& transport = value.at("transport");
  if(!transport.is_object()) {
    throw std::runtime_error("MCP server transport must be an object");
  }
  const auto type = transport.at("type").get<std::string>();
  if(type != "stdio") {
    throw std::runtime_error("MCP transport type is not supported in C++ Milestone 25: " + type);
  }
  server.transport_type = TransportType::Stdio;
  server.stdio.command = transport.at("command").get<std::string>();
  if(server.stdio.command.empty()) {
    throw std::runtime_error("MCP stdio transport command must not be empty");
  }
  server.stdio.args = parse_string_array(transport, "args");
  server.stdio.env = parse_env(transport);
  server.stdio.receive_timeout_ms = parse_receive_timeout_ms(transport);
  return server;
}

}  // namespace

McpConfig parse_mcp_config_json(const nlohmann::json& value) {
  try {
    if(!value.is_object()) {
      throw std::runtime_error("MCP config must be an object");
    }
    McpConfig config;
    const auto servers = value.value("servers", nlohmann::json::array());
    if(!servers.is_array()) {
      throw std::runtime_error("MCP config servers must be an array");
    }
    for(const auto& server : servers) {
      config.servers.push_back(parse_server(server));
    }
    return config;
  } catch(const nlohmann::json::exception& e) {
    throw std::runtime_error(std::string("MCP config is malformed: ") + e.what());
  }
}

McpConfig load_mcp_config_file(const std::filesystem::path& path) {
  if(!std::filesystem::exists(path)) {
    return {};
  }
  std::ifstream input(path);
  if(!input) {
    throw std::runtime_error("Failed to open MCP config file: " + path.string());
  }
  try {
    nlohmann::json value;
    input >> value;
    return parse_mcp_config_json(value);
  } catch(const nlohmann::json::exception& e) {
    throw std::runtime_error(std::string("MCP config file is malformed: ") + e.what());
  }
}

}  // namespace ava::mcp
