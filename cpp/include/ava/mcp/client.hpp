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

struct McpResource {
  std::string uri;
  std::string name;
  std::string description;
  std::string mime_type;
};

struct McpPrompt {
  std::string name;
  std::string description;
  nlohmann::json arguments{nlohmann::json::array()};
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
  std::vector<McpResource> list_resources();
  nlohmann::json read_resource(const std::string& uri);
  std::vector<McpPrompt> list_prompts();
  nlohmann::json get_prompt(const std::string& name, const nlohmann::json& arguments = nlohmann::json::object());
  void close();

 private:
  [[nodiscard]] std::uint64_t next_id();
  [[nodiscard]] nlohmann::json request(std::string method, nlohmann::json params);
  [[nodiscard]] JsonRpcMessage receive_matching_response(std::uint64_t id);
  void apply_error(const JsonRpcMessage& message);
  void require_initialized_with_tools(const char* operation) const;
  void require_initialized_with_resources(const char* operation) const;
  void require_initialized_with_prompts(const char* operation) const;

  std::unique_ptr<McpTransport> transport_;
  std::string server_name_;
  std::uint64_t next_id_{1};
  std::chrono::milliseconds response_timeout_;
  bool initialized_{false};
  ServerCapabilities capabilities_;
  ConnectionHealth health_;
};

}  // namespace ava::mcp
