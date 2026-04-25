#pragma once

#include <cstdint>
#include <filesystem>
#include <map>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

namespace ava::mcp {

enum class TransportType {
  Stdio,
};

struct StdioTransportConfig {
  std::string command;
  std::vector<std::string> args;
  std::map<std::string, std::string> env;
  std::uint32_t receive_timeout_ms{5000};
};

struct McpServerConfig {
  std::string name;
  bool enabled{true};
  TransportType transport_type{TransportType::Stdio};
  StdioTransportConfig stdio;
};

struct McpConfig {
  std::vector<McpServerConfig> servers;
};

[[nodiscard]] McpConfig parse_mcp_config_json(const nlohmann::json& value);
[[nodiscard]] McpConfig load_mcp_config_file(const std::filesystem::path& path);

}  // namespace ava::mcp
