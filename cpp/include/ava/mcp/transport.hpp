#pragma once

#include <chrono>
#include <cstdint>
#include <deque>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "ava/mcp/message.hpp"

namespace ava::mcp {

class McpTransport {
public:
  virtual ~McpTransport() = default;

  virtual void send(const JsonRpcMessage &message) = 0;
  [[nodiscard]] virtual JsonRpcMessage receive() = 0;
  virtual void close() = 0;
};

class StdioTransport final : public McpTransport {
public:
  StdioTransport(std::string command, std::vector<std::string> args = {},
                 std::map<std::string, std::string> env = {},
                 std::chrono::milliseconds receive_timeout =
                     std::chrono::milliseconds{5000});
  ~StdioTransport() override;

  StdioTransport(const StdioTransport &) = delete;
  StdioTransport &operator=(const StdioTransport &) = delete;

  void send(const JsonRpcMessage &message) override;
  [[nodiscard]] JsonRpcMessage receive() override;
  void close() override;

private:
  class Impl;
  std::unique_ptr<Impl> impl_;
};

class HttpJsonTransport final : public McpTransport {
public:
  HttpJsonTransport(std::string url,
                    std::map<std::string, std::string> headers = {},
                    std::string bearer_token_env = {},
                    std::uint32_t request_timeout_ms = 5000);

  void send(const JsonRpcMessage &message) override;
  [[nodiscard]] JsonRpcMessage receive() override;
  void close() override;

private:
  std::string url_;
  std::map<std::string, std::string> headers_;
  std::string bearer_token_env_;
  std::chrono::milliseconds request_timeout_;
  std::deque<JsonRpcMessage> pending_responses_;
  bool closed_{false};
};

class InMemoryTransport final : public McpTransport {
public:
  void send(const JsonRpcMessage &message) override;
  [[nodiscard]] JsonRpcMessage receive() override;
  void close() override;

  void push_inbound(JsonRpcMessage message);
  [[nodiscard]] JsonRpcMessage pop_outbound();
  [[nodiscard]] bool has_outbound() const;
  [[nodiscard]] bool closed() const;

private:
  std::deque<JsonRpcMessage> inbound_;
  std::deque<JsonRpcMessage> outbound_;
  bool closed_{false};
};

} // namespace ava::mcp
