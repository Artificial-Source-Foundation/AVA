#pragma once

#include <cstddef>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include <nlohmann/json.hpp>

#include "ava/mcp/client.hpp"
#include "ava/mcp/config.hpp"

namespace ava::mcp {

struct McpDiscoveredTool {
  std::string server_name;
  McpTool tool;
};

struct McpServerReport {
  std::string server_name;
  bool connected{false};
  std::optional<std::string> error;
  std::size_t tool_count{0};
};

class McpManager {
 public:
  using TransportFactory = std::function<std::unique_ptr<McpTransport>(const McpServerConfig&)>;

  explicit McpManager(TransportFactory transport_factory = {});
  ~McpManager();

  McpManager(const McpManager&) = delete;
  McpManager& operator=(const McpManager&) = delete;

  std::vector<McpServerReport> initialize(const McpConfig& config);

  [[nodiscard]] std::vector<McpDiscoveredTool> list_tools() const;
  [[nodiscard]] std::size_t server_count() const;
  [[nodiscard]] std::size_t tool_count() const;
  [[nodiscard]] bool has_server(const std::string& server_name) const;
  [[nodiscard]] std::optional<McpServerReport> server_report(const std::string& server_name) const;

  [[nodiscard]] nlohmann::json call_tool(
      const std::string& server_name,
      const std::string& tool_name,
      const nlohmann::json& arguments
  );

  void shutdown() noexcept;

 private:
  struct ServerRuntime {
    std::unique_ptr<McpClient> client;
    std::vector<McpTool> tools;
  };

  [[nodiscard]] std::unique_ptr<McpTransport> create_transport(const McpServerConfig& config) const;
  void rebuild_tool_index();

  TransportFactory transport_factory_;
  std::unordered_map<std::string, ServerRuntime> servers_;
  std::unordered_map<std::string, McpServerReport> reports_;
  std::vector<McpDiscoveredTool> tools_;
};

}  // namespace ava::mcp
