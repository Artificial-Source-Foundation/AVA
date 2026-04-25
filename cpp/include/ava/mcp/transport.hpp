#pragma once

#include <chrono>
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

  virtual void send(const JsonRpcMessage& message) = 0;
  [[nodiscard]] virtual JsonRpcMessage receive() = 0;
  virtual void close() = 0;
};

class StdioTransport final : public McpTransport {
 public:
  StdioTransport(
      std::string command,
      std::vector<std::string> args = {},
      std::map<std::string, std::string> env = {},
      std::chrono::milliseconds receive_timeout = std::chrono::milliseconds{5000}
  );
  ~StdioTransport() override;

  StdioTransport(const StdioTransport&) = delete;
  StdioTransport& operator=(const StdioTransport&) = delete;

  void send(const JsonRpcMessage& message) override;
  [[nodiscard]] JsonRpcMessage receive() override;
  void close() override;

 private:
  class Impl;
  std::unique_ptr<Impl> impl_;
};

class InMemoryTransport final : public McpTransport {
 public:
  void send(const JsonRpcMessage& message) override;
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

}  // namespace ava::mcp
