#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include <algorithm>
#include <array>
#include <atomic>
#include <cctype>
#include <cerrno>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <deque>
#include <filesystem>
#include <fstream>
#include <limits>
#include <map>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
#include <variant>
#include <vector>

#if !defined(_WIN32)
#include <arpa/inet.h>
#include <netinet/in.h>
#include <signal.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

#include "ava/config/paths.hpp"
#include "ava/mcp/client.hpp"
#include "ava/mcp/config.hpp"
#include "ava/mcp/manager.hpp"
#include "ava/mcp/message.hpp"
#include "ava/mcp/transport.hpp"

namespace {

void set_env_var(const std::string &key, const std::string &value) {
#if defined(_WIN32)
  _putenv_s(key.c_str(), value.c_str());
#else
  setenv(key.c_str(), value.c_str(), 1);
#endif
}

void unset_env_var(const std::string &key) {
#if defined(_WIN32)
  _putenv_s(key.c_str(), "");
#else
  unsetenv(key.c_str());
#endif
}

struct ScopedEnvVar {
  std::string key;
  std::optional<std::string> old_value;

  ScopedEnvVar(std::string k, std::string value) : key(std::move(k)) {
    if (const char *current = std::getenv(key.c_str()); current != nullptr) {
      old_value = std::string(current);
    }
    set_env_var(key, value);
  }

  ~ScopedEnvVar() {
    if (old_value.has_value()) {
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

  void send(const ava::mcp::JsonRpcMessage &message) override {
    if (state_->closed) {
      throw std::runtime_error("scripted transport is closed");
    }
    state_->outbound.push_back(message);
  }

  ava::mcp::JsonRpcMessage receive() override {
    if (state_->closed) {
      throw std::runtime_error("scripted transport is closed");
    }
    if (state_->inbound.empty()) {
      throw std::runtime_error("scripted transport has no inbound messages");
    }
    auto message = state_->inbound.front();
    state_->inbound.pop_front();
    return message;
  }

  void close() override { state_->closed = true; }

private:
  std::shared_ptr<State> state_;
};

class FailingTransport final : public ava::mcp::McpTransport {
public:
  void send(const ava::mcp::JsonRpcMessage &) override {
    throw std::runtime_error("transport send failed");
  }

  ava::mcp::JsonRpcMessage receive() override {
    throw std::runtime_error("transport receive failed");
  }

  void close() override {}
};

[[nodiscard]] std::filesystem::path mock_mcp_server_binary_path() {
  const auto path = std::filesystem::path{AVA_MOCK_MCP_SERVER_PATH};
  if (path.is_absolute() && std::filesystem::exists(path)) {
    return path;
  }

  const auto cwd = std::filesystem::current_path();
  for (const auto &candidate : {
           cwd / path,
           cwd / "tests" / path,
           cwd.parent_path() / "tests" / path,
       }) {
    if (std::filesystem::exists(candidate)) {
      return candidate;
    }
  }
  return path;
}

class FailingInitializedNotificationTransport final
    : public ava::mcp::McpTransport {
public:
  void send(const ava::mcp::JsonRpcMessage &message) override {
    ++send_count_;
    if (send_count_ == 1) {
      REQUIRE(message.method == std::optional<std::string>{"initialize"});
      return;
    }
    throw std::runtime_error("notification send failed");
  }

  ava::mcp::JsonRpcMessage receive() override {
    return ava::mcp::make_result(
        1,
        nlohmann::json{{"protocolVersion", "2024-11-05"},
                       {"capabilities",
                        nlohmann::json{{"tools", nlohmann::json::object()}}}});
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

  explicit TimedNoiseTransport(std::shared_ptr<State> state,
                               std::chrono::milliseconds receive_delay)
      : state_(std::move(state)), receive_delay_(receive_delay) {}

  void send(const ava::mcp::JsonRpcMessage &message) override {
    state_->outbound.push_back(message);
  }

  [[nodiscard]] ava::mcp::JsonRpcMessage receive() override {
    std::this_thread::sleep_for(receive_delay_);
    ++state_->receive_calls;
    if ((state_->receive_calls % 2U) == 0U) {
      return ava::mcp::make_request(
          static_cast<std::uint64_t>(1000U + state_->receive_calls), "ping");
    }
    return ava::mcp::make_notification(
        "notifications/progress",
        nlohmann::json{{"tick", state_->receive_calls}});
  }

  void close() override {}

private:
  std::shared_ptr<State> state_;
  std::chrono::milliseconds receive_delay_;
};

class PingReplyFailingTransport final : public ava::mcp::McpTransport {
public:
  void send(const ava::mcp::JsonRpcMessage &message) override {
    ++send_count_;
    if (send_count_ == 1) {
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

#if AVA_WITH_CPR && !defined(_WIN32)

[[nodiscard]] std::string socket_error_message(const std::string &context) {
  return context + " (errno=" + std::to_string(errno) + ")";
}

class LoopbackHttpStub final {
public:
  LoopbackHttpStub(int status_code, std::string response_body,
                   std::string response_content_type = "application/json",
                   std::map<std::string, std::string> response_headers = {},
                   std::size_t expected_requests = 1)
      : status_code_(status_code), response_body_(std::move(response_body)),
        response_content_type_(std::move(response_content_type)),
        response_headers_(std::move(response_headers)),
        expected_requests_(expected_requests) {
    listen_fd_ = ::socket(AF_INET, SOCK_STREAM, 0);
    if (listen_fd_ < 0) {
      throw std::runtime_error(socket_error_message(
          "MCP loopback HTTP stub failed to create socket"));
    }

    const int reuse = 1;
    (void)::setsockopt(listen_fd_, SOL_SOCKET, SO_REUSEADDR, &reuse,
                       sizeof(reuse));

    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    address.sin_port = 0;

    if (::bind(listen_fd_, reinterpret_cast<sockaddr *>(&address),
               sizeof(address)) != 0) {
      const auto message = socket_error_message(
          "MCP loopback HTTP stub failed to bind loopback socket");
      close_listen_fd();
      throw std::runtime_error(message);
    }

    socklen_t address_size = sizeof(address);
    if (::getsockname(listen_fd_, reinterpret_cast<sockaddr *>(&address),
                      &address_size) != 0) {
      const auto message = socket_error_message(
          "MCP loopback HTTP stub failed to query bound socket");
      close_listen_fd();
      throw std::runtime_error(message);
    }
    port_ = ntohs(address.sin_port);

    if (::listen(listen_fd_, 1) != 0) {
      const auto message = socket_error_message(
          "MCP loopback HTTP stub failed to listen on loopback socket");
      close_listen_fd();
      throw std::runtime_error(message);
    }

    server_thread_ = std::thread([this]() { run(); });
  }

  ~LoopbackHttpStub() { stop(); }

  LoopbackHttpStub(const LoopbackHttpStub &) = delete;
  LoopbackHttpStub &operator=(const LoopbackHttpStub &) = delete;

  [[nodiscard]] std::string url() const {
    return "http://127.0.0.1:" + std::to_string(port_) + "/mcp";
  }

  void wait() {
    if (server_thread_.joinable()) {
      server_thread_.join();
    }
    if (!error_message_.empty()) {
      throw std::runtime_error(error_message_);
    }
  }

  [[nodiscard]] const std::string &request_line() const {
    return request_line_;
  }

  [[nodiscard]] const std::string &request_body() const {
    return request_body_;
  }

private:
  [[nodiscard]] static std::string lowercase(std::string value) {
    std::transform(
        value.begin(), value.end(), value.begin(),
        [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
    return value;
  }

  [[nodiscard]] static std::string trim(std::string value) {
    const auto first = value.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) {
      return {};
    }
    const auto last = value.find_last_not_of(" \t\r\n");
    return value.substr(first, last - first + 1);
  }

  [[nodiscard]] static std::size_t
  parse_content_length(const std::string &header_block) {
    std::size_t cursor = 0;
    while (cursor < header_block.size()) {
      const auto line_end = header_block.find("\r\n", cursor);
      const auto line = header_block.substr(
          cursor, line_end == std::string::npos ? std::string::npos
                                                : line_end - cursor);
      const auto lowered = lowercase(line);
      constexpr std::string_view kContentLength = "content-length:";
      if (lowered.rfind(kContentLength, 0) == 0) {
        const auto value = trim(line.substr(kContentLength.size()));
        if (value.empty()) {
          return 0;
        }
        try {
          return static_cast<std::size_t>(std::stoull(value));
        } catch (const std::exception &) {
          return 0;
        }
      }

      if (line_end == std::string::npos) {
        break;
      }
      cursor = line_end + 2;
    }
    return 0;
  }

  [[nodiscard]] static bool send_all(int fd, const std::string &payload) {
    std::size_t sent = 0;
    while (sent < payload.size()) {
      const auto written =
          ::send(fd, payload.data() + sent, payload.size() - sent, 0);
      if (written < 0) {
        if (errno == EINTR) {
          continue;
        }
        return false;
      }
      if (written == 0) {
        return false;
      }
      sent += static_cast<std::size_t>(written);
    }
    return true;
  }

  [[nodiscard]] static std::string reason_phrase(int status_code) {
    if (status_code == 200) {
      return "OK";
    }
    if (status_code == 302) {
      return "Found";
    }
    if (status_code == 401) {
      return "Unauthorized";
    }
    if (status_code == 500) {
      return "Internal Server Error";
    }
    return "Status";
  }

  void close_listen_fd() {
    if (listen_fd_ >= 0) {
      ::close(listen_fd_);
      listen_fd_ = -1;
    }
  }

  void stop() {
    stop_requested_.store(true, std::memory_order_release);
    if (listen_fd_ >= 0) {
      ::shutdown(listen_fd_, SHUT_RDWR);
      close_listen_fd();
    }
    if (server_thread_.joinable()) {
      server_thread_.join();
    }
  }

  void run() {
    for (std::size_t handled_requests = 0;
         handled_requests < expected_requests_; ++handled_requests) {
      const int client_fd = ::accept(listen_fd_, nullptr, nullptr);
      if (client_fd < 0) {
        if (!stop_requested_.load(std::memory_order_acquire)) {
          error_message_ = socket_error_message(
              "MCP loopback HTTP stub failed to accept client");
        }
        return;
      }

      std::string raw_request;
      std::size_t body_start = std::string::npos;
      std::size_t content_length = 0;

      while (true) {
        std::array<char, 1024> buffer{};
        const auto read_bytes =
            ::recv(client_fd, buffer.data(), buffer.size(), 0);
        if (read_bytes < 0) {
          if (errno == EINTR) {
            continue;
          }
          error_message_ = socket_error_message(
              "MCP loopback HTTP stub failed to read request");
          ::close(client_fd);
          return;
        }
        if (read_bytes == 0) {
          error_message_ =
              "MCP loopback HTTP stub client disconnected before full request";
          ::close(client_fd);
          return;
        }

        raw_request.append(buffer.data(), static_cast<std::size_t>(read_bytes));

        if (body_start == std::string::npos) {
          const auto header_end = raw_request.find("\r\n\r\n");
          if (header_end != std::string::npos) {
            body_start = header_end + 4;
            const auto line_end = raw_request.find("\r\n");
            if (line_end != std::string::npos && request_line_.empty()) {
              request_line_ = raw_request.substr(0, line_end);
            }
            content_length =
                parse_content_length(raw_request.substr(0, header_end));
          }
        }

        if (body_start != std::string::npos &&
            raw_request.size() >= body_start + content_length) {
          if (request_body_.empty()) {
            request_body_ = raw_request.substr(body_start, content_length);
          }
          break;
        }
      }

      std::string response = "HTTP/1.1 " + std::to_string(status_code_) + " " +
                             reason_phrase(status_code_) +
                             "\r\nContent-Type: " + response_content_type_;
      for (const auto &[name, value] : response_headers_) {
        response += "\r\n" + name + ": " + value;
      }
      response +=
          "\r\nContent-Length: " + std::to_string(response_body_.size()) +
          "\r\nConnection: close\r\n\r\n" + response_body_;

      if (!send_all(client_fd, response)) {
        error_message_ = socket_error_message(
            "MCP loopback HTTP stub failed to write response");
        ::close(client_fd);
        return;
      }

      ::close(client_fd);
    }
  }

  int status_code_;
  std::string response_body_;
  std::string response_content_type_;
  std::map<std::string, std::string> response_headers_;
  std::size_t expected_requests_{1};
  int listen_fd_{-1};
  std::uint16_t port_{0};
  std::thread server_thread_;
  std::atomic<bool> stop_requested_{false};
  std::string request_line_;
  std::string request_body_;
  std::string error_message_;
};

#endif

#if !defined(_WIN32)

[[nodiscard]] bool process_is_alive(pid_t pid) {
  if (pid <= 0) {
    return false;
  }

  if (::kill(pid, 0) == 0) {
    return true;
  }
  if (errno == ESRCH) {
    return false;
  }
  return true;
}

[[nodiscard]] std::optional<pid_t>
read_pid_from_file_with_retry(const std::filesystem::path &path,
                              std::chrono::milliseconds timeout) {
  const auto deadline = std::chrono::steady_clock::now() + timeout;
  while (std::chrono::steady_clock::now() < deadline) {
    std::ifstream input(path);
    std::string raw;
    if (input && std::getline(input, raw) && !raw.empty()) {
      try {
        const auto parsed = std::stoll(raw);
        if (parsed > 0) {
          return static_cast<pid_t>(parsed);
        }
      } catch (const std::exception &) {
        // Keep retrying until timeout.
      }
    }

    std::this_thread::sleep_for(std::chrono::milliseconds(5));
  }

  return std::nullopt;
}

#endif

} // namespace

TEST_CASE("mcp json rpc message encodes and decodes", "[ava_mcp]") {
  const auto request =
      ava::mcp::make_request(7, "tools/call", nlohmann::json{{"name", "echo"}});
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

  const auto null_result = ava::mcp::decode_message(
      nlohmann::json{{"jsonrpc", "2.0"}, {"id", "abc"}, {"result", nullptr}});
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
      ava::mcp::decode_message(nlohmann::json{
          {"jsonrpc", "2.0"}, {"id", nullptr}, {"result", nullptr}}),
      std::runtime_error);
  REQUIRE_THROWS_AS(
      ava::mcp::decode_message(nlohmann::json{
          {"jsonrpc", "2.0"},
          {"id", 1},
          {"result", nullptr},
          {"error", nlohmann::json{{"code", 1}, {"message", "bad"}}}}),
      std::runtime_error);
}

TEST_CASE("mcp in memory transport records outbound and receives inbound",
          "[ava_mcp]") {
  ava::mcp::InMemoryTransport transport;
  transport.send(ava::mcp::make_notification("notifications/initialized"));
  REQUIRE(transport.has_outbound());
  REQUIRE(transport.pop_outbound().method ==
          std::optional<std::string>{"notifications/initialized"});

  transport.push_inbound(
      ava::mcp::make_result(1, nlohmann::json{{"ok", true}}));
  REQUIRE(transport.receive().result.at("ok") == true);

  transport.close();
  REQUIRE(transport.closed());
  REQUIRE_THROWS_AS(transport.send(ava::mcp::make_notification("x")),
                    std::runtime_error);
}

TEST_CASE("mcp http transport requires cpr when disabled", "[ava_mcp]") {
#if AVA_WITH_CPR
  SUCCEED("HTTP transport is enabled when AVA_WITH_CPR=ON");
#else
  REQUIRE_THROWS_WITH(
      ava::mcp::HttpJsonTransport("https://example.invalid/mcp"),
      Catch::Matchers::ContainsSubstring("AVA_WITH_CPR=ON"));
#endif
}

TEST_CASE("mcp http transport constructor rejects URL userinfo", "[ava_mcp]") {
  REQUIRE_THROWS_WITH(
      ava::mcp::HttpJsonTransport("https://user:pass@example.invalid/mcp"),
      Catch::Matchers::ContainsSubstring("must not include URL userinfo"));
}

TEST_CASE("mcp http transport constructor rejects bearer env over http",
          "[ava_mcp]") {
  REQUIRE_THROWS_WITH(
      ava::mcp::HttpJsonTransport("http://example.invalid/mcp", {},
                                  "AVA_MCP_REMOTE_BEARER_TOKEN"),
      Catch::Matchers::ContainsSubstring("with authentication requires "
                                         "https://"));
}

TEST_CASE("mcp http transport constructor rejects inline credential headers",
          "[ava_mcp]") {
  REQUIRE_THROWS_WITH(ava::mcp::HttpJsonTransport(
                          "https://example.invalid/mcp",
                          {{"Authorization", "Bearer should-not-be-inline"}}),
                      Catch::Matchers::ContainsSubstring("inline credentials"));

  REQUIRE_THROWS_WITH(
      ava::mcp::HttpJsonTransport(
          "https://example.invalid/mcp",
          {{"proxy-authorization", "Basic should-not-be-inline"}}),
      Catch::Matchers::ContainsSubstring("inline credentials"));

  REQUIRE_THROWS_WITH(
      ava::mcp::HttpJsonTransport("https://example.invalid/mcp",
                                  {{"cOoKiE", "session=should-not-inline"}}),
      Catch::Matchers::ContainsSubstring("inline credentials"));
}

TEST_CASE("mcp http transport constructor rejects invalid header syntax",
          "[ava_mcp]") {
  REQUIRE_THROWS_WITH(
      ava::mcp::HttpJsonTransport("https://example.invalid/mcp",
                                  {{"Cookie: session=secret", "ignored"}}),
      Catch::Matchers::ContainsSubstring(
          "header name must be a valid HTTP field-name token"));

  REQUIRE_THROWS_WITH(ava::mcp::HttpJsonTransport("https://example.invalid/mcp",
                                                  {{"X-Bad\r\nName", "value"}}),
                      Catch::Matchers::ContainsSubstring(
                          "header name must be a valid HTTP field-name token"));

  REQUIRE_THROWS_WITH(
      ava::mcp::HttpJsonTransport("https://example.invalid/mcp",
                                  {{"X-Test", "value\r\ninjected"}}),
      Catch::Matchers::ContainsSubstring(
          "header value must not contain CR or LF"));
}

TEST_CASE("mcp http transport constructor rejects timeout overflow",
          "[ava_mcp]") {
  const auto timeout_overflow =
      static_cast<std::uint32_t>(std::numeric_limits<int>::max()) + 1U;
  REQUIRE_THROWS_WITH(
      ava::mcp::HttpJsonTransport("https://example.invalid/mcp", {}, {},
                                  timeout_overflow),
      Catch::Matchers::ContainsSubstring("exceeds supported CPR range"));
}

TEST_CASE("mcp http transport enforces pending and closed behavior",
          "[ava_mcp]") {
#if AVA_WITH_CPR
  ava::mcp::HttpJsonTransport transport("https://example.invalid/mcp");

  REQUIRE_THROWS_WITH(transport.receive(), Catch::Matchers::ContainsSubstring(
                                               "no pending responses"));

  transport.close();

  REQUIRE_THROWS_WITH(transport.receive(),
                      Catch::Matchers::ContainsSubstring("closed"));
  REQUIRE_THROWS_WITH(transport.send(ava::mcp::make_request(
                          1, "initialize", nlohmann::json::object())),
                      Catch::Matchers::ContainsSubstring("closed"));
#else
  SUCCEED("HTTP pending/closed behavior requires AVA_WITH_CPR=ON");
#endif
}

TEST_CASE(
    "mcp http transport loopback POST sends JSON-RPC and decodes response",
    "[ava_mcp]") {
#if AVA_WITH_CPR && !defined(_WIN32)
  LoopbackHttpStub stub(200,
                        R"({"jsonrpc":"2.0","id":1,"result":{"ok":true}})");
  ava::mcp::HttpJsonTransport transport(stub.url());

  transport.send(
      ava::mcp::make_request(1, "tools/list", nlohmann::json::object()));
  const auto response = transport.receive();
  stub.wait();

  REQUIRE(response.result.at("ok") == true);
  REQUIRE(stub.request_line().rfind("POST ", 0) == 0);
  const auto sent_payload = nlohmann::json::parse(stub.request_body());
  REQUIRE(sent_payload.at("jsonrpc") == "2.0");
  REQUIRE(sent_payload.at("id") == 1);
  REQUIRE(sent_payload.at("method") == "tools/list");
#elif AVA_WITH_CPR
  SUCCEED("loopback HTTP transport coverage is POSIX-only");
#else
  SUCCEED("loopback HTTP transport coverage requires AVA_WITH_CPR=ON");
#endif
}

TEST_CASE("mcp http transport rejects SSE loopback responses", "[ava_mcp]") {
#if AVA_WITH_CPR && !defined(_WIN32)
  LoopbackHttpStub stub(200,
                        R"({"jsonrpc":"2.0","id":11,"result":{"ok":true}})",
                        "text/event-stream");
  ava::mcp::HttpJsonTransport transport(stub.url());

  REQUIRE_THROWS_WITH(
      transport.send(
          ava::mcp::make_request(11, "initialize", nlohmann::json::object())),
      Catch::Matchers::ContainsSubstring("SSE response not supported"));
  stub.wait();
#elif AVA_WITH_CPR
  SUCCEED("loopback HTTP transport coverage is POSIX-only");
#else
  SUCCEED("loopback HTTP transport coverage requires AVA_WITH_CPR=ON");
#endif
}

TEST_CASE("mcp http transport rejects JSON response batches", "[ava_mcp]") {
#if AVA_WITH_CPR && !defined(_WIN32)
  LoopbackHttpStub stub(200,
                        R"([{"jsonrpc":"2.0","id":12,"result":{"ok":true}}])",
                        "application/json");
  ava::mcp::HttpJsonTransport transport(stub.url());

  REQUIRE_THROWS_WITH(
      transport.send(
          ava::mcp::make_request(12, "initialize", nlohmann::json::object())),
      Catch::Matchers::ContainsSubstring("response batches are not supported"));
  stub.wait();
#elif AVA_WITH_CPR
  SUCCEED("loopback HTTP transport coverage is POSIX-only");
#else
  SUCCEED("loopback HTTP transport coverage requires AVA_WITH_CPR=ON");
#endif
}

TEST_CASE(
    "mcp http transport notification response body does not pollute queue",
    "[ava_mcp]") {
#if AVA_WITH_CPR && !defined(_WIN32)
  LoopbackHttpStub stub(
      200, R"({"jsonrpc":"2.0","id":"ignored","result":{"ok":true}})");
  ava::mcp::HttpJsonTransport transport(stub.url());

  transport.send(ava::mcp::make_notification("notifications/initialized",
                                             nlohmann::json::object()));
  stub.wait();

  REQUIRE_THROWS_WITH(transport.receive(), Catch::Matchers::ContainsSubstring(
                                               "no pending responses"));
  const auto sent_payload = nlohmann::json::parse(stub.request_body());
  REQUIRE(sent_payload.at("jsonrpc") == "2.0");
  REQUIRE(sent_payload.at("method") == "notifications/initialized");
  REQUIRE_FALSE(sent_payload.contains("id"));
#elif AVA_WITH_CPR
  SUCCEED("loopback HTTP transport coverage is POSIX-only");
#else
  SUCCEED("loopback HTTP transport coverage requires AVA_WITH_CPR=ON");
#endif
}

TEST_CASE("mcp http transport enforces pending response queue limits",
          "[ava_mcp]") {
#if AVA_WITH_CPR && !defined(_WIN32)
  LoopbackHttpStub stub(200, R"({"jsonrpc":"2.0","id":1,"result":{"ok":true}})",
                        "application/json", {}, 32);
  ava::mcp::HttpJsonTransport transport(stub.url());

  for (std::uint64_t id = 1; id <= 32; ++id) {
    transport.send(
        ava::mcp::make_request(id, "initialize", nlohmann::json::object()));
  }

  REQUIRE_THROWS_WITH(
      transport.send(
          ava::mcp::make_request(33, "initialize", nlohmann::json::object())),
      Catch::Matchers::ContainsSubstring("pending response queue is full"));
  stub.wait();
#elif AVA_WITH_CPR
  SUCCEED("loopback HTTP transport coverage is POSIX-only");
#else
  SUCCEED("loopback HTTP transport coverage requires AVA_WITH_CPR=ON");
#endif
}

TEST_CASE("mcp http transport request rejects empty response bodies",
          "[ava_mcp]") {
#if AVA_WITH_CPR && !defined(_WIN32)
  LoopbackHttpStub stub(200, "");
  ava::mcp::HttpJsonTransport transport(stub.url());

  REQUIRE_THROWS_WITH(
      transport.send(
          ava::mcp::make_request(7, "initialize", nlohmann::json::object())),
      Catch::Matchers::ContainsSubstring("empty response body"));
  stub.wait();
#elif AVA_WITH_CPR
  SUCCEED("loopback HTTP transport coverage is POSIX-only");
#else
  SUCCEED("loopback HTTP transport coverage requires AVA_WITH_CPR=ON");
#endif
}

TEST_CASE("mcp http transport non-2xx errors avoid body leakage", "[ava_mcp]") {
#if AVA_WITH_CPR && !defined(_WIN32)
  LoopbackHttpStub stub(401, R"({"error":"should-not-leak"})");
  ava::mcp::HttpJsonTransport transport(stub.url());

  std::string error_message;
  try {
    transport.send(
        ava::mcp::make_request(9, "initialize", nlohmann::json::object()));
    FAIL("Expected 401 request failure");
  } catch (const std::exception &ex) {
    error_message = ex.what();
  }
  stub.wait();

  REQUIRE_THAT(error_message,
               Catch::Matchers::ContainsSubstring(
                   "Unauthorized; check or refresh bearer token"));
  REQUIRE(error_message.find("should-not-leak") == std::string::npos);
#elif AVA_WITH_CPR
  SUCCEED("loopback HTTP transport coverage is POSIX-only");
#else
  SUCCEED("loopback HTTP transport coverage requires AVA_WITH_CPR=ON");
#endif
}

TEST_CASE("mcp http transport 500 errors stay generic and do not leak body",
          "[ava_mcp]") {
#if AVA_WITH_CPR && !defined(_WIN32)
  LoopbackHttpStub stub(500, R"({"details":"sensitive-debug-payload"})");
  ava::mcp::HttpJsonTransport transport(stub.url());

  std::string error_message;
  try {
    transport.send(
        ava::mcp::make_request(10, "initialize", nlohmann::json::object()));
    FAIL("Expected 500 request failure");
  } catch (const std::exception &ex) {
    error_message = ex.what();
  }
  stub.wait();

  REQUIRE_THAT(error_message, Catch::Matchers::ContainsSubstring("status 500"));
  REQUIRE(error_message.find("sensitive-debug-payload") == std::string::npos);
#elif AVA_WITH_CPR
  SUCCEED("loopback HTTP transport coverage is POSIX-only");
#else
  SUCCEED("loopback HTTP transport coverage requires AVA_WITH_CPR=ON");
#endif
}

TEST_CASE("mcp http transport does not auto-follow redirects", "[ava_mcp]") {
#if AVA_WITH_CPR && !defined(_WIN32)
  LoopbackHttpStub stub(302, "", "application/json",
                        {{"Location", "http://127.0.0.1:1/redirected"}});
  ava::mcp::HttpJsonTransport transport(stub.url());

  REQUIRE_THROWS_WITH(transport.send(ava::mcp::make_request(
                          10, "initialize", nlohmann::json::object())),
                      Catch::Matchers::ContainsSubstring("status 302"));
  stub.wait();
#elif AVA_WITH_CPR
  SUCCEED("loopback HTTP transport coverage is POSIX-only");
#else
  SUCCEED("loopback HTTP transport coverage requires AVA_WITH_CPR=ON");
#endif
}

TEST_CASE("mcp stdio transport exchanges NDJSON with local mock server",
          "[ava_mcp]") {
  const auto mock_server_path = mock_mcp_server_binary_path();
  if (!std::filesystem::exists(mock_server_path)) {
    SKIP("mock MCP stdio server helper is unavailable in this build");
  }

  ava::mcp::StdioTransport transport(mock_server_path.string(), {}, {},
                                     std::chrono::milliseconds(500));

  transport.send(ava::mcp::make_request(
      1, "initialize",
      nlohmann::json{{"protocolVersion", "2024-11-05"},
                     {"capabilities", nlohmann::json::object()}}));
  auto response = transport.receive();
  REQUIRE(response.result.at("protocolVersion") == "2024-11-05");

  transport.send(ava::mcp::make_notification("notifications/initialized"));
  transport.send(
      ava::mcp::make_request(2, "tools/list", nlohmann::json::object()));
  response = transport.receive();
  REQUIRE(response.result.at("tools").is_array());
  REQUIRE(response.result.at("tools").at(0).at("name") == "echo");

  transport.send(ava::mcp::make_request(
      3, "tools/call",
      nlohmann::json{{"name", "echo"},
                     {"arguments", nlohmann::json{{"text", "hello"}}}}));
  response = transport.receive();
  REQUIRE(response.result.at("content").at(0).at("text") == "hello");
}

TEST_CASE("mcp stdio transport receive timeout prevents hanging", "[ava_mcp]") {
#if defined(_WIN32)
  SUCCEED("stdio transport timeout coverage is POSIX-only");
#else
  ava::mcp::StdioTransport transport("/bin/sh", {"-c", "sleep 1"}, {},
                                     std::chrono::milliseconds(25));
  REQUIRE_THROWS_WITH(transport.receive(),
                      Catch::Matchers::ContainsSubstring("timed out"));
#endif
}

TEST_CASE(
    "mcp stdio transport timeout stays bounded despite blank heartbeat noise",
    "[ava_mcp]") {
  const auto mock_server_path = mock_mcp_server_binary_path();
  if (!std::filesystem::exists(mock_server_path)) {
    SKIP("mock MCP stdio server helper is unavailable in this build");
  }

  ava::mcp::StdioTransport transport(mock_server_path.string(),
                                     {"--blank-heartbeat", "700", "10"}, {},
                                     std::chrono::milliseconds(75));

  const auto started_at = std::chrono::steady_clock::now();
  REQUIRE_THROWS_WITH(transport.receive(),
                      Catch::Matchers::ContainsSubstring("timed out"));
  const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
      std::chrono::steady_clock::now() - started_at);
  REQUIRE(elapsed.count() < 500);
}

TEST_CASE(
    "mcp stdio transport rejects oversized payloads without newline delimiter",
    "[ava_mcp]") {
#if defined(_WIN32)
  SUCCEED("stdio transport oversized-payload coverage is POSIX-only");
#else
  ava::mcp::StdioTransport transport(
      "/bin/sh",
      {"-c", "dd if=/dev/zero bs=1049600 count=1 2>/dev/null | tr '\\0' 'a'; "
             "sleep 1"},
      {}, std::chrono::milliseconds(250));

  REQUIRE_THROWS_WITH(transport.receive(), Catch::Matchers::ContainsSubstring(
                                               "receive buffer exceeded"));
#endif
}

TEST_CASE("mcp stdio transport forwards only allowlisted baseline env plus "
          "explicit config env",
          "[ava_mcp]") {
  const auto mock_server_path = mock_mcp_server_binary_path();
  if (!std::filesystem::exists(mock_server_path)) {
    SKIP("mock MCP stdio server helper is unavailable in this build");
  }

  const ScopedEnvVar parent_database_url("DATABASE_URL",
                                         "postgres://parent/secret");
  const ScopedEnvVar parent_cloudflare_token("CLOUDFLARE_API_TOKEN",
                                             "parent-cloudflare-token");

  {
    ava::mcp::StdioTransport transport(mock_server_path.string(),
                                       {"--echo-env", "DATABASE_URL"}, {},
                                       std::chrono::milliseconds(500));

    transport.send(
        ava::mcp::make_request(1, "test/echoEnv", nlohmann::json::object()));
    const auto response = transport.receive();
    REQUIRE(response.result.at("value") == "");
  }

  {
    ava::mcp::StdioTransport transport(mock_server_path.string(),
                                       {"--echo-env", "CLOUDFLARE_API_TOKEN"},
                                       {}, std::chrono::milliseconds(500));

    transport.send(
        ava::mcp::make_request(1, "test/echoEnv", nlohmann::json::object()));
    const auto response = transport.receive();
    REQUIRE(response.result.at("value") == "");
  }

  {
    ava::mcp::StdioTransport transport(
        mock_server_path.string(), {"--echo-env", "MCP_EXPLICIT_TOKEN"},
        {{"MCP_EXPLICIT_TOKEN", "explicit-secret"}},
        std::chrono::milliseconds(500));

    transport.send(
        ava::mcp::make_request(1, "test/echoEnv", nlohmann::json::object()));
    const auto response = transport.receive();
    REQUIRE(response.result.at("value") == "explicit-secret");
  }
}

TEST_CASE("mcp stdio transport send to closed child stdin reports error",
          "[ava_mcp]") {
#if defined(_WIN32)
  SUCCEED("stdio transport closed-stdin coverage is POSIX-only");
#else
  ava::mcp::StdioTransport transport("/bin/sh", {"-c", "exec 0<&-; sleep 1"},
                                     {}, std::chrono::milliseconds(75));

  const auto deadline =
      std::chrono::steady_clock::now() + std::chrono::seconds(1);
  std::optional<std::string> send_error;
  std::uint64_t attempt = 0;
  while (std::chrono::steady_clock::now() < deadline &&
         !send_error.has_value()) {
    try {
      transport.send(ava::mcp::make_request(++attempt, "initialize",
                                            nlohmann::json::object()));
      std::this_thread::sleep_for(std::chrono::milliseconds(10));
    } catch (const std::runtime_error &error) {
      send_error = error.what();
    }
  }

  REQUIRE(send_error.has_value());
  const auto mentions_stdin_closed =
      send_error->find("stdin closed") != std::string::npos;
  const auto mentions_send_pipe =
      send_error->find("send pipe error") != std::string::npos;
  const auto mentions_failed_write =
      send_error->find("failed to write") != std::string::npos;
  REQUIRE(
      (mentions_stdin_closed || mentions_send_pipe || mentions_failed_write));
#endif
}

TEST_CASE(
    "mcp stdio transport close terminates the spawned child process group",
    "[ava_mcp]") {
#if defined(_WIN32)
  SUCCEED("process-group cleanup coverage is POSIX-only");
#else
  const auto pid_file = std::filesystem::temp_directory_path() /
                        "ava_mcp_stdio_group_cleanup.pid";
  std::filesystem::remove(pid_file);

  const auto script =
      "sleep 30 & bg=$!; printf '%s' \"$bg\" > \"$PID_FILE\"; "
      "printf '{\"jsonrpc\":\"2.0\",\"id\":1,\"result\":{}}\\n'; wait";

  std::optional<pid_t> background_pid;
  try {
    ava::mcp::StdioTransport transport("/bin/sh", {"-c", script},
                                       {{"PID_FILE", pid_file.string()}},
                                       std::chrono::milliseconds(500));

    const auto response = transport.receive();
    REQUIRE(response.id.has_value());
    REQUIRE(std::get<std::uint64_t>(*response.id) == 1);

    background_pid =
        read_pid_from_file_with_retry(pid_file, std::chrono::milliseconds(200));
    REQUIRE(background_pid.has_value());

    transport.close();

    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    REQUIRE_FALSE(process_is_alive(*background_pid));
  } catch (...) {
    if (background_pid.has_value() && process_is_alive(*background_pid)) {
      ::kill(*background_pid, SIGKILL);
    }
    std::filesystem::remove(pid_file);
    throw;
  }

  if (background_pid.has_value() && process_is_alive(*background_pid)) {
    ::kill(*background_pid, SIGKILL);
  }
  std::filesystem::remove(pid_file);
#endif
}

TEST_CASE("mcp stdio transport close tolerates concurrent callers",
          "[ava_mcp]") {
#if defined(_WIN32)
  SUCCEED("stdio transport coverage is POSIX-only");
#else
  ava::mcp::StdioTransport transport(
      "/bin/sh",
      {"-c",
       "printf '{\"jsonrpc\":\"2.0\",\"id\":1,\"result\":{}}\\n'; sleep 30"},
      {}, std::chrono::milliseconds(500));

  const auto response = transport.receive();
  REQUIRE(response.id.has_value());
  REQUIRE(std::get<std::uint64_t>(*response.id) == 1);

  std::thread first([&transport]() { transport.close(); });
  std::thread second([&transport]() { transport.close(); });

  first.join();
  second.join();
  REQUIRE_NOTHROW(transport.close());
#endif
}

TEST_CASE("mcp client runs initialize list tools and call tool flow",
          "[ava_mcp]") {
  auto transport = std::make_unique<ava::mcp::InMemoryTransport>();
  auto *raw_transport = transport.get();
  raw_transport->push_inbound(ava::mcp::make_result(
      1,
      nlohmann::json{{"protocolVersion", "2024-11-05"},
                     {"capabilities",
                      nlohmann::json{{"tools", nlohmann::json::object()}}}}));
  raw_transport->push_inbound(ava::mcp::make_notification(
      "notifications/progress", nlohmann::json{{"progress", 1}}));
  raw_transport->push_inbound(ava::mcp::make_result(
      2, nlohmann::json{
             {"tools", nlohmann::json::array({nlohmann::json{
                           {"name", "echo"},
                           {"description", "Echo input"},
                           {"inputSchema", nlohmann::json{{"type", "object"}}},
                       }})},
             {"nextCursor", "page-2"}}));
  raw_transport->push_inbound(ava::mcp::make_result(
      3, nlohmann::json{
             {"tools", nlohmann::json::array({nlohmann::json{
                           {"name", "reverse"},
                           {"description", "Reverse input"},
                           {"inputSchema", nlohmann::json{{"type", "object"}}},
                       }})}}));
  raw_transport->push_inbound(ava::mcp::make_request(99, "ping"));
  raw_transport->push_inbound(ava::mcp::make_result(
      4, nlohmann::json{{"content", nlohmann::json::array({nlohmann::json{
                                        {"type", "text"}, {"text", "hi"}}})}}));

  ava::mcp::McpClient client(std::move(transport), "mock");
  const auto capabilities = client.initialize();
  REQUIRE(capabilities.tools);
  auto outbound = raw_transport->pop_outbound();
  REQUIRE(outbound.method == std::optional<std::string>{"initialize"});
  REQUIRE(outbound.params.at("protocolVersion") == "2024-11-05");
  outbound = raw_transport->pop_outbound();
  REQUIRE(outbound.method ==
          std::optional<std::string>{"notifications/initialized"});

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

TEST_CASE("mcp client lists resources reads resources and handles prompts",
          "[ava_mcp]") {
  auto transport = std::make_unique<ava::mcp::InMemoryTransport>();
  auto *raw_transport = transport.get();
  raw_transport->push_inbound(ava::mcp::make_result(
      1, nlohmann::json{
             {"protocolVersion", "2024-11-05"},
             {"capabilities",
              nlohmann::json{{"resources", nlohmann::json::object()},
                             {"prompts", nlohmann::json::object()}}},
         }));
  raw_transport->push_inbound(ava::mcp::make_result(
      2, nlohmann::json{{"resources", nlohmann::json::array({nlohmann::json{
                                          {"uri", "file:///tmp/a.txt"},
                                          {"name", "a"},
                                          {"mimeType", "text/plain"}}})}}));
  raw_transport->push_inbound(ava::mcp::make_result(
      3, nlohmann::json{{"contents", nlohmann::json::array({nlohmann::json{
                                         {"uri", "file:///tmp/a.txt"},
                                         {"text", "hello"}}})}}));
  raw_transport->push_inbound(ava::mcp::make_result(
      4, nlohmann::json{{"prompts", nlohmann::json::array({nlohmann::json{
                                        {"name", "summarize"},
                                        {"description", "Summarize"}}})}}));
  raw_transport->push_inbound(ava::mcp::make_result(
      5, nlohmann::json{{"messages",
                         nlohmann::json::array({nlohmann::json{
                             {"role", "user"},
                             {"content", nlohmann::json{{"type", "text"},
                                                        {"text", "go"}}}}})}}));

  ava::mcp::McpClient client(std::move(transport), "mock");
  const auto capabilities = client.initialize();
  REQUIRE(capabilities.resources);
  REQUIRE(capabilities.prompts);
  REQUIRE(raw_transport->pop_outbound().method ==
          std::optional<std::string>{"initialize"});
  REQUIRE(raw_transport->pop_outbound().method ==
          std::optional<std::string>{"notifications/initialized"});

  const auto resources = client.list_resources();
  REQUIRE(resources.size() == 1);
  REQUIRE(resources.front().uri == "file:///tmp/a.txt");
  REQUIRE(resources.front().mime_type == "text/plain");
  auto outbound = raw_transport->pop_outbound();
  REQUIRE(outbound.method == std::optional<std::string>{"resources/list"});

  const auto resource = client.read_resource("file:///tmp/a.txt");
  REQUIRE(resource.at("contents").at(0).at("text") == "hello");
  outbound = raw_transport->pop_outbound();
  REQUIRE(outbound.method == std::optional<std::string>{"resources/read"});
  REQUIRE(outbound.params.at("uri") == "file:///tmp/a.txt");

  const auto prompts = client.list_prompts();
  REQUIRE(prompts.size() == 1);
  REQUIRE(prompts.front().name == "summarize");
  outbound = raw_transport->pop_outbound();
  REQUIRE(outbound.method == std::optional<std::string>{"prompts/list"});

  const auto prompt =
      client.get_prompt("summarize", nlohmann::json{{"topic", "mcp"}});
  REQUIRE(prompt.at("messages").at(0).at("content").at("text") == "go");
  outbound = raw_transport->pop_outbound();
  REQUIRE(outbound.method == std::optional<std::string>{"prompts/get"});
  REQUIRE(outbound.params.at("name") == "summarize");
  REQUIRE(outbound.params.at("arguments").at("topic") == "mcp");
}

TEST_CASE("mcp client truncates oversized tool resource and prompt text",
          "[ava_mcp]") {
  auto transport = std::make_unique<ava::mcp::InMemoryTransport>();
  auto *raw_transport = transport.get();
  raw_transport->push_inbound(ava::mcp::make_result(
      1,
      nlohmann::json{{"protocolVersion", "2024-11-05"},
                     {"capabilities",
                      nlohmann::json{{"tools", nlohmann::json::object()},
                                     {"resources", nlohmann::json::object()},
                                     {"prompts", nlohmann::json::object()}}}}));
  const std::string oversized(100'100, 'x');
  raw_transport->push_inbound(ava::mcp::make_result(
      2, nlohmann::json{
             {"content", nlohmann::json::array({nlohmann::json{
                             {"type", "text"}, {"text", oversized}}})}}));
  raw_transport->push_inbound(ava::mcp::make_result(
      3, nlohmann::json{{"contents", nlohmann::json::array({nlohmann::json{
                                         {"text", oversized}}})}}));
  raw_transport->push_inbound(ava::mcp::make_result(
      4, nlohmann::json{
             {"messages",
              nlohmann::json::array({nlohmann::json{
                  {"content", nlohmann::json{{"text", oversized}}}}})}}));

  ava::mcp::McpClient client(std::move(transport), "mock");
  client.initialize();
  (void)raw_transport->pop_outbound();
  (void)raw_transport->pop_outbound();

  auto tool = client.call_tool("echo", nlohmann::json::object());
  REQUIRE(tool.at("content").at(0).at("text").get<std::string>().size() <=
          100'000);
  REQUIRE(tool.at("content").at(0).at("text").get<std::string>().find(
              "truncated MCP output") != std::string::npos);
  auto resource = client.read_resource("file:///tmp/a.txt");
  REQUIRE(resource.at("contents").at(0).at("text").get<std::string>().size() <=
          100'000);
  REQUIRE(resource.at("contents")
              .at(0)
              .at("text")
              .get<std::string>()
              .find("truncated MCP output") != std::string::npos);
  auto prompt = client.get_prompt("summarize");
  REQUIRE(prompt.at("messages")
              .at(0)
              .at("content")
              .at("text")
              .get<std::string>()
              .size() <= 100'000);
  REQUIRE(prompt.at("messages")
              .at(0)
              .at("content")
              .at("text")
              .get<std::string>()
              .find("truncated MCP output") != std::string::npos);
}

TEST_CASE("mcp client rejects oversized binary payload strings", "[ava_mcp]") {
  auto transport = std::make_unique<ava::mcp::InMemoryTransport>();
  auto *raw_transport = transport.get();
  raw_transport->push_inbound(ava::mcp::make_result(
      1,
      nlohmann::json{{"protocolVersion", "2024-11-05"},
                     {"capabilities",
                      nlohmann::json{{"resources", nlohmann::json::object()},
                                     {"prompts", nlohmann::json::object()}}}}));
  const std::string oversized(100'100, 'a');
  raw_transport->push_inbound(ava::mcp::make_result(
      2, nlohmann::json{{"contents", nlohmann::json::array({nlohmann::json{
                                         {"blob", oversized}}})}}));
  raw_transport->push_inbound(ava::mcp::make_result(
      3, nlohmann::json{
             {"messages",
              nlohmann::json::array({nlohmann::json{
                  {"content", nlohmann::json{{"type", "image"},
                                             {"data", oversized}}}}})}}));

  ava::mcp::McpClient client(std::move(transport), "mock");
  client.initialize();

  REQUIRE_THROWS_WITH(client.read_resource("file:///tmp/a.bin"),
                      Catch::Matchers::ContainsSubstring("binary payload"));
  REQUIRE(client.health().consecutive_terminal_errors() == 1);
  REQUIRE_THROWS_WITH(client.get_prompt("image"),
                      Catch::Matchers::ContainsSubstring("binary payload"));
  REQUIRE(client.health().consecutive_terminal_errors() == 1);
}

TEST_CASE("mcp client paginates resources and prompts", "[ava_mcp]") {
  auto transport = std::make_unique<ava::mcp::InMemoryTransport>();
  auto *raw_transport = transport.get();
  raw_transport->push_inbound(ava::mcp::make_result(
      1, nlohmann::json{
             {"protocolVersion", "2024-11-05"},
             {"capabilities",
              nlohmann::json{{"resources", nlohmann::json::object()},
                             {"prompts", nlohmann::json::object()}}},
         }));
  raw_transport->push_inbound(ava::mcp::make_result(
      2, nlohmann::json{
             {"resources", nlohmann::json::array({nlohmann::json{
                               {"uri", "file:///tmp/a.txt"}, {"name", "a"}}})},
             {"nextCursor", "r2"}}));
  raw_transport->push_inbound(ava::mcp::make_result(
      3, nlohmann::json{{"resources",
                         nlohmann::json::array({nlohmann::json{
                             {"uri", "file:///tmp/b.txt"}, {"name", "b"}}})}}));
  raw_transport->push_inbound(ava::mcp::make_result(
      4, nlohmann::json{{"prompts", nlohmann::json::array({nlohmann::json{
                                        {"name", "summarize"}}})},
                        {"nextCursor", "p2"}}));
  raw_transport->push_inbound(ava::mcp::make_result(
      5, nlohmann::json{{"prompts", nlohmann::json::array({nlohmann::json{
                                        {"name", "rewrite"}}})}}));

  ava::mcp::McpClient client(std::move(transport), "mock");
  client.initialize();
  (void)raw_transport->pop_outbound();
  (void)raw_transport->pop_outbound();

  const auto resources = client.list_resources();
  REQUIRE(resources.size() == 2);
  REQUIRE(resources.at(0).uri == "file:///tmp/a.txt");
  REQUIRE(resources.at(1).uri == "file:///tmp/b.txt");
  REQUIRE(raw_transport->pop_outbound().method ==
          std::optional<std::string>{"resources/list"});
  auto outbound = raw_transport->pop_outbound();
  REQUIRE(outbound.method == std::optional<std::string>{"resources/list"});
  REQUIRE(outbound.params.at("cursor") == "r2");

  const auto prompts = client.list_prompts();
  REQUIRE(prompts.size() == 2);
  REQUIRE(prompts.at(0).name == "summarize");
  REQUIRE(prompts.at(1).name == "rewrite");
  REQUIRE(raw_transport->pop_outbound().method ==
          std::optional<std::string>{"prompts/list"});
  outbound = raw_transport->pop_outbound();
  REQUIRE(outbound.method == std::optional<std::string>{"prompts/list"});
  REQUIRE(outbound.params.at("cursor") == "p2");
}

TEST_CASE("mcp client response wait remains bounded under valid notification "
          "and ping noise",
          "[ava_mcp]") {
  auto state = std::make_shared<TimedNoiseTransport::State>();
  auto transport = std::make_unique<TimedNoiseTransport>(
      state, std::chrono::milliseconds(8));

  ava::mcp::McpClient client(std::move(transport), "noisy",
                             std::chrono::milliseconds(90));

  const auto started_at = std::chrono::steady_clock::now();
  REQUIRE_THROWS_WITH(client.initialize(),
                      Catch::Matchers::ContainsSubstring("timed out"));
  const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
      std::chrono::steady_clock::now() - started_at);
  REQUIRE(elapsed.count() < 500);
  REQUIRE(client.health().consecutive_terminal_errors() == 1);

  std::size_t pong_count = 0;
  for (const auto &outbound : state->outbound) {
    if (!outbound.method.has_value() && outbound.id.has_value() &&
        outbound.result.is_object()) {
      ++pong_count;
    }
  }
  REQUIRE(pong_count > 0);
}

TEST_CASE("mcp client enforces initialize and tools capability before tool "
          "operations",
          "[ava_mcp]") {
  auto transport = std::make_unique<ava::mcp::InMemoryTransport>();
  auto *raw_transport = transport.get();
  raw_transport->push_inbound(ava::mcp::make_result(
      1, nlohmann::json{{"protocolVersion", "2024-11-05"},
                        {"capabilities", nlohmann::json::object()}}));

  ava::mcp::McpClient client(std::move(transport), "mock");
  REQUIRE_THROWS_AS(client.list_tools(), std::runtime_error);
  client.initialize();
  REQUIRE_THROWS_AS(client.list_tools(), std::runtime_error);
  REQUIRE(raw_transport->pop_outbound().method ==
          std::optional<std::string>{"initialize"});
  REQUIRE(raw_transport->pop_outbound().method ==
          std::optional<std::string>{"notifications/initialized"});
}

TEST_CASE("mcp client enforces resources and prompts capabilities",
          "[ava_mcp]") {
  auto transport = std::make_unique<ava::mcp::InMemoryTransport>();
  auto *raw_transport = transport.get();
  raw_transport->push_inbound(ava::mcp::make_result(
      1, nlohmann::json{{"protocolVersion", "2024-11-05"},
                        {"capabilities", nlohmann::json::object()}}));

  ava::mcp::McpClient client(std::move(transport), "mock");
  REQUIRE_THROWS_AS(client.list_resources(), std::runtime_error);
  REQUIRE_THROWS_AS(client.read_resource("file:///tmp/a.txt"),
                    std::runtime_error);
  REQUIRE_THROWS_AS(client.list_prompts(), std::runtime_error);
  REQUIRE_THROWS_AS(client.get_prompt("summarize"), std::runtime_error);

  client.initialize();
  REQUIRE_THROWS_AS(client.list_resources(), std::runtime_error);
  REQUIRE_THROWS_AS(client.read_resource("file:///tmp/a.txt"),
                    std::runtime_error);
  REQUIRE_THROWS_AS(client.list_prompts(), std::runtime_error);
  REQUIRE_THROWS_AS(client.get_prompt("summarize"), std::runtime_error);
  REQUIRE(raw_transport->pop_outbound().method ==
          std::optional<std::string>{"initialize"});
  REQUIRE(raw_transport->pop_outbound().method ==
          std::optional<std::string>{"notifications/initialized"});
}

TEST_CASE("mcp client validates prompt get arguments before sending",
          "[ava_mcp]") {
  auto transport = std::make_unique<ava::mcp::InMemoryTransport>();
  auto *raw_transport = transport.get();
  raw_transport->push_inbound(ava::mcp::make_result(
      1,
      nlohmann::json{{"protocolVersion", "2024-11-05"},
                     {"capabilities",
                      nlohmann::json{{"prompts", nlohmann::json::object()}}}}));
  raw_transport->push_inbound(ava::mcp::make_result(
      2, nlohmann::json{{"messages",
                         nlohmann::json::array({nlohmann::json{
                             {"role", "user"},
                             {"content", nlohmann::json{{"type", "text"},
                                                        {"text", "ok"}}}}})}}));

  ava::mcp::McpClient client(std::move(transport), "mock");
  client.initialize();
  (void)raw_transport->pop_outbound();
  (void)raw_transport->pop_outbound();

  REQUIRE_THROWS_WITH(client.get_prompt("summarize", nlohmann::json::array()),
                      Catch::Matchers::ContainsSubstring("string values"));
  REQUIRE_THROWS_WITH(
      client.get_prompt("summarize", nlohmann::json{{"topic", 7}}),
      Catch::Matchers::ContainsSubstring("string values"));

  const auto prompt = client.get_prompt("summarize", nlohmann::json::object());
  REQUIRE(prompt.at("messages").at(0).at("content").at("text") == "ok");
  const auto outbound = raw_transport->pop_outbound();
  REQUIRE(outbound.method == std::optional<std::string>{"prompts/get"});
  REQUIRE(outbound.params.at("name") == "summarize");
  REQUIRE_FALSE(outbound.params.contains("arguments"));
}

TEST_CASE(
    "mcp client rejects malformed initialize capabilities as terminal health",
    "[ava_mcp]") {
  auto transport = std::make_unique<ava::mcp::InMemoryTransport>();
  auto *raw_transport = transport.get();
  raw_transport->push_inbound(
      ava::mcp::make_result(1, nlohmann::json{{"protocolVersion", "2024-11-05"},
                                              {"capabilities", "broken"}}));

  ava::mcp::McpClient client(std::move(transport), "mock");
  REQUIRE_THROWS_WITH(
      client.initialize(),
      Catch::Matchers::ContainsSubstring("capabilities must be an object"));
  REQUIRE(client.health().consecutive_terminal_errors() == 1);
}

TEST_CASE("mcp client records ping response send failures as terminal health",
          "[ava_mcp]") {
  auto transport = std::make_unique<PingReplyFailingTransport>();
  ava::mcp::McpClient client(std::move(transport), "mock",
                             std::chrono::milliseconds(50));

  REQUIRE_THROWS_WITH(client.initialize(), Catch::Matchers::ContainsSubstring(
                                               "ping reply send failed"));
  REQUIRE(client.health().consecutive_terminal_errors() == 1);
}

TEST_CASE("mcp client propagates errors and tracks terminal health",
          "[ava_mcp]") {
  auto transport = std::make_unique<ava::mcp::InMemoryTransport>();
  auto *raw_transport = transport.get();
  raw_transport->push_inbound(ava::mcp::make_error(1, -32603, "first"));
  raw_transport->push_inbound(ava::mcp::make_error(2, -32603, "second"));
  raw_transport->push_inbound(ava::mcp::make_error(3, -32603, "third"));
  raw_transport->push_inbound(ava::mcp::make_result(
      4, nlohmann::json{{"protocolVersion", "2024-11-05"},
                        {"capabilities", nlohmann::json::object()}}));

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

TEST_CASE(
    "mcp client treats response correlation failures as terminal health errors",
    "[ava_mcp]") {
  auto transport = std::make_unique<ava::mcp::InMemoryTransport>();
  auto *raw_transport = transport.get();
  raw_transport->push_inbound(
      ava::mcp::make_notification("notifications/progress"));
  raw_transport->push_inbound(
      ava::mcp::make_result(999, nlohmann::json::object()));
  raw_transport->push_inbound(
      ava::mcp::make_result(888, nlohmann::json::object()));
  raw_transport->push_inbound(ava::mcp::make_result(
      3, nlohmann::json{{"protocolVersion", "2024-11-05"}}));

  ava::mcp::McpClient client(std::move(transport), "mock");
  REQUIRE_THROWS_AS(client.initialize(), std::runtime_error);
  REQUIRE(client.health().consecutive_terminal_errors() == 1);
  REQUIRE_THROWS_AS(client.initialize(), std::runtime_error);
  REQUIRE(client.health().consecutive_terminal_errors() == 2);
  client.initialize();
  REQUIRE(client.health().consecutive_terminal_errors() == 0);
}

TEST_CASE("mcp client reports reconnect needed after three terminal failures",
          "[ava_mcp]") {
  auto transport = std::make_unique<ava::mcp::InMemoryTransport>();
  auto *raw_transport = transport.get();
  raw_transport->push_inbound(
      ava::mcp::make_result(999, nlohmann::json::object()));
  raw_transport->push_inbound(
      ava::mcp::make_result(998, nlohmann::json::object()));
  raw_transport->push_inbound(
      ava::mcp::make_result(997, nlohmann::json::object()));

  ava::mcp::McpClient client(std::move(transport), "mock");
  REQUIRE_THROWS_AS(client.initialize(), std::runtime_error);
  REQUIRE_THROWS_AS(client.initialize(), std::runtime_error);
  REQUIRE_FALSE(client.health().reconnect_needed());
  REQUIRE_THROWS_AS(client.initialize(), std::runtime_error);
  REQUIRE(client.health().reconnect_needed());
}

TEST_CASE("mcp valid server errors reset terminal health streak", "[ava_mcp]") {
  auto transport = std::make_unique<ava::mcp::InMemoryTransport>();
  auto *raw_transport = transport.get();
  raw_transport->push_inbound(
      ava::mcp::make_result(999, nlohmann::json::object()));
  raw_transport->push_inbound(
      ava::mcp::make_error(2, -32601, "unknown method"));
  raw_transport->push_inbound(
      ava::mcp::make_result(997, nlohmann::json::object()));
  raw_transport->push_inbound(
      ava::mcp::make_result(996, nlohmann::json::object()));

  ava::mcp::McpClient client(std::move(transport), "mock");
  REQUIRE_THROWS_AS(client.initialize(), std::runtime_error);
  REQUIRE(client.health().consecutive_terminal_errors() == 1);
  REQUIRE_THROWS_AS(client.initialize(), std::runtime_error);
  REQUIRE(client.health().consecutive_terminal_errors() == 0);
  REQUIRE_THROWS_AS(client.initialize(), std::runtime_error);
  REQUIRE_THROWS_AS(client.initialize(), std::runtime_error);
  REQUIRE_FALSE(client.health().reconnect_needed());
}

TEST_CASE("mcp client records transport failures as terminal health errors",
          "[ava_mcp]") {
  auto transport = std::make_unique<ava::mcp::InMemoryTransport>();
  auto *raw_transport = transport.get();
  raw_transport->close();

  ava::mcp::McpClient client(std::move(transport), "mock");
  REQUIRE_THROWS_AS(client.initialize(), std::runtime_error);
  REQUIRE(client.health().consecutive_terminal_errors() == 1);
  REQUIRE(raw_transport->closed());
}

TEST_CASE("mcp client rejects malformed tool results deterministically",
          "[ava_mcp]") {
  auto transport = std::make_unique<ava::mcp::InMemoryTransport>();
  auto *raw_transport = transport.get();
  raw_transport->push_inbound(ava::mcp::make_result(
      1,
      nlohmann::json{{"protocolVersion", "2024-11-05"},
                     {"capabilities",
                      nlohmann::json{{"tools", nlohmann::json::object()}}}}));
  raw_transport->push_inbound(
      ava::mcp::make_result(2, nlohmann::json::array()));
  raw_transport->push_inbound(
      ava::mcp::make_result(3, nlohmann::json::object()));

  ava::mcp::McpClient client(std::move(transport), "mock");
  client.initialize();
  REQUIRE_THROWS_AS(client.list_tools(), std::runtime_error);
  REQUIRE(client.health().consecutive_terminal_errors() == 1);
  REQUIRE_THROWS_AS(client.list_tools(), std::runtime_error);
}

TEST_CASE("mcp client records malformed tools list as terminal health",
          "[ava_mcp]") {
  auto transport = std::make_unique<ava::mcp::InMemoryTransport>();
  auto *raw_transport = transport.get();
  raw_transport->push_inbound(ava::mcp::make_result(
      1,
      nlohmann::json{{"protocolVersion", "2024-11-05"},
                     {"capabilities",
                      nlohmann::json{{"tools", nlohmann::json::object()}}}}));
  raw_transport->push_inbound(
      ava::mcp::make_result(2, nlohmann::json{{"nextCursor", "later"}}));

  ava::mcp::McpClient client(std::move(transport), "mock");
  client.initialize();
  REQUIRE_THROWS_AS(client.list_tools(), std::runtime_error);
  REQUIRE(client.health().consecutive_terminal_errors() == 1);
}

TEST_CASE("mcp client records malformed resources and prompts lists as "
          "terminal health",
          "[ava_mcp]") {
  SECTION("resources") {
    auto transport = std::make_unique<ava::mcp::InMemoryTransport>();
    auto *raw_transport = transport.get();
    raw_transport->push_inbound(ava::mcp::make_result(
        1, nlohmann::json{
               {"protocolVersion", "2024-11-05"},
               {"capabilities",
                nlohmann::json{{"resources", nlohmann::json::object()}}}}));
    raw_transport->push_inbound(
        ava::mcp::make_result(2, nlohmann::json{{"nextCursor", "later"}}));
    raw_transport->push_inbound(ava::mcp::make_result(
        3,
        nlohmann::json{{"resources", nlohmann::json::array({nlohmann::json{
                                         {"uri", ""}, {"name", "empty"}}})}}));

    ava::mcp::McpClient client(std::move(transport), "mock");
    client.initialize();
    REQUIRE_THROWS_AS(client.list_resources(), std::runtime_error);
    REQUIRE(client.health().consecutive_terminal_errors() == 1);
    REQUIRE_THROWS_WITH(client.list_resources(),
                        Catch::Matchers::ContainsSubstring("empty or blank"));
    REQUIRE(client.health().consecutive_terminal_errors() == 1);
  }

  SECTION("prompts") {
    auto transport = std::make_unique<ava::mcp::InMemoryTransport>();
    auto *raw_transport = transport.get();
    raw_transport->push_inbound(ava::mcp::make_result(
        1, nlohmann::json{
               {"protocolVersion", "2024-11-05"},
               {"capabilities",
                nlohmann::json{{"prompts", nlohmann::json::object()}}}}));
    raw_transport->push_inbound(
        ava::mcp::make_result(2, nlohmann::json{{"nextCursor", "later"}}));
    raw_transport->push_inbound(ava::mcp::make_result(
        3, nlohmann::json{{"prompts", nlohmann::json::array({nlohmann::json{
                                          {"name", "   "}}})}}));

    ava::mcp::McpClient client(std::move(transport), "mock");
    client.initialize();
    REQUIRE_THROWS_AS(client.list_prompts(), std::runtime_error);
    REQUIRE(client.health().consecutive_terminal_errors() == 1);
    REQUIRE_THROWS_WITH(client.list_prompts(),
                        Catch::Matchers::ContainsSubstring("empty or blank"));
    REQUIRE(client.health().consecutive_terminal_errors() == 1);
  }
}

TEST_CASE("mcp client rejects malformed resource read and prompt get results",
          "[ava_mcp]") {
  auto transport = std::make_unique<ava::mcp::InMemoryTransport>();
  auto *raw_transport = transport.get();
  raw_transport->push_inbound(ava::mcp::make_result(
      1, nlohmann::json{
             {"protocolVersion", "2024-11-05"},
             {"capabilities",
              nlohmann::json{{"resources", nlohmann::json::object()},
                             {"prompts", nlohmann::json::object()}}},
         }));
  raw_transport->push_inbound(
      ava::mcp::make_result(2, nlohmann::json::object()));
  raw_transport->push_inbound(
      ava::mcp::make_result(3, nlohmann::json::object()));

  ava::mcp::McpClient client(std::move(transport), "mock");
  client.initialize();
  REQUIRE_THROWS_WITH(client.read_resource("file:///tmp/a.txt"),
                      Catch::Matchers::ContainsSubstring("contents array"));
  REQUIRE(client.health().consecutive_terminal_errors() == 1);
  REQUIRE_THROWS_WITH(client.get_prompt("summarize"),
                      Catch::Matchers::ContainsSubstring("messages array"));
  REQUIRE(client.health().consecutive_terminal_errors() == 1);
}

TEST_CASE("mcp client rejects empty or blank tool names from tools/list",
          "[ava_mcp]") {
  auto transport = std::make_unique<ava::mcp::InMemoryTransport>();
  auto *raw_transport = transport.get();
  raw_transport->push_inbound(ava::mcp::make_result(
      1,
      nlohmann::json{{"protocolVersion", "2024-11-05"},
                     {"capabilities",
                      nlohmann::json{{"tools", nlohmann::json::object()}}}}));
  raw_transport->push_inbound(ava::mcp::make_result(
      2, nlohmann::json{
             {"tools",
              nlohmann::json::array({nlohmann::json{
                  {"name", ""},
                  {"inputSchema", nlohmann::json{{"type", "object"}}}}})}}));
  raw_transport->push_inbound(ava::mcp::make_result(
      3, nlohmann::json{
             {"tools",
              nlohmann::json::array({nlohmann::json{
                  {"name", "   "},
                  {"inputSchema", nlohmann::json{{"type", "object"}}}}})}}));

  ava::mcp::McpClient client(std::move(transport), "mock");
  client.initialize();

  REQUIRE_THROWS_WITH(client.list_tools(),
                      Catch::Matchers::ContainsSubstring("empty or blank"));
  REQUIRE(client.health().consecutive_terminal_errors() == 1);

  REQUIRE_THROWS_WITH(client.list_tools(),
                      Catch::Matchers::ContainsSubstring("empty or blank"));
  REQUIRE(client.health().consecutive_terminal_errors() == 1);
}

TEST_CASE("mcp initialize notification send failure is terminal health",
          "[ava_mcp]") {
  auto transport = std::make_unique<FailingInitializedNotificationTransport>();
  ava::mcp::McpClient client(std::move(transport), "mock");

  REQUIRE_THROWS_AS(client.initialize(), std::runtime_error);
  REQUIRE(client.health().consecutive_terminal_errors() == 1);
}

TEST_CASE("mcp manager isolates server init failures and routes tool calls",
          "[ava_mcp]") {
  auto good_state = std::make_shared<ScriptedTransport::State>();
  good_state->inbound.push_back(ava::mcp::make_result(
      1,
      nlohmann::json{{"protocolVersion", "2024-11-05"},
                     {"capabilities",
                      nlohmann::json{{"tools", nlohmann::json::object()}}}}));
  good_state->inbound.push_back(ava::mcp::make_result(
      2, nlohmann::json{
             {"tools",
              nlohmann::json::array({nlohmann::json{
                  {"name", "echo"},
                  {"description", "Echo"},
                  {"inputSchema", nlohmann::json{{"type", "object"}}}}})}}));
  good_state->inbound.push_back(ava::mcp::make_result(
      3, nlohmann::json{{"content", nlohmann::json::array({nlohmann::json{
                                        {"type", "text"}, {"text", "hello"}}})},
                        {"isError", false}}));

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

  ava::mcp::McpManager manager(
      [good_state](const ava::mcp::McpServerConfig &server)
          -> std::unique_ptr<ava::mcp::McpTransport> {
        if (server.name == "good") {
          return std::make_unique<ScriptedTransport>(good_state);
        }
        if (server.name == "bad") {
          return std::make_unique<FailingTransport>();
        }
        return std::make_unique<FailingTransport>();
      });

  const auto reports =
      manager.initialize(ava::mcp::McpConfig{.servers = {good, bad, disabled}});
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

  const auto result =
      manager.call_tool("good", "echo", nlohmann::json{{"text", "hello"}});
  REQUIRE(result.at("content").at(0).at("text") == "hello");

  REQUIRE(good_state->outbound.size() == 4);
  REQUIRE(good_state->outbound.at(0).method ==
          std::optional<std::string>{"initialize"});
  REQUIRE(good_state->outbound.at(1).method ==
          std::optional<std::string>{"notifications/initialized"});
  REQUIRE(good_state->outbound.at(2).method ==
          std::optional<std::string>{"tools/list"});
  REQUIRE(good_state->outbound.at(3).method ==
          std::optional<std::string>{"tools/call"});
  REQUIRE(good_state->outbound.at(3).params.at("name") == "echo");

  manager.shutdown();
  REQUIRE(good_state->closed);
  REQUIRE(manager.server_count() == 0);
  REQUIRE(manager.tool_count() == 0);
  REQUIRE_FALSE(manager.server_report("good").has_value());
  REQUIRE_NOTHROW(manager.shutdown());
}

TEST_CASE("mcp manager supports resource and prompt only servers",
          "[ava_mcp]") {
  auto state = std::make_shared<ScriptedTransport::State>();
  state->inbound.push_back(ava::mcp::make_result(
      1, nlohmann::json{
             {"protocolVersion", "2024-11-05"},
             {"capabilities",
              nlohmann::json{{"resources", nlohmann::json::object()},
                             {"prompts", nlohmann::json::object()}}},
         }));
  state->inbound.push_back(ava::mcp::make_result(
      2, nlohmann::json{{"resources",
                         nlohmann::json::array({nlohmann::json{
                             {"uri", "file:///tmp/a.txt"}, {"name", "a"}}})}}));
  state->inbound.push_back(ava::mcp::make_result(
      3, nlohmann::json{{"contents", nlohmann::json::array({nlohmann::json{
                                         {"uri", "file:///tmp/a.txt"},
                                         {"text", "hello"}}})}}));
  state->inbound.push_back(ava::mcp::make_result(
      4, nlohmann::json{{"prompts", nlohmann::json::array({nlohmann::json{
                                        {"name", "summarize"}}})}}));
  state->inbound.push_back(ava::mcp::make_result(
      5, nlohmann::json{{"messages",
                         nlohmann::json::array({nlohmann::json{
                             {"role", "user"},
                             {"content", nlohmann::json{{"type", "text"},
                                                        {"text", "go"}}}}})}}));

  ava::mcp::McpServerConfig server;
  server.name = "content";
  server.transport_type = ava::mcp::TransportType::Stdio;
  server.stdio.command = "mock";

  ava::mcp::McpManager manager([state](const ava::mcp::McpServerConfig &) {
    return std::make_unique<ScriptedTransport>(state);
  });

  const auto reports =
      manager.initialize(ava::mcp::McpConfig{.servers = {server}});
  REQUIRE(reports.size() == 1);
  REQUIRE(reports.front().connected);
  REQUIRE(reports.front().tool_count == 0);
  REQUIRE(manager.server_count() == 1);
  REQUIRE(manager.tool_count() == 0);

  const auto resources = manager.list_resources("content");
  REQUIRE(resources.size() == 1);
  REQUIRE(resources.front().uri == "file:///tmp/a.txt");

  const auto resource = manager.read_resource("content", "file:///tmp/a.txt");
  REQUIRE(resource.at("contents").at(0).at("text") == "hello");

  const auto prompts = manager.list_prompts("content");
  REQUIRE(prompts.size() == 1);
  REQUIRE(prompts.front().name == "summarize");

  const auto prompt = manager.get_prompt("content", "summarize",
                                         nlohmann::json{{"topic", "mcp"}});
  REQUIRE(prompt.at("messages").at(0).at("content").at("text") == "go");

  REQUIRE_THROWS_WITH(manager.list_resources(""),
                      Catch::Matchers::ContainsSubstring("must not be empty"));
  REQUIRE_THROWS_WITH(manager.list_resources("missing"),
                      Catch::Matchers::ContainsSubstring("not connected"));

  REQUIRE(state->outbound.size() == 6);
  REQUIRE(state->outbound.at(0).method ==
          std::optional<std::string>{"initialize"});
  REQUIRE(state->outbound.at(1).method ==
          std::optional<std::string>{"notifications/initialized"});
  REQUIRE(state->outbound.at(2).method ==
          std::optional<std::string>{"resources/list"});
  REQUIRE(state->outbound.at(3).method ==
          std::optional<std::string>{"resources/read"});
  REQUIRE(state->outbound.at(3).params.at("uri") == "file:///tmp/a.txt");
  REQUIRE(state->outbound.at(4).method ==
          std::optional<std::string>{"prompts/list"});
  REQUIRE(state->outbound.at(5).method ==
          std::optional<std::string>{"prompts/get"});
  REQUIRE(state->outbound.at(5).params.at("name") == "summarize");
  REQUIRE(state->outbound.at(5).params.at("arguments").at("topic") == "mcp");
}

TEST_CASE("mcp manager preserves non-tool capabilities when tool listing fails",
          "[ava_mcp]") {
  auto state = std::make_shared<ScriptedTransport::State>();
  state->inbound.push_back(ava::mcp::make_result(
      1, nlohmann::json{
             {"protocolVersion", "2024-11-05"},
             {"capabilities",
              nlohmann::json{{"tools", nlohmann::json::object()},
                             {"resources", nlohmann::json::object()}}},
         }));
  state->inbound.push_back(
      ava::mcp::make_result(2, nlohmann::json{{"tools", "broken"}}));
  state->inbound.push_back(ava::mcp::make_result(
      3, nlohmann::json{{"resources",
                         nlohmann::json::array({nlohmann::json{
                             {"uri", "file:///tmp/a.txt"}, {"name", "a"}}})}}));

  ava::mcp::McpServerConfig server;
  server.name = "mixed";
  server.transport_type = ava::mcp::TransportType::Stdio;
  server.stdio.command = "mock";

  ava::mcp::McpManager manager([state](const ava::mcp::McpServerConfig &) {
    return std::make_unique<ScriptedTransport>(state);
  });

  const auto reports =
      manager.initialize(ava::mcp::McpConfig{.servers = {server}});
  REQUIRE(reports.size() == 1);
  REQUIRE(reports.front().connected);
  REQUIRE(reports.front().tool_count == 0);
  REQUIRE(reports.front().error.has_value());
  REQUIRE_THAT(*reports.front().error,
               Catch::Matchers::ContainsSubstring("tools/list failed"));
  REQUIRE(manager.has_server("mixed"));

  const auto resources = manager.list_resources("mixed");
  REQUIRE(resources.size() == 1);
  REQUIRE(resources.front().uri == "file:///tmp/a.txt");

  REQUIRE(state->outbound.size() == 4);
  REQUIRE(state->outbound.at(0).method ==
          std::optional<std::string>{"initialize"});
  REQUIRE(state->outbound.at(1).method ==
          std::optional<std::string>{"notifications/initialized"});
  REQUIRE(state->outbound.at(2).method ==
          std::optional<std::string>{"tools/list"});
  REQUIRE(state->outbound.at(3).method ==
          std::optional<std::string>{"resources/list"});
}

TEST_CASE("mcp manager rejects calls to missing server or tool", "[ava_mcp]") {
  auto state = std::make_shared<ScriptedTransport::State>();
  state->inbound.push_back(ava::mcp::make_result(
      1,
      nlohmann::json{{"protocolVersion", "2024-11-05"},
                     {"capabilities",
                      nlohmann::json{{"tools", nlohmann::json::object()}}}}));
  state->inbound.push_back(ava::mcp::make_result(
      2, nlohmann::json{
             {"tools",
              nlohmann::json::array({nlohmann::json{
                  {"name", "echo"},
                  {"inputSchema", nlohmann::json{{"type", "object"}}}}})}}));

  ava::mcp::McpServerConfig server;
  server.name = "one";
  server.transport_type = ava::mcp::TransportType::Stdio;
  server.stdio.command = "mock";

  ava::mcp::McpManager manager([state](const ava::mcp::McpServerConfig &) {
    return std::make_unique<ScriptedTransport>(state);
  });

  manager.initialize(ava::mcp::McpConfig{.servers = {server}});

  REQUIRE_THROWS_WITH(
      manager.call_tool("missing", "echo", nlohmann::json::object()),
      Catch::Matchers::ContainsSubstring("not connected"));
  REQUIRE_THROWS_WITH(
      manager.call_tool("one", "missing", nlohmann::json::object()),
      Catch::Matchers::ContainsSubstring("is not registered"));
}

TEST_CASE("mcp manager isolates non-standard server initialization failures",
          "[ava_mcp]") {
  auto state = std::make_shared<ScriptedTransport::State>();
  state->inbound.push_back(ava::mcp::make_result(
      1,
      nlohmann::json{{"protocolVersion", "2024-11-05"},
                     {"capabilities",
                      nlohmann::json{{"tools", nlohmann::json::object()}}}}));
  state->inbound.push_back(ava::mcp::make_result(
      2, nlohmann::json{
             {"tools",
              nlohmann::json::array({nlohmann::json{
                  {"name", "echo"},
                  {"inputSchema", nlohmann::json{{"type", "object"}}}}})}}));

  ava::mcp::McpServerConfig stable;
  stable.name = "stable";
  stable.transport_type = ava::mcp::TransportType::Stdio;
  stable.stdio.command = "mock";

  ava::mcp::McpServerConfig unstable = stable;
  unstable.name = "unstable";

  ava::mcp::McpManager manager([state](const ava::mcp::McpServerConfig &server)
                                   -> std::unique_ptr<ava::mcp::McpTransport> {
    if (server.name == "unstable") {
      throw 7;
    }
    return std::make_unique<ScriptedTransport>(state);
  });

  const auto reports =
      manager.initialize(ava::mcp::McpConfig{.servers = {stable, unstable}});
  REQUIRE(reports.size() == 2);
  REQUIRE(manager.server_count() == 1);
  REQUIRE(manager.has_server("stable"));
  REQUIRE_FALSE(manager.has_server("unstable"));

  const auto unstable_report = manager.server_report("unstable");
  REQUIRE(unstable_report.has_value());
  REQUIRE_FALSE(unstable_report->connected);
  REQUIRE(unstable_report->error.has_value());
  REQUIRE_THAT(*unstable_report->error,
               Catch::Matchers::ContainsSubstring("non-standard exception"));
}

TEST_CASE(
    "mcp manager reports remote transports as unavailable without leaking auth",
    "[ava_mcp]") {
#if AVA_WITH_CPR
  SKIP(
      "MCP HTTP manager fail-closed test is scoped to AVA_WITH_CPR=OFF builds");
#else
  const ScopedEnvVar token_env("AVA_MCP_REMOTE_TOKEN",
                               "top-secret-token-value");

  ava::mcp::McpServerConfig remote;
  remote.name = "remote";
  remote.transport_type = ava::mcp::TransportType::Http;
  remote.remote.url = "https://mcp.example.invalid/rpc";
  remote.remote.auth.bearer_token_env = "AVA_MCP_REMOTE_TOKEN";

  ava::mcp::McpManager manager;
  const auto reports =
      manager.initialize(ava::mcp::McpConfig{.servers = {remote}});

  REQUIRE(reports.size() == 1);
  REQUIRE_FALSE(reports.front().connected);
  REQUIRE(reports.front().error.has_value());
  REQUIRE_THAT(*reports.front().error,
               Catch::Matchers::ContainsSubstring("AVA_WITH_CPR=ON"));
  REQUIRE_THAT(*reports.front().error,
               Catch::Matchers::ContainsSubstring("MCP HTTP transport"));
  REQUIRE(reports.front().error->find("AVA_MCP_REMOTE_TOKEN") ==
          std::string::npos);
  REQUIRE(reports.front().error->find("top-secret-token-value") ==
          std::string::npos);
  REQUIRE(manager.server_count() == 0);
#endif
}

TEST_CASE("mcp manager reports sse transports as unavailable without leaking "
          "oauth descriptors",
          "[ava_mcp]") {
  ava::mcp::McpServerConfig remote;
  remote.name = "remote-sse";
  remote.transport_type = ava::mcp::TransportType::Sse;
  remote.remote.url = "https://mcp.example.invalid/sse";
  remote.remote.auth.bearer_token_env = "AVA_MCP_REMOTE_BEARER_TOKEN";
  remote.remote.auth.oauth_issuer = "https://issuer.example.invalid";
  remote.remote.auth.oauth_client_id = "ava-client-id";
  remote.remote.auth.oauth_scopes = {"mcp:read", "mcp:call"};

  ava::mcp::McpManager manager;
  const auto reports =
      manager.initialize(ava::mcp::McpConfig{.servers = {remote}});

  REQUIRE(reports.size() == 1);
  REQUIRE_FALSE(reports.front().connected);
  REQUIRE(reports.front().error.has_value());
  REQUIRE_THAT(*reports.front().error,
               Catch::Matchers::ContainsSubstring("sse transport"));
  REQUIRE_THAT(*reports.front().error,
               Catch::Matchers::ContainsSubstring("not yet executable"));
  REQUIRE(reports.front().error->find("AVA_MCP_REMOTE_BEARER_TOKEN") ==
          std::string::npos);
  REQUIRE(reports.front().error->find("issuer.example.invalid") ==
          std::string::npos);
  REQUIRE(reports.front().error->find("ava-client-id") == std::string::npos);
  REQUIRE(manager.server_count() == 0);
}

TEST_CASE("mcp config parses stdio and guarded remote transports",
          "[ava_mcp]") {
  const auto parsed = ava::mcp::parse_mcp_config_json(nlohmann::json{
      {"servers", nlohmann::json::array({nlohmann::json{
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
  REQUIRE(parsed.servers.at(0).stdio.args ==
          std::vector<std::string>{"--root", "/tmp"});
  REQUIRE(parsed.servers.at(0).stdio.env.at("TOKEN") == "redacted");
  REQUIRE(parsed.servers.at(0).stdio.receive_timeout_ms == 1200);

  const auto defaults = ava::mcp::parse_mcp_config_json(nlohmann::json{
      {"servers", nlohmann::json::array({nlohmann::json{
                      {"name", "defaults"},
                      {"transport", nlohmann::json{{"type", "stdio"},
                                                   {"command", "mcp-server"}}},
                  }})},
  });
  REQUIRE(defaults.servers.at(0).stdio.receive_timeout_ms == 5000);

  const auto remote = ava::mcp::parse_mcp_config_json(nlohmann::json{
      {"servers",
       nlohmann::json::array({
           nlohmann::json{
               {"name", "remote-http"},
               {"transport",
                nlohmann::json{
                    {"type", "http"},
                    {"url", "HTTPS://example.invalid/mcp"},
                    {"headers", nlohmann::json{{"X-Ava-Test", "ok"}}},
                    {"bearerTokenEnv", "AVA_MCP_TOKEN"},
                    {"requestTimeoutMs", 2500},
                    {"oauth",
                     nlohmann::json{
                         {"issuer", "https://auth.example.invalid"},
                         {"clientId", "ava-test"},
                         {"scopes",
                          nlohmann::json::array({"mcp:read", "mcp:call"})},
                     }},
                }},
           },
           nlohmann::json{
               {"name", "remote-sse"},
               {"transport",
                nlohmann::json{{"type", "sse"},
                               {"url", "http://localhost:3000/sse"}}},
           },
       })},
  });
  REQUIRE(remote.servers.size() == 2);
  REQUIRE(remote.servers.at(0).transport_type == ava::mcp::TransportType::Http);
  REQUIRE(remote.servers.at(0).remote.url == "HTTPS://example.invalid/mcp");
  REQUIRE(remote.servers.at(0).remote.headers.at("X-Ava-Test") == "ok");
  REQUIRE(remote.servers.at(0).remote.auth.bearer_token_env == "AVA_MCP_TOKEN");
  REQUIRE(remote.servers.at(0).remote.auth.oauth_issuer ==
          "https://auth.example.invalid");
  REQUIRE(remote.servers.at(0).remote.auth.oauth_client_id == "ava-test");
  REQUIRE(remote.servers.at(0).remote.auth.oauth_scopes ==
          std::vector<std::string>{"mcp:read", "mcp:call"});
  REQUIRE(remote.servers.at(0).remote.request_timeout_ms == 2500);
  REQUIRE(remote.servers.at(1).transport_type == ava::mcp::TransportType::Sse);
  REQUIRE(remote.servers.at(1).remote.request_timeout_ms == 5000);

  REQUIRE_THROWS_AS(
      ava::mcp::parse_mcp_config_json(nlohmann::json{
          {"servers",
           nlohmann::json::array({nlohmann::json{
               {"name", 42},
               {"transport",
                nlohmann::json{{"type", "stdio"}, {"command", "server"}}}}})},
      }),
      std::runtime_error);

  REQUIRE_THROWS_AS(
      ava::mcp::parse_mcp_config_json(nlohmann::json{
          {"servers",
           nlohmann::json::array({nlohmann::json{
               {"name", "bad-timeout"},
               {"transport", nlohmann::json{{"type", "stdio"},
                                            {"command", "server"},
                                            {"receiveTimeoutMs", 0}}}}})},
      }),
      std::runtime_error);

  REQUIRE_THROWS_WITH(
      ava::mcp::parse_mcp_config_json(nlohmann::json{
          {"servers",
           nlohmann::json::array({nlohmann::json{
               {"name", "abusive-timeout"},
               {"transport", nlohmann::json{{"type", "stdio"},
                                            {"command", "server"},
                                            {"receiveTimeoutMs", 600000}}}}})},
      }),
      Catch::Matchers::ContainsSubstring("must not exceed"));

  REQUIRE_THROWS_WITH(
      ava::mcp::parse_mcp_config_json(nlohmann::json{
          {"servers",
           nlohmann::json::array({nlohmann::json{
               {"name", "bad-url"},
               {"transport",
                nlohmann::json{{"type", "http"}, {"url", "file:///tmp/mcp"}}},
           }})},
      }),
      Catch::Matchers::ContainsSubstring("http:// or https://"));

  REQUIRE_THROWS_WITH(
      ava::mcp::parse_mcp_config_json(nlohmann::json{
          {"servers",
           nlohmann::json::array({nlohmann::json{
               {"name", "inline-auth"},
               {"transport",
                nlohmann::json{{"type", "http"},
                               {"url", "https://example.invalid"},
                               {"headers", nlohmann::json{{"AUTHORIZATION",
                                                           "Bearer secret"}}}}},
           }})},
      }),
      Catch::Matchers::ContainsSubstring("inline credentials"));

  REQUIRE_THROWS_WITH(
      ava::mcp::parse_mcp_config_json(nlohmann::json{
          {"servers",
           nlohmann::json::array({nlohmann::json{
               {"name", "empty-token-env"},
               {"transport", nlohmann::json{{"type", "http"},
                                            {"url", "https://example.invalid"},
                                            {"bearerTokenEnv", ""}}},
           }})},
      }),
      Catch::Matchers::ContainsSubstring("bearerTokenEnv must not be empty"));

  REQUIRE_THROWS_WITH(
      ava::mcp::parse_mcp_config_json(nlohmann::json{
          {"servers",
           nlohmann::json::array({nlohmann::json{
               {"name", "sse-accept"},
               {"transport",
                nlohmann::json{
                    {"type", "sse"},
                    {"url", "https://example.invalid"},
                    {"headers", nlohmann::json{{"accept", "text/plain"}}}}},
           }})},
      }),
      Catch::Matchers::ContainsSubstring(
          "SSE transport owns the Accept header"));

  REQUIRE_THROWS_WITH(
      ava::mcp::parse_mcp_config_json(nlohmann::json{
          {"servers",
           nlohmann::json::array({nlohmann::json{
               {"name", "bad-remote-timeout"},
               {"transport", nlohmann::json{{"type", "http"},
                                            {"url", "https://example.invalid"},
                                            {"requestTimeoutMs", 600000}}},
           }})},
      }),
      Catch::Matchers::ContainsSubstring("requestTimeoutMs must not exceed"));

  REQUIRE_THROWS_WITH(
      ava::mcp::parse_mcp_config_json(nlohmann::json{
          {"servers",
           nlohmann::json::array({nlohmann::json{
               {"name", "inline-oauth"},
               {"transport",
                nlohmann::json{
                    {"type", "sse"},
                    {"url", "https://example.invalid"},
                    {"oauth", nlohmann::json{{"refreshToken", "secret"}}}}},
           }})},
      }),
      Catch::Matchers::ContainsSubstring("inline credential field"));

  REQUIRE_THROWS_WITH(
      ava::mcp::parse_mcp_config_json(nlohmann::json{
          {"servers",
           nlohmann::json::array({nlohmann::json{
               {"name", "bad-scopes"},
               {"transport",
                nlohmann::json{
                    {"type", "http"},
                    {"url", "https://example.invalid"},
                    {"oauth", nlohmann::json{{"scopes", "mcp:read"}}}}},
           }})},
      }),
      Catch::Matchers::ContainsSubstring(
          "MCP remote transport oauth scopes must be an array"));
}

TEST_CASE("mcp config rejects URL userinfo in remote transport urls",
          "[ava_mcp]") {
  REQUIRE_THROWS_WITH(
      ava::mcp::parse_mcp_config_json(nlohmann::json{
          {"servers",
           nlohmann::json::array({nlohmann::json{
               {"name", "userinfo-http"},
               {"transport",
                nlohmann::json{{"type", "http"},
                               {"url", "https://user:pass@example.invalid"}}},
           }})},
      }),
      Catch::Matchers::ContainsSubstring("must not include URL userinfo"));

  REQUIRE_THROWS_WITH(
      ava::mcp::parse_mcp_config_json(nlohmann::json{
          {"servers",
           nlohmann::json::array({nlohmann::json{
               {"name", "userinfo-sse"},
               {"transport",
                nlohmann::json{{"type", "sse"},
                               {"url", "https://user@example.invalid/sse"}}},
           }})},
      }),
      Catch::Matchers::ContainsSubstring("must not include URL userinfo"));
}

TEST_CASE("mcp config rejects http auth when bearer token env is configured",
          "[ava_mcp]") {
  REQUIRE_THROWS_WITH(
      ava::mcp::parse_mcp_config_json(nlohmann::json{
          {"servers", nlohmann::json::array({nlohmann::json{
                          {"name", "http-bearer-auth"},
                          {"transport",
                           nlohmann::json{{"type", "http"},
                                          {"url", "http://example.invalid"},
                                          {"bearerTokenEnv", "AVA_MCP_TOKEN"}}},
                      }})},
      }),
      Catch::Matchers::ContainsSubstring(
          "with authentication requires https://"));
}

TEST_CASE("mcp config rejects http auth when oauth descriptors are configured",
          "[ava_mcp]") {
  REQUIRE_THROWS_WITH(
      ava::mcp::parse_mcp_config_json(nlohmann::json{
          {"servers",
           nlohmann::json::array({nlohmann::json{
               {"name", "http-oauth-descriptors"},
               {"transport",
                nlohmann::json{
                    {"type", "http"},
                    {"url", "http://example.invalid"},
                    {"oauth", nlohmann::json{{"issuer", "https://issuer"},
                                             {"clientId", "ava"}}}}},
           }})},
      }),
      Catch::Matchers::ContainsSubstring(
          "with authentication requires https://"));

  REQUIRE_THROWS_WITH(
      ava::mcp::parse_mcp_config_json(nlohmann::json{
          {"servers",
           nlohmann::json::array({nlohmann::json{
               {"name", "http-oauth-scopes"},
               {"transport",
                nlohmann::json{
                    {"type", "http"},
                    {"url", "http://example.invalid"},
                    {"oauth", nlohmann::json{{"scopes", nlohmann::json::array(
                                                            {"mcp:call"})}}}}},
           }})},
      }),
      Catch::Matchers::ContainsSubstring(
          "with authentication requires https://"));
}

TEST_CASE("mcp config rejects credential-bearing proxy and cookie headers",
          "[ava_mcp]") {
  REQUIRE_THROWS_WITH(
      ava::mcp::parse_mcp_config_json(nlohmann::json{
          {"servers",
           nlohmann::json::array({nlohmann::json{
               {"name", "inline-proxy-auth"},
               {"transport",
                nlohmann::json{
                    {"type", "http"},
                    {"url", "https://example.invalid"},
                    {"headers",
                     nlohmann::json{{"Proxy-Authorization", "Basic secret"}}}}},
           }})},
      }),
      Catch::Matchers::ContainsSubstring("Proxy-Authorization"));

  REQUIRE_THROWS_WITH(
      ava::mcp::parse_mcp_config_json(nlohmann::json{
          {"servers",
           nlohmann::json::array({nlohmann::json{
               {"name", "inline-cookie"},
               {"transport",
                nlohmann::json{
                    {"type", "sse"},
                    {"url", "https://example.invalid"},
                    {"headers", nlohmann::json{{"Cookie", "session=secret"}}}}},
           }})},
      }),
      Catch::Matchers::ContainsSubstring("Cookie"));
}

TEST_CASE("mcp config rejects invalid remote header syntax", "[ava_mcp]") {
  REQUIRE_THROWS_WITH(
      ava::mcp::parse_mcp_config_json(nlohmann::json{
          {"servers",
           nlohmann::json::array({nlohmann::json{
               {"name", "invalid-header-name-with-colon"},
               {"transport",
                nlohmann::json{
                    {"type", "http"},
                    {"url", "https://example.invalid"},
                    {"headers",
                     nlohmann::json{{"Cookie: session=secret", "ignored"}}}}},
           }})},
      }),
      Catch::Matchers::ContainsSubstring(
          "header name must be a valid HTTP field-name token"));

  REQUIRE_THROWS_WITH(
      ava::mcp::parse_mcp_config_json(nlohmann::json{
          {"servers",
           nlohmann::json::array({nlohmann::json{
               {"name", "invalid-header-name-with-cr"},
               {"transport",
                nlohmann::json{
                    {"type", "http"},
                    {"url", "https://example.invalid"},
                    {"headers", nlohmann::json{{"X-Bad\r\nName", "ok"}}}}},
           }})},
      }),
      Catch::Matchers::ContainsSubstring(
          "header name must be a valid HTTP field-name token"));

  REQUIRE_THROWS_WITH(
      ava::mcp::parse_mcp_config_json(nlohmann::json{
          {"servers",
           nlohmann::json::array({nlohmann::json{
               {"name", "invalid-header-value-with-lf"},
               {"transport",
                nlohmann::json{
                    {"type", "sse"},
                    {"url", "https://example.invalid"},
                    {"headers",
                     nlohmann::json{{"X-Test", "value\r\nsecond-line"}}}}},
           }})},
      }),
      Catch::Matchers::ContainsSubstring(
          "header value must not contain CR or LF"));
}

TEST_CASE("mcp config rejects inline oauth access token and client secret",
          "[ava_mcp]") {
  REQUIRE_THROWS_WITH(
      ava::mcp::parse_mcp_config_json(nlohmann::json{
          {"servers",
           nlohmann::json::array({nlohmann::json{
               {"name", "inline-oauth-access-token"},
               {"transport",
                nlohmann::json{
                    {"type", "http"},
                    {"url", "https://example.invalid"},
                    {"oauth", nlohmann::json{{"accessToken", "secret"}}}}},
           }})},
      }),
      Catch::Matchers::ContainsSubstring("accessToken"));

  REQUIRE_THROWS_WITH(
      ava::mcp::parse_mcp_config_json(nlohmann::json{
          {"servers",
           nlohmann::json::array({nlohmann::json{
               {"name", "inline-oauth-client-secret"},
               {"transport",
                nlohmann::json{
                    {"type", "http"},
                    {"url", "https://example.invalid"},
                    {"oauth", nlohmann::json{{"clientSecret", "secret"}}}}},
           }})},
      }),
      Catch::Matchers::ContainsSubstring("clientSecret"));
}

TEST_CASE("mcp path helpers resolve global and project locations",
          "[ava_mcp]") {
  const auto global = ava::config::mcp_config_path();
  REQUIRE(global.filename() == "mcp.json");
  REQUIRE(ava::config::custom_tools_dir().filename() == "tools");

  const auto project = std::filesystem::path{"/workspace/project"};
  REQUIRE(ava::config::project_mcp_config_path(project) ==
          project / ".ava" / "mcp.json");
  REQUIRE(ava::config::project_custom_tools_dir(project) ==
          project / ".ava" / "tools");
}

TEST_CASE("mcp missing config file loads as empty config", "[ava_mcp]") {
  const auto missing = std::filesystem::temp_directory_path() /
                       "ava_missing_mcp_config_for_test.json";
  std::filesystem::remove(missing);
  const auto parsed = ava::mcp::load_mcp_config_file(missing);
  REQUIRE(parsed.servers.empty());
}

TEST_CASE("mcp malformed config file throws runtime error", "[ava_mcp]") {
  const auto path = std::filesystem::temp_directory_path() /
                    "ava_malformed_mcp_config_for_test.json";
  {
    std::ofstream output(path);
    output << "{ invalid json";
  }
  REQUIRE_THROWS_AS(ava::mcp::load_mcp_config_file(path), std::runtime_error);
  std::filesystem::remove(path);
}
