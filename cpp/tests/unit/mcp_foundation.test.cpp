#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include <chrono>
#include <cerrno>
#include <cstdint>
#include <cstdlib>
#include <deque>
#include <filesystem>
#include <fstream>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <thread>
#include <variant>
#include <vector>

#if !defined(_WIN32)
#include <signal.h>
#include <unistd.h>
#endif

#include "ava/config/paths.hpp"
#include "ava/mcp/client.hpp"
#include "ava/mcp/config.hpp"
#include "ava/mcp/manager.hpp"
#include "ava/mcp/message.hpp"
#include "ava/mcp/transport.hpp"

namespace {

void set_env_var(const std::string& key, const std::string& value) {
#if defined(_WIN32)
  _putenv_s(key.c_str(), value.c_str());
#else
  setenv(key.c_str(), value.c_str(), 1);
#endif
}

void unset_env_var(const std::string& key) {
#if defined(_WIN32)
  _putenv_s(key.c_str(), "");
#else
  unsetenv(key.c_str());
#endif
}

struct ScopedEnvVar {
  std::string key;
  std::optional<std::string> old_value;

  ScopedEnvVar(std::string k, std::string value)
      : key(std::move(k)) {
    if(const char* current = std::getenv(key.c_str()); current != nullptr) {
      old_value = std::string(current);
    }
    set_env_var(key, value);
  }

  ~ScopedEnvVar() {
    if(old_value.has_value()) {
      set_env_var(key, *old_value);
    } else {
      unset_env_var(key);
    }
  }
};

#ifndef AVA_MOCK_MCP_SERVER_PATH
#define AVA_MOCK_MCP_SERVER_PATH "ava_mock_mcp_server"
#endif

class ScriptedTransport final : public ava::mcp::McpTransport {
 public:
  struct State {
    std::deque<ava::mcp::JsonRpcMessage> inbound;
    std::deque<ava::mcp::JsonRpcMessage> outbound;
    bool closed{false};
  };

  explicit ScriptedTransport(std::shared_ptr<State> state)
      : state_(std::move(state)) {}

  void send(const ava::mcp::JsonRpcMessage& message) override {
    if(state_->closed) {
      throw std::runtime_error("scripted transport is closed");
    }
    state_->outbound.push_back(message);
  }

  ava::mcp::JsonRpcMessage receive() override {
    if(state_->closed) {
      throw std::runtime_error("scripted transport is closed");
    }
    if(state_->inbound.empty()) {
      throw std::runtime_error("scripted transport has no inbound messages");
    }
    auto message = state_->inbound.front();
    state_->inbound.pop_front();
    return message;
  }

  void close() override {
    state_->closed = true;
  }

 private:
  std::shared_ptr<State> state_;
};

class FailingTransport final : public ava::mcp::McpTransport {
 public:
  void send(const ava::mcp::JsonRpcMessage&) override {
    throw std::runtime_error("transport send failed");
  }

  ava::mcp::JsonRpcMessage receive() override {
    throw std::runtime_error("transport receive failed");
  }

  void close() override {}
};

[[nodiscard]] std::filesystem::path mock_mcp_server_binary_path() {
  const auto path = std::filesystem::path{AVA_MOCK_MCP_SERVER_PATH};
  if(path.is_absolute() && std::filesystem::exists(path)) {
    return path;
  }

  const auto cwd = std::filesystem::current_path();
  for(const auto& candidate : {
          cwd / path,
          cwd / "tests" / path,
          cwd.parent_path() / "tests" / path,
      }) {
    if(std::filesystem::exists(candidate)) {
      return candidate;
    }
  }
  return path;
}

class FailingInitializedNotificationTransport final : public ava::mcp::McpTransport {
 public:
  void send(const ava::mcp::JsonRpcMessage& message) override {
    ++send_count_;
    if(send_count_ == 1) {
      REQUIRE(message.method == std::optional<std::string>{"initialize"});
      return;
    }
    throw std::runtime_error("notification send failed");
  }

  ava::mcp::JsonRpcMessage receive() override {
    return ava::mcp::make_result(
        1,
        nlohmann::json{{"protocolVersion", "2024-11-05"}, {"capabilities", nlohmann::json{{"tools", nlohmann::json::object()}}}}
    );
  }

  void close() override {}

 private:
  int send_count_{0};
};

class TimedNoiseTransport final : public ava::mcp::McpTransport {
 public:
  struct State {
    std::vector<ava::mcp::JsonRpcMessage> outbound;
    std::size_t receive_calls{0};
  };

  explicit TimedNoiseTransport(std::shared_ptr<State> state, std::chrono::milliseconds receive_delay)
      : state_(std::move(state)),
        receive_delay_(receive_delay) {}

  void send(const ava::mcp::JsonRpcMessage& message) override {
    state_->outbound.push_back(message);
  }

  [[nodiscard]] ava::mcp::JsonRpcMessage receive() override {
    std::this_thread::sleep_for(receive_delay_);
    ++state_->receive_calls;
    if((state_->receive_calls % 2U) == 0U) {
      return ava::mcp::make_request(
          static_cast<std::uint64_t>(1000U + state_->receive_calls),
          "ping"
      );
    }
    return ava::mcp::make_notification(
        "notifications/progress",
        nlohmann::json{{"tick", state_->receive_calls}}
    );
  }

  void close() override {}

 private:
  std::shared_ptr<State> state_;
  std::chrono::milliseconds receive_delay_;
};

class PingReplyFailingTransport final : public ava::mcp::McpTransport {
 public:
  void send(const ava::mcp::JsonRpcMessage& message) override {
    ++send_count_;
    if(send_count_ == 1) {
      REQUIRE(message.method == std::optional<std::string>{"initialize"});
      return;
    }
    throw std::runtime_error("ping reply send failed");
  }

  [[nodiscard]] ava::mcp::JsonRpcMessage receive() override {
    return ava::mcp::make_request(99, "ping");
  }

  void close() override {}

 private:
  int send_count_{0};
};

#if !defined(_WIN32)

[[nodiscard]] bool process_is_alive(pid_t pid) {
  if(pid <= 0) {
    return false;
  }

  if(::kill(pid, 0) == 0) {
    return true;
  }
  if(errno == ESRCH) {
    return false;
  }
  return true;
}

[[nodiscard]] std::optional<pid_t> read_pid_from_file_with_retry(
    const std::filesystem::path& path,
    std::chrono::milliseconds timeout
) {
  const auto deadline = std::chrono::steady_clock::now() + timeout;
  while(std::chrono::steady_clock::now() < deadline) {
    std::ifstream input(path);
    std::string raw;
    if(input && std::getline(input, raw) && !raw.empty()) {
      try {
        const auto parsed = std::stoll(raw);
        if(parsed > 0) {
          return static_cast<pid_t>(parsed);
        }
      } catch(const std::exception&) {
        // Keep retrying until timeout.
      }
    }

    std::this_thread::sleep_for(std::chrono::milliseconds(5));
  }

  return std::nullopt;
}

#endif

}  // namespace

TEST_CASE("mcp json rpc message encodes and decodes", "[ava_mcp]") {
  const auto request = ava::mcp::make_request(7, "tools/call", nlohmann::json{{"name", "echo"}});
  const auto encoded = ava::mcp::encode_message(request);
  REQUIRE(encoded.at("jsonrpc") == "2.0");
  REQUIRE(encoded.at("id") == 7);
  REQUIRE(encoded.at("method") == "tools/call");
  REQUIRE(encoded.at("params").at("name") == "echo");

  const auto decoded = ava::mcp::decode_message(encoded);
  REQUIRE(decoded.is_request());
  REQUIRE(decoded.id.has_value());
  REQUIRE(std::get<std::uint64_t>(*decoded.id) == 7);
  REQUIRE(decoded.method == std::optional<std::string>{"tools/call"});
  REQUIRE(decoded.params.at("name") == "echo");

  const auto null_result = ava::mcp::decode_message(nlohmann::json{{"jsonrpc", "2.0"}, {"id", "abc"}, {"result", nullptr}});
  REQUIRE(null_result.is_response());
  REQUIRE(null_result.id.has_value());
  REQUIRE(std::get<std::string>(*null_result.id) == "abc");
  REQUIRE(null_result.result.is_null());

  const auto error = ava::mcp::decode_message(nlohmann::json{
      {"jsonrpc", "2.0"},
      {"id", 8},
      {"error", nlohmann::json{{"code", -32000}, {"message", "boom"}}},
  });
  REQUIRE(error.is_response());
  REQUIRE(error.error.has_value());
  REQUIRE(error.error->code == -32000);
  REQUIRE(error.error->message == "boom");

  REQUIRE_THROWS_AS(
      ava::mcp::decode_message(nlohmann::json{{"jsonrpc", "2.0"}, {"id", nullptr}, {"result", nullptr}}),
      std::runtime_error
  );
  REQUIRE_THROWS_AS(
      ava::mcp::decode_message(nlohmann::json{{"jsonrpc", "2.0"}, {"id", 1}, {"result", nullptr}, {"error", nlohmann::json{{"code", 1}, {"message", "bad"}}}}),
      std::runtime_error
  );
}

TEST_CASE("mcp in memory transport records outbound and receives inbound", "[ava_mcp]") {
  ava::mcp::InMemoryTransport transport;
  transport.send(ava::mcp::make_notification("notifications/initialized"));
  REQUIRE(transport.has_outbound());
  REQUIRE(transport.pop_outbound().method == std::optional<std::string>{"notifications/initialized"});

  transport.push_inbound(ava::mcp::make_result(1, nlohmann::json{{"ok", true}}));
  REQUIRE(transport.receive().result.at("ok") == true);

  transport.close();
  REQUIRE(transport.closed());
  REQUIRE_THROWS_AS(transport.send(ava::mcp::make_notification("x")), std::runtime_error);
}

TEST_CASE("mcp stdio transport exchanges NDJSON with local mock server", "[ava_mcp]") {
  const auto mock_server_path = mock_mcp_server_binary_path();
  if(!std::filesystem::exists(mock_server_path)) {
    SKIP("mock MCP stdio server helper is unavailable in this build");
  }

  ava::mcp::StdioTransport transport(
      mock_server_path.string(),
      {},
      {},
      std::chrono::milliseconds(500)
  );

  transport.send(ava::mcp::make_request(
      1,
      "initialize",
      nlohmann::json{{"protocolVersion", "2024-11-05"}, {"capabilities", nlohmann::json::object()}}
  ));
  auto response = transport.receive();
  REQUIRE(response.result.at("protocolVersion") == "2024-11-05");

  transport.send(ava::mcp::make_notification("notifications/initialized"));
  transport.send(ava::mcp::make_request(2, "tools/list", nlohmann::json::object()));
  response = transport.receive();
  REQUIRE(response.result.at("tools").is_array());
  REQUIRE(response.result.at("tools").at(0).at("name") == "echo");

  transport.send(ava::mcp::make_request(
      3,
      "tools/call",
      nlohmann::json{{"name", "echo"}, {"arguments", nlohmann::json{{"text", "hello"}}}}
  ));
  response = transport.receive();
  REQUIRE(response.result.at("content").at(0).at("text") == "hello");
}

TEST_CASE("mcp stdio transport receive timeout prevents hanging", "[ava_mcp]") {
#if defined(_WIN32)
  SUCCEED("stdio transport timeout coverage is POSIX-only");
#else
  ava::mcp::StdioTransport transport(
      "/bin/sh",
      {"-c", "sleep 1"},
      {},
      std::chrono::milliseconds(25)
  );
  REQUIRE_THROWS_WITH(
      transport.receive(),
      Catch::Matchers::ContainsSubstring("timed out")
  );
#endif
}

TEST_CASE("mcp stdio transport timeout stays bounded despite blank heartbeat noise", "[ava_mcp]") {
  const auto mock_server_path = mock_mcp_server_binary_path();
  if(!std::filesystem::exists(mock_server_path)) {
    SKIP("mock MCP stdio server helper is unavailable in this build");
  }

  ava::mcp::StdioTransport transport(
      mock_server_path.string(),
      {"--blank-heartbeat", "700", "10"},
      {},
      std::chrono::milliseconds(75)
  );

  const auto started_at = std::chrono::steady_clock::now();
  REQUIRE_THROWS_WITH(transport.receive(), Catch::Matchers::ContainsSubstring("timed out"));
  const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
      std::chrono::steady_clock::now() - started_at
  );
  REQUIRE(elapsed.count() < 500);
}

TEST_CASE("mcp stdio transport rejects oversized payloads without newline delimiter", "[ava_mcp]") {
#if defined(_WIN32)
  SUCCEED("stdio transport oversized-payload coverage is POSIX-only");
#else
  ava::mcp::StdioTransport transport(
      "/bin/sh",
      {"-c", "dd if=/dev/zero bs=1049600 count=1 2>/dev/null | tr '\\0' 'a'; sleep 1"},
      {},
      std::chrono::milliseconds(250)
  );

  REQUIRE_THROWS_WITH(
      transport.receive(),
      Catch::Matchers::ContainsSubstring("receive buffer exceeded")
  );
#endif
}

TEST_CASE("mcp stdio transport forwards only allowlisted baseline env plus explicit config env", "[ava_mcp]") {
  const auto mock_server_path = mock_mcp_server_binary_path();
  if(!std::filesystem::exists(mock_server_path)) {
    SKIP("mock MCP stdio server helper is unavailable in this build");
  }

  const ScopedEnvVar parent_database_url("DATABASE_URL", "postgres://parent/secret");
  const ScopedEnvVar parent_cloudflare_token("CLOUDFLARE_API_TOKEN", "parent-cloudflare-token");

  {
    ava::mcp::StdioTransport transport(
        mock_server_path.string(),
        {"--echo-env", "DATABASE_URL"},
        {},
        std::chrono::milliseconds(500)
    );

    transport.send(ava::mcp::make_request(1, "test/echoEnv", nlohmann::json::object()));
    const auto response = transport.receive();
    REQUIRE(response.result.at("value") == "");
  }

  {
    ava::mcp::StdioTransport transport(
        mock_server_path.string(),
        {"--echo-env", "CLOUDFLARE_API_TOKEN"},
        {},
        std::chrono::milliseconds(500)
    );

    transport.send(ava::mcp::make_request(1, "test/echoEnv", nlohmann::json::object()));
    const auto response = transport.receive();
    REQUIRE(response.result.at("value") == "");
  }

  {
    ava::mcp::StdioTransport transport(
        mock_server_path.string(),
        {"--echo-env", "MCP_EXPLICIT_TOKEN"},
        {{"MCP_EXPLICIT_TOKEN", "explicit-secret"}},
        std::chrono::milliseconds(500)
    );

    transport.send(ava::mcp::make_request(1, "test/echoEnv", nlohmann::json::object()));
    const auto response = transport.receive();
    REQUIRE(response.result.at("value") == "explicit-secret");
  }
}

TEST_CASE("mcp stdio transport send to closed child stdin reports error", "[ava_mcp]") {
#if defined(_WIN32)
  SUCCEED("stdio transport closed-stdin coverage is POSIX-only");
#else
  ava::mcp::StdioTransport transport(
      "/bin/sh",
      {"-c", "exec 0<&-; sleep 1"},
      {},
      std::chrono::milliseconds(75)
  );

  const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(1);
  std::optional<std::string> send_error;
  std::uint64_t attempt = 0;
  while(std::chrono::steady_clock::now() < deadline && !send_error.has_value()) {
    try {
      transport.send(ava::mcp::make_request(++attempt, "initialize", nlohmann::json::object()));
      std::this_thread::sleep_for(std::chrono::milliseconds(10));
    } catch(const std::runtime_error& error) {
      send_error = error.what();
    }
  }

  REQUIRE(send_error.has_value());
  const auto mentions_stdin_closed = send_error->find("stdin closed") != std::string::npos;
  const auto mentions_send_pipe = send_error->find("send pipe error") != std::string::npos;
  const auto mentions_failed_write = send_error->find("failed to write") != std::string::npos;
  REQUIRE((mentions_stdin_closed || mentions_send_pipe || mentions_failed_write));
#endif
}

TEST_CASE("mcp stdio transport close terminates the spawned child process group", "[ava_mcp]") {
#if defined(_WIN32)
  SUCCEED("process-group cleanup coverage is POSIX-only");
#else
  const auto pid_file = std::filesystem::temp_directory_path() / "ava_mcp_stdio_group_cleanup.pid";
  std::filesystem::remove(pid_file);

  const auto script =
      "sleep 30 & bg=$!; printf '%s' \"$bg\" > \"$PID_FILE\"; "
      "printf '{\"jsonrpc\":\"2.0\",\"id\":1,\"result\":{}}\\n'; wait";

  std::optional<pid_t> background_pid;
  try {
    ava::mcp::StdioTransport transport(
        "/bin/sh",
        {"-c", script},
        {{"PID_FILE", pid_file.string()}},
        std::chrono::milliseconds(500)
    );

    const auto response = transport.receive();
    REQUIRE(response.id.has_value());
    REQUIRE(std::get<std::uint64_t>(*response.id) == 1);

    background_pid = read_pid_from_file_with_retry(pid_file, std::chrono::milliseconds(200));
    REQUIRE(background_pid.has_value());

    transport.close();

    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    REQUIRE_FALSE(process_is_alive(*background_pid));
  } catch(...) {
    if(background_pid.has_value() && process_is_alive(*background_pid)) {
      ::kill(*background_pid, SIGKILL);
    }
    std::filesystem::remove(pid_file);
    throw;
  }

  if(background_pid.has_value() && process_is_alive(*background_pid)) {
    ::kill(*background_pid, SIGKILL);
  }
  std::filesystem::remove(pid_file);
#endif
}

TEST_CASE("mcp stdio transport close tolerates concurrent callers", "[ava_mcp]") {
#if defined(_WIN32)
  SUCCEED("stdio transport coverage is POSIX-only");
#else
  ava::mcp::StdioTransport transport(
      "/bin/sh",
      {"-c", "printf '{\"jsonrpc\":\"2.0\",\"id\":1,\"result\":{}}\\n'; sleep 30"},
      {},
      std::chrono::milliseconds(500)
  );

  const auto response = transport.receive();
  REQUIRE(response.id.has_value());
  REQUIRE(std::get<std::uint64_t>(*response.id) == 1);

  std::thread first([&transport]() {
    transport.close();
  });
  std::thread second([&transport]() {
    transport.close();
  });

  first.join();
  second.join();
  REQUIRE_NOTHROW(transport.close());
#endif
}

TEST_CASE("mcp client runs initialize list tools and call tool flow", "[ava_mcp]") {
  auto transport = std::make_unique<ava::mcp::InMemoryTransport>();
  auto* raw_transport = transport.get();
  raw_transport->push_inbound(ava::mcp::make_result(
      1,
      nlohmann::json{{"protocolVersion", "2024-11-05"}, {"capabilities", nlohmann::json{{"tools", nlohmann::json::object()}}}}
  ));
  raw_transport->push_inbound(ava::mcp::make_notification("notifications/progress", nlohmann::json{{"progress", 1}}));
  raw_transport->push_inbound(ava::mcp::make_result(
      2,
      nlohmann::json{{"tools", nlohmann::json::array({nlohmann::json{
                                          {"name", "echo"},
                                          {"description", "Echo input"},
                                          {"inputSchema", nlohmann::json{{"type", "object"}}},
                                      }})},
                     {"nextCursor", "page-2"}}
  ));
  raw_transport->push_inbound(ava::mcp::make_result(
      3,
      nlohmann::json{{"tools", nlohmann::json::array({nlohmann::json{
                                          {"name", "reverse"},
                                          {"description", "Reverse input"},
                                          {"inputSchema", nlohmann::json{{"type", "object"}}},
                                      }})}}
  ));
  raw_transport->push_inbound(ava::mcp::make_request(99, "ping"));
  raw_transport->push_inbound(ava::mcp::make_result(
      4,
      nlohmann::json{{"content", nlohmann::json::array({nlohmann::json{{"type", "text"}, {"text", "hi"}}})}}
  ));

  ava::mcp::McpClient client(std::move(transport), "mock");
  const auto capabilities = client.initialize();
  REQUIRE(capabilities.tools);
  auto outbound = raw_transport->pop_outbound();
  REQUIRE(outbound.method == std::optional<std::string>{"initialize"});
  REQUIRE(outbound.params.at("protocolVersion") == "2024-11-05");
  outbound = raw_transport->pop_outbound();
  REQUIRE(outbound.method == std::optional<std::string>{"notifications/initialized"});

  const auto tools = client.list_tools();
  REQUIRE(tools.size() == 2);
  REQUIRE(tools.at(0).name == "echo");
  REQUIRE(tools.at(1).name == "reverse");
  outbound = raw_transport->pop_outbound();
  REQUIRE(outbound.method == std::optional<std::string>{"tools/list"});
  outbound = raw_transport->pop_outbound();
  REQUIRE(outbound.method == std::optional<std::string>{"tools/list"});
  REQUIRE(outbound.params.at("cursor") == "page-2");

  const auto result = client.call_tool("echo", nlohmann::json{{"text", "hi"}});
  REQUIRE(result.at("content").at(0).at("text") == "hi");
  outbound = raw_transport->pop_outbound();
  REQUIRE(outbound.method == std::optional<std::string>{"tools/call"});
  REQUIRE(outbound.params.at("name") == "echo");
  REQUIRE(outbound.params.at("arguments").at("text") == "hi");
  outbound = raw_transport->pop_outbound();
  REQUIRE(outbound.id.has_value());
  REQUIRE(std::get<std::uint64_t>(*outbound.id) == 99);
  REQUIRE(outbound.result.is_object());
  REQUIRE(client.health().consecutive_terminal_errors() == 0);
  REQUIRE_THROWS_AS(client.initialize(), std::runtime_error);
}

TEST_CASE("mcp client response wait remains bounded under valid notification and ping noise", "[ava_mcp]") {
  auto state = std::make_shared<TimedNoiseTransport::State>();
  auto transport = std::make_unique<TimedNoiseTransport>(state, std::chrono::milliseconds(8));

  ava::mcp::McpClient client(
      std::move(transport),
      "noisy",
      std::chrono::milliseconds(90)
  );

  const auto started_at = std::chrono::steady_clock::now();
  REQUIRE_THROWS_WITH(client.initialize(), Catch::Matchers::ContainsSubstring("timed out"));
  const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
      std::chrono::steady_clock::now() - started_at
  );
  REQUIRE(elapsed.count() < 500);
  REQUIRE(client.health().consecutive_terminal_errors() == 1);

  std::size_t pong_count = 0;
  for(const auto& outbound : state->outbound) {
    if(!outbound.method.has_value() && outbound.id.has_value() && outbound.result.is_object()) {
      ++pong_count;
    }
  }
  REQUIRE(pong_count > 0);
}

TEST_CASE("mcp client enforces initialize and tools capability before tool operations", "[ava_mcp]") {
  auto transport = std::make_unique<ava::mcp::InMemoryTransport>();
  auto* raw_transport = transport.get();
  raw_transport->push_inbound(ava::mcp::make_result(
      1,
      nlohmann::json{{"protocolVersion", "2024-11-05"}, {"capabilities", nlohmann::json::object()}}
  ));

  ava::mcp::McpClient client(std::move(transport), "mock");
  REQUIRE_THROWS_AS(client.list_tools(), std::runtime_error);
  client.initialize();
  REQUIRE_THROWS_AS(client.list_tools(), std::runtime_error);
  REQUIRE(raw_transport->pop_outbound().method == std::optional<std::string>{"initialize"});
  REQUIRE(raw_transport->pop_outbound().method == std::optional<std::string>{"notifications/initialized"});
}

TEST_CASE("mcp client rejects malformed initialize capabilities as terminal health", "[ava_mcp]") {
  auto transport = std::make_unique<ava::mcp::InMemoryTransport>();
  auto* raw_transport = transport.get();
  raw_transport->push_inbound(ava::mcp::make_result(
      1,
      nlohmann::json{{"protocolVersion", "2024-11-05"}, {"capabilities", "broken"}}
  ));

  ava::mcp::McpClient client(std::move(transport), "mock");
  REQUIRE_THROWS_WITH(
      client.initialize(),
      Catch::Matchers::ContainsSubstring("capabilities must be an object")
  );
  REQUIRE(client.health().consecutive_terminal_errors() == 1);
}

TEST_CASE("mcp client records ping response send failures as terminal health", "[ava_mcp]") {
  auto transport = std::make_unique<PingReplyFailingTransport>();
  ava::mcp::McpClient client(std::move(transport), "mock", std::chrono::milliseconds(50));

  REQUIRE_THROWS_WITH(
      client.initialize(),
      Catch::Matchers::ContainsSubstring("ping reply send failed")
  );
  REQUIRE(client.health().consecutive_terminal_errors() == 1);
}

TEST_CASE("mcp client propagates errors and tracks terminal health", "[ava_mcp]") {
  auto transport = std::make_unique<ava::mcp::InMemoryTransport>();
  auto* raw_transport = transport.get();
  raw_transport->push_inbound(ava::mcp::make_error(1, -32603, "first"));
  raw_transport->push_inbound(ava::mcp::make_error(2, -32603, "second"));
  raw_transport->push_inbound(ava::mcp::make_error(3, -32603, "third"));
  raw_transport->push_inbound(ava::mcp::make_result(
      4,
      nlohmann::json{{"protocolVersion", "2024-11-05"}, {"capabilities", nlohmann::json::object()}}
  ));

  ava::mcp::McpClient client(std::move(transport), "mock");
  REQUIRE_THROWS_AS(client.initialize(), std::runtime_error);
  REQUIRE(client.health().consecutive_terminal_errors() == 0);
  REQUIRE_THROWS_AS(client.initialize(), std::runtime_error);
  REQUIRE(client.health().consecutive_terminal_errors() == 0);
  REQUIRE_THROWS_AS(client.initialize(), std::runtime_error);
  REQUIRE_FALSE(client.health().reconnect_needed());

  client.initialize();
  REQUIRE(client.health().consecutive_terminal_errors() == 0);
}

TEST_CASE("mcp client treats response correlation failures as terminal health errors", "[ava_mcp]") {
  auto transport = std::make_unique<ava::mcp::InMemoryTransport>();
  auto* raw_transport = transport.get();
  raw_transport->push_inbound(ava::mcp::make_notification("notifications/progress"));
  raw_transport->push_inbound(ava::mcp::make_result(999, nlohmann::json::object()));
  raw_transport->push_inbound(ava::mcp::make_result(888, nlohmann::json::object()));
  raw_transport->push_inbound(ava::mcp::make_result(3, nlohmann::json{{"protocolVersion", "2024-11-05"}}));

  ava::mcp::McpClient client(std::move(transport), "mock");
  REQUIRE_THROWS_AS(client.initialize(), std::runtime_error);
  REQUIRE(client.health().consecutive_terminal_errors() == 1);
  REQUIRE_THROWS_AS(client.initialize(), std::runtime_error);
  REQUIRE(client.health().consecutive_terminal_errors() == 2);
  client.initialize();
  REQUIRE(client.health().consecutive_terminal_errors() == 0);
}

TEST_CASE("mcp client reports reconnect needed after three terminal failures", "[ava_mcp]") {
  auto transport = std::make_unique<ava::mcp::InMemoryTransport>();
  auto* raw_transport = transport.get();
  raw_transport->push_inbound(ava::mcp::make_result(999, nlohmann::json::object()));
  raw_transport->push_inbound(ava::mcp::make_result(998, nlohmann::json::object()));
  raw_transport->push_inbound(ava::mcp::make_result(997, nlohmann::json::object()));

  ava::mcp::McpClient client(std::move(transport), "mock");
  REQUIRE_THROWS_AS(client.initialize(), std::runtime_error);
  REQUIRE_THROWS_AS(client.initialize(), std::runtime_error);
  REQUIRE_FALSE(client.health().reconnect_needed());
  REQUIRE_THROWS_AS(client.initialize(), std::runtime_error);
  REQUIRE(client.health().reconnect_needed());
}

TEST_CASE("mcp valid server errors reset terminal health streak", "[ava_mcp]") {
  auto transport = std::make_unique<ava::mcp::InMemoryTransport>();
  auto* raw_transport = transport.get();
  raw_transport->push_inbound(ava::mcp::make_result(999, nlohmann::json::object()));
  raw_transport->push_inbound(ava::mcp::make_error(2, -32601, "unknown method"));
  raw_transport->push_inbound(ava::mcp::make_result(997, nlohmann::json::object()));
  raw_transport->push_inbound(ava::mcp::make_result(996, nlohmann::json::object()));

  ava::mcp::McpClient client(std::move(transport), "mock");
  REQUIRE_THROWS_AS(client.initialize(), std::runtime_error);
  REQUIRE(client.health().consecutive_terminal_errors() == 1);
  REQUIRE_THROWS_AS(client.initialize(), std::runtime_error);
  REQUIRE(client.health().consecutive_terminal_errors() == 0);
  REQUIRE_THROWS_AS(client.initialize(), std::runtime_error);
  REQUIRE_THROWS_AS(client.initialize(), std::runtime_error);
  REQUIRE_FALSE(client.health().reconnect_needed());
}

TEST_CASE("mcp client records transport failures as terminal health errors", "[ava_mcp]") {
  auto transport = std::make_unique<ava::mcp::InMemoryTransport>();
  auto* raw_transport = transport.get();
  raw_transport->close();

  ava::mcp::McpClient client(std::move(transport), "mock");
  REQUIRE_THROWS_AS(client.initialize(), std::runtime_error);
  REQUIRE(client.health().consecutive_terminal_errors() == 1);
  REQUIRE(raw_transport->closed());
}

TEST_CASE("mcp client rejects malformed tool results deterministically", "[ava_mcp]") {
  auto transport = std::make_unique<ava::mcp::InMemoryTransport>();
  auto* raw_transport = transport.get();
  raw_transport->push_inbound(ava::mcp::make_result(
      1,
      nlohmann::json{{"protocolVersion", "2024-11-05"}, {"capabilities", nlohmann::json{{"tools", nlohmann::json::object()}}}}
  ));
  raw_transport->push_inbound(ava::mcp::make_result(2, nlohmann::json::array()));
  raw_transport->push_inbound(ava::mcp::make_result(3, nlohmann::json::object()));

  ava::mcp::McpClient client(std::move(transport), "mock");
  client.initialize();
  REQUIRE_THROWS_AS(client.list_tools(), std::runtime_error);
  REQUIRE(client.health().consecutive_terminal_errors() == 1);
  REQUIRE_THROWS_AS(client.list_tools(), std::runtime_error);
}

TEST_CASE("mcp client records malformed tools list as terminal health", "[ava_mcp]") {
  auto transport = std::make_unique<ava::mcp::InMemoryTransport>();
  auto* raw_transport = transport.get();
  raw_transport->push_inbound(ava::mcp::make_result(
      1,
      nlohmann::json{{"protocolVersion", "2024-11-05"}, {"capabilities", nlohmann::json{{"tools", nlohmann::json::object()}}}}
  ));
  raw_transport->push_inbound(ava::mcp::make_result(2, nlohmann::json{{"nextCursor", "later"}}));

  ava::mcp::McpClient client(std::move(transport), "mock");
  client.initialize();
  REQUIRE_THROWS_AS(client.list_tools(), std::runtime_error);
  REQUIRE(client.health().consecutive_terminal_errors() == 1);
}

TEST_CASE("mcp client rejects empty or blank tool names from tools/list", "[ava_mcp]") {
  auto transport = std::make_unique<ava::mcp::InMemoryTransport>();
  auto* raw_transport = transport.get();
  raw_transport->push_inbound(ava::mcp::make_result(
      1,
      nlohmann::json{{"protocolVersion", "2024-11-05"}, {"capabilities", nlohmann::json{{"tools", nlohmann::json::object()}}}}
  ));
  raw_transport->push_inbound(ava::mcp::make_result(
      2,
      nlohmann::json{{"tools", nlohmann::json::array({nlohmann::json{{"name", ""}, {"inputSchema", nlohmann::json{{"type", "object"}}}}})}}
  ));
  raw_transport->push_inbound(ava::mcp::make_result(
      3,
      nlohmann::json{{"tools", nlohmann::json::array({nlohmann::json{{"name", "   "}, {"inputSchema", nlohmann::json{{"type", "object"}}}}})}}
  ));

  ava::mcp::McpClient client(std::move(transport), "mock");
  client.initialize();

  REQUIRE_THROWS_WITH(
      client.list_tools(),
      Catch::Matchers::ContainsSubstring("empty or blank")
  );
  REQUIRE(client.health().consecutive_terminal_errors() == 1);

  REQUIRE_THROWS_WITH(
      client.list_tools(),
      Catch::Matchers::ContainsSubstring("empty or blank")
  );
  REQUIRE(client.health().consecutive_terminal_errors() == 1);
}

TEST_CASE("mcp initialize notification send failure is terminal health", "[ava_mcp]") {
  auto transport = std::make_unique<FailingInitializedNotificationTransport>();
  ava::mcp::McpClient client(std::move(transport), "mock");

  REQUIRE_THROWS_AS(client.initialize(), std::runtime_error);
  REQUIRE(client.health().consecutive_terminal_errors() == 1);
}

TEST_CASE("mcp manager isolates server init failures and routes tool calls", "[ava_mcp]") {
  auto good_state = std::make_shared<ScriptedTransport::State>();
  good_state->inbound.push_back(ava::mcp::make_result(
      1,
      nlohmann::json{{"protocolVersion", "2024-11-05"}, {"capabilities", nlohmann::json{{"tools", nlohmann::json::object()}}}}
  ));
  good_state->inbound.push_back(ava::mcp::make_result(
      2,
      nlohmann::json{{"tools", nlohmann::json::array({nlohmann::json{{"name", "echo"}, {"description", "Echo"}, {"inputSchema", nlohmann::json{{"type", "object"}}}}})}}
  ));
  good_state->inbound.push_back(ava::mcp::make_result(
      3,
      nlohmann::json{{"content", nlohmann::json::array({nlohmann::json{{"type", "text"}, {"text", "hello"}}})}, {"isError", false}}
  ));

  ava::mcp::McpServerConfig good;
  good.name = "good";
  good.enabled = true;
  good.transport_type = ava::mcp::TransportType::Stdio;
  good.stdio.command = "mock";

  ava::mcp::McpServerConfig bad = good;
  bad.name = "bad";

  ava::mcp::McpServerConfig disabled = good;
  disabled.name = "disabled";
  disabled.enabled = false;

  ava::mcp::McpManager manager([
                                  good_state
                              ](const ava::mcp::McpServerConfig& server) -> std::unique_ptr<ava::mcp::McpTransport> {
    if(server.name == "good") {
      return std::make_unique<ScriptedTransport>(good_state);
    }
    if(server.name == "bad") {
      return std::make_unique<FailingTransport>();
    }
    return std::make_unique<FailingTransport>();
  });

  const auto reports = manager.initialize(ava::mcp::McpConfig{.servers = {good, bad, disabled}});
  REQUIRE(reports.size() == 2);
  REQUIRE(manager.server_count() == 1);
  REQUIRE(manager.tool_count() == 1);

  const auto good_report = manager.server_report("good");
  REQUIRE(good_report.has_value());
  REQUIRE(good_report->connected);
  REQUIRE(good_report->tool_count == 1);

  const auto bad_report = manager.server_report("bad");
  REQUIRE(bad_report.has_value());
  REQUIRE_FALSE(bad_report->connected);
  REQUIRE(bad_report->error.has_value());

  const auto tools = manager.list_tools();
  REQUIRE(tools.size() == 1);
  REQUIRE(tools.at(0).server_name == "good");
  REQUIRE(tools.at(0).tool.name == "echo");

  const auto result = manager.call_tool("good", "echo", nlohmann::json{{"text", "hello"}});
  REQUIRE(result.at("content").at(0).at("text") == "hello");

  REQUIRE(good_state->outbound.size() == 4);
  REQUIRE(good_state->outbound.at(0).method == std::optional<std::string>{"initialize"});
  REQUIRE(good_state->outbound.at(1).method == std::optional<std::string>{"notifications/initialized"});
  REQUIRE(good_state->outbound.at(2).method == std::optional<std::string>{"tools/list"});
  REQUIRE(good_state->outbound.at(3).method == std::optional<std::string>{"tools/call"});
  REQUIRE(good_state->outbound.at(3).params.at("name") == "echo");

  manager.shutdown();
  REQUIRE(good_state->closed);
  REQUIRE(manager.server_count() == 0);
  REQUIRE(manager.tool_count() == 0);
  REQUIRE_FALSE(manager.server_report("good").has_value());
  REQUIRE_NOTHROW(manager.shutdown());
}

TEST_CASE("mcp manager rejects calls to missing server or tool", "[ava_mcp]") {
  auto state = std::make_shared<ScriptedTransport::State>();
  state->inbound.push_back(ava::mcp::make_result(
      1,
      nlohmann::json{{"protocolVersion", "2024-11-05"}, {"capabilities", nlohmann::json{{"tools", nlohmann::json::object()}}}}
  ));
  state->inbound.push_back(ava::mcp::make_result(
      2,
      nlohmann::json{{"tools", nlohmann::json::array({nlohmann::json{{"name", "echo"}, {"inputSchema", nlohmann::json{{"type", "object"}}}}})}}
  ));

  ava::mcp::McpServerConfig server;
  server.name = "one";
  server.transport_type = ava::mcp::TransportType::Stdio;
  server.stdio.command = "mock";

  ava::mcp::McpManager manager([state](const ava::mcp::McpServerConfig&) {
    return std::make_unique<ScriptedTransport>(state);
  });

  manager.initialize(ava::mcp::McpConfig{.servers = {server}});

  REQUIRE_THROWS_WITH(
      manager.call_tool("missing", "echo", nlohmann::json::object()),
      Catch::Matchers::ContainsSubstring("not connected")
  );
  REQUIRE_THROWS_WITH(
      manager.call_tool("one", "missing", nlohmann::json::object()),
      Catch::Matchers::ContainsSubstring("is not registered")
  );
}

TEST_CASE("mcp manager isolates non-standard server initialization failures", "[ava_mcp]") {
  auto state = std::make_shared<ScriptedTransport::State>();
  state->inbound.push_back(ava::mcp::make_result(
      1,
      nlohmann::json{{"protocolVersion", "2024-11-05"}, {"capabilities", nlohmann::json{{"tools", nlohmann::json::object()}}}}
  ));
  state->inbound.push_back(ava::mcp::make_result(
      2,
      nlohmann::json{{"tools", nlohmann::json::array({nlohmann::json{{"name", "echo"}, {"inputSchema", nlohmann::json{{"type", "object"}}}}})}}
  ));

  ava::mcp::McpServerConfig stable;
  stable.name = "stable";
  stable.transport_type = ava::mcp::TransportType::Stdio;
  stable.stdio.command = "mock";

  ava::mcp::McpServerConfig unstable = stable;
  unstable.name = "unstable";

  ava::mcp::McpManager manager([state](const ava::mcp::McpServerConfig& server) -> std::unique_ptr<ava::mcp::McpTransport> {
    if(server.name == "unstable") {
      throw 7;
    }
    return std::make_unique<ScriptedTransport>(state);
  });

  const auto reports = manager.initialize(ava::mcp::McpConfig{.servers = {stable, unstable}});
  REQUIRE(reports.size() == 2);
  REQUIRE(manager.server_count() == 1);
  REQUIRE(manager.has_server("stable"));
  REQUIRE_FALSE(manager.has_server("unstable"));

  const auto unstable_report = manager.server_report("unstable");
  REQUIRE(unstable_report.has_value());
  REQUIRE_FALSE(unstable_report->connected);
  REQUIRE(unstable_report->error.has_value());
  REQUIRE_THAT(*unstable_report->error, Catch::Matchers::ContainsSubstring("non-standard exception"));
}

TEST_CASE("mcp config parses stdio servers and rejects unsupported transports", "[ava_mcp]") {
  const auto parsed = ava::mcp::parse_mcp_config_json(nlohmann::json{
      {"servers",
       nlohmann::json::array({nlohmann::json{
           {"name", "filesystem"},
           {"enabled", true},
           {"transport",
            nlohmann::json{
                {"type", "stdio"},
                {"command", "mcp-server"},
                {"args", nlohmann::json::array({"--root", "/tmp"})},
                {"env", nlohmann::json{{"TOKEN", "redacted"}}},
                {"receiveTimeoutMs", 1200},
             }},
        }})},
  });

  REQUIRE(parsed.servers.size() == 1);
  REQUIRE(parsed.servers.at(0).name == "filesystem");
  REQUIRE(parsed.servers.at(0).enabled);
  REQUIRE(parsed.servers.at(0).stdio.command == "mcp-server");
  REQUIRE(parsed.servers.at(0).stdio.args == std::vector<std::string>{"--root", "/tmp"});
  REQUIRE(parsed.servers.at(0).stdio.env.at("TOKEN") == "redacted");
  REQUIRE(parsed.servers.at(0).stdio.receive_timeout_ms == 1200);

  const auto defaults = ava::mcp::parse_mcp_config_json(nlohmann::json{
      {"servers",
       nlohmann::json::array({nlohmann::json{
           {"name", "defaults"},
           {"transport", nlohmann::json{{"type", "stdio"}, {"command", "mcp-server"}}},
       }})},
  });
  REQUIRE(defaults.servers.at(0).stdio.receive_timeout_ms == 5000);

  REQUIRE_THROWS_AS(
      ava::mcp::parse_mcp_config_json(nlohmann::json{
          {"servers",
           nlohmann::json::array({nlohmann::json{
               {"name", "remote"},
               {"transport", nlohmann::json{{"type", "http"}, {"url", "https://example.invalid"}}},
           }})},
      }),
      std::runtime_error
  );

  REQUIRE_THROWS_AS(
      ava::mcp::parse_mcp_config_json(nlohmann::json{
          {"servers", nlohmann::json::array({nlohmann::json{{"name", 42}, {"transport", nlohmann::json{{"type", "stdio"}, {"command", "server"}}}}})},
      }),
      std::runtime_error
  );

  REQUIRE_THROWS_AS(
      ava::mcp::parse_mcp_config_json(nlohmann::json{
          {"servers",
           nlohmann::json::array({nlohmann::json{{"name", "bad-timeout"},
                                                 {"transport", nlohmann::json{{"type", "stdio"}, {"command", "server"}, {"receiveTimeoutMs", 0}}}}})},
      }),
      std::runtime_error
  );

  REQUIRE_THROWS_WITH(
      ava::mcp::parse_mcp_config_json(nlohmann::json{
          {"servers",
           nlohmann::json::array({nlohmann::json{{"name", "abusive-timeout"},
                                                 {"transport", nlohmann::json{{"type", "stdio"}, {"command", "server"}, {"receiveTimeoutMs", 600000}}}}})},
      }),
      Catch::Matchers::ContainsSubstring("must not exceed")
  );
}

TEST_CASE("mcp path helpers resolve global and project locations", "[ava_mcp]") {
  const auto global = ava::config::mcp_config_path();
  REQUIRE(global.filename() == "mcp.json");
  REQUIRE(ava::config::custom_tools_dir().filename() == "tools");

  const auto project = std::filesystem::path{"/workspace/project"};
  REQUIRE(ava::config::project_mcp_config_path(project) == project / ".ava" / "mcp.json");
  REQUIRE(ava::config::project_custom_tools_dir(project) == project / ".ava" / "tools");
}

TEST_CASE("mcp missing config file loads as empty config", "[ava_mcp]") {
  const auto missing = std::filesystem::temp_directory_path() / "ava_missing_mcp_config_for_test.json";
  std::filesystem::remove(missing);
  const auto parsed = ava::mcp::load_mcp_config_file(missing);
  REQUIRE(parsed.servers.empty());
}

TEST_CASE("mcp malformed config file throws runtime error", "[ava_mcp]") {
  const auto path = std::filesystem::temp_directory_path() / "ava_malformed_mcp_config_for_test.json";
  {
    std::ofstream output(path);
    output << "{ invalid json";
  }
  REQUIRE_THROWS_AS(ava::mcp::load_mcp_config_file(path), std::runtime_error);
  std::filesystem::remove(path);
}
