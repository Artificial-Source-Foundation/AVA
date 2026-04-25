#pragma once

#include <chrono>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "ava/mcp/transport.hpp"

namespace ava::mcp {

struct ServerCapabilities {
  bool tools{false};
  bool resources{false};
  bool prompts{false};
};

struct McpTool {
  std::string name;
  std::string description;
  nlohmann::json input_schema{nlohmann::json::object()};
};

class ConnectionHealth {
 public:
  void record_success();
  void record_terminal_error();
  [[nodiscard]] bool reconnect_needed() const;
  [[nodiscard]] std::uint32_t consecutive_terminal_errors() const;

 private:
  std::uint32_t consecutive_terminal_errors_{0};
};

class McpClient {
 public:
  McpClient(
      std::unique_ptr<McpTransport> transport,
      std::string server_name,
      std::chrono::milliseconds response_timeout = std::chrono::milliseconds{5000}
  );

  [[nodiscard]] const std::string& server_name() const;
  [[nodiscard]] const ServerCapabilities& capabilities() const;
  [[nodiscard]] const ConnectionHealth& health() const;

  ServerCapabilities initialize();
  std::vector<McpTool> list_tools();
  nlohmann::json call_tool(const std::string& name, const nlohmann::json& arguments);
  void close();

 private:
  [[nodiscard]] std::uint64_t next_id();
  [[nodiscard]] nlohmann::json request(std::string method, nlohmann::json params);
  [[nodiscard]] JsonRpcMessage receive_matching_response(std::uint64_t id);
  void apply_error(const JsonRpcMessage& message);
  void require_initialized_with_tools(const char* operation) const;

  std::unique_ptr<McpTransport> transport_;
  std::string server_name_;
  std::uint64_t next_id_{1};
  std::chrono::milliseconds response_timeout_;
  bool initialized_{false};
  ServerCapabilities capabilities_;
  ConnectionHealth health_;
};

}  // namespace ava::mcp
