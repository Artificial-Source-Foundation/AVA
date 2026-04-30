#include "ava/app/connect_openai.h"

#include <netinet/in.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>

#include <algorithm>
#include <array>
#include <cerrno>
#include <chrono>
#include <cstring>
#include <iostream>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

#include "ava/config/auth.h"
#include "ava/config/openai_oauth.h"
#include "ava/core/result.h"
#include "ava/provider/curl_transport.h"
#include "ava/tui/composer.h"

namespace ava::app {
namespace {

long long unix_time_seconds() {
  return std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch()).count();
}

class ScopedSocket {
 public:
  explicit ScopedSocket(int fd) : fd_(fd) {}
  ScopedSocket(const ScopedSocket&) = delete;
  ScopedSocket& operator=(const ScopedSocket&) = delete;
  ScopedSocket(ScopedSocket&& other) noexcept : fd_(std::exchange(other.fd_, -1)) {}
  ScopedSocket& operator=(ScopedSocket&& other) noexcept {
    if (this != &other) {
      close_if_open();
      fd_ = std::exchange(other.fd_, -1);
    }
    return *this;
  }
  ~ScopedSocket() { close_if_open(); }

  [[nodiscard]] int get() const noexcept { return fd_; }

 private:
  void close_if_open() noexcept {
    if (fd_ >= 0) static_cast<void>(::close(fd_));
  }

  int fd_ = -1;
};

std::string errno_message() { return std::strerror(errno); }

ava::core::Error connect_error(ava::core::ErrorCategory category, std::string message) {
  return ava::core::Error(category, std::move(message));
}

ava::core::VoidResult send_all_to_socket(int fd, std::string_view text) {
  std::size_t offset = 0;
  while (offset < text.size()) {
    const auto written = ::send(fd, text.data() + offset, text.size() - offset, MSG_NOSIGNAL);
    if (written < 0) {
      if (errno == EINTR) continue;
      auto error = connect_error(ava::core::ErrorCategory::Io, "failed to write OAuth callback response");
      error.with_context("cause", errno_message());
      return std::unexpected(std::move(error));
    }
    if (written == 0) {
      return std::unexpected(
          connect_error(ava::core::ErrorCategory::Io, "OAuth callback response write made no progress"));
    }
    offset += static_cast<std::size_t>(written);
  }
  return {};
}

std::string url_decode(std::string_view value) {
  auto hex_value = [](char ch) -> std::optional<unsigned char> {
    if (ch >= '0' && ch <= '9') return static_cast<unsigned char>(ch - '0');
    if (ch >= 'A' && ch <= 'F') return static_cast<unsigned char>(ch - 'A' + 10);
    if (ch >= 'a' && ch <= 'f') return static_cast<unsigned char>(ch - 'a' + 10);
    return std::nullopt;
  };

  std::string output;
  output.reserve(value.size());
  for (std::size_t index = 0; index < value.size(); ++index) {
    if (value[index] == '+') {
      output.push_back(' ');
      continue;
    }
    if (value[index] == '%' && index + 2 < value.size()) {
      const auto high = hex_value(value[index + 1]);
      const auto low = hex_value(value[index + 2]);
      if (high && low) {
        output.push_back(static_cast<char>((*high << 4U) | *low));
        index += 2;
        continue;
      }
    }
    output.push_back(value[index]);
  }
  return output;
}

std::map<std::string, std::string> parse_query(std::string_view query) {
  std::map<std::string, std::string> fields;
  std::size_t start = 0;
  while (start <= query.size()) {
    const auto end = query.find('&', start);
    const auto part = query.substr(start, end == std::string_view::npos ? std::string_view::npos : end - start);
    const auto split = part.find('=');
    if (split != std::string_view::npos) {
      fields[url_decode(part.substr(0, split))] = url_decode(part.substr(split + 1));
    }
    if (end == std::string_view::npos) break;
    start = end + 1;
  }
  return fields;
}

struct OAuthCallback {
  std::string code;
  std::string state;
  std::string error;
  std::string error_description;
};

ava::core::Result<OAuthCallback> parse_oauth_callback_request(std::string_view request) {
  const auto request_line_end = request.find("\r\n");
  const auto request_line = request.substr(0, request_line_end);
  constexpr std::string_view prefix = "GET ";
  if (!request_line.starts_with(prefix)) {
    return std::unexpected(connect_error(ava::core::ErrorCategory::InvalidArgument, "OAuth callback was not a GET"));
  }
  const auto target_end = request_line.find(' ', prefix.size());
  if (target_end == std::string_view::npos) {
    return std::unexpected(
        connect_error(ava::core::ErrorCategory::InvalidArgument, "OAuth callback request line was invalid"));
  }
  const auto target = request_line.substr(prefix.size(), target_end - prefix.size());
  const auto query_start = target.find('?');
  const auto path = target.substr(0, query_start);
  if (path != "/auth/callback") {
    auto error = connect_error(ava::core::ErrorCategory::InvalidArgument, "OAuth callback path was invalid");
    error.with_context("path", std::string(path));
    return std::unexpected(std::move(error));
  }
  if (query_start == std::string_view::npos) {
    return std::unexpected(connect_error(ava::core::ErrorCategory::InvalidArgument, "OAuth callback query missing"));
  }
  const auto fields = parse_query(target.substr(query_start + 1));
  OAuthCallback callback;
  if (const auto state = fields.find("state"); state != fields.end()) callback.state = state->second;
  if (const auto error = fields.find("error"); error != fields.end()) callback.error = error->second;
  if (const auto description = fields.find("error_description"); description != fields.end()) {
    callback.error_description = description->second;
  }
  if (!callback.error.empty()) return callback;

  const auto code = fields.find("code");
  if (code == fields.end() || code->second.empty() || callback.state.empty()) {
    return std::unexpected(
        connect_error(ava::core::ErrorCategory::InvalidArgument, "OAuth callback was missing code or state"));
  }
  callback.code = code->second;
  return callback;
}

ava::core::Result<std::string> read_http_request_with_deadline(int fd, std::chrono::steady_clock::time_point deadline) {
  std::string request;
  std::array<char, 4096> buffer{};
  while (request.find("\r\n\r\n") == std::string::npos && request.size() < 16384) {
    const auto remaining =
        std::chrono::duration_cast<std::chrono::milliseconds>(deadline - std::chrono::steady_clock::now());
    if (remaining.count() <= 0) {
      return std::unexpected(connect_error(ava::core::ErrorCategory::Io, "timed out reading OAuth callback"));
    }
    pollfd descriptor{.fd = fd, .events = POLLIN, .revents = 0};
    int ready = 0;
    do {
      ready = ::poll(&descriptor, 1, static_cast<int>(std::min<long long>(5000, remaining.count())));
    } while (ready < 0 && errno == EINTR);
    if (ready == 0) {
      return std::unexpected(connect_error(ava::core::ErrorCategory::Io, "timed out reading OAuth callback"));
    }
    if (ready < 0) {
      auto error = connect_error(ava::core::ErrorCategory::Io, "failed while reading OAuth callback");
      error.with_context("cause", errno_message());
      return std::unexpected(std::move(error));
    }

    const auto count = ::read(fd, buffer.data(), buffer.size());
    if (count < 0) {
      if (errno == EINTR) continue;
      auto error = connect_error(ava::core::ErrorCategory::Io, "failed to read OAuth callback");
      error.with_context("cause", errno_message());
      return std::unexpected(std::move(error));
    }
    if (count == 0) break;
    request.append(buffer.data(), static_cast<std::size_t>(count));
  }
  return request;
}

void send_callback_response(int fd, std::string_view body) {
  const std::string response =
      "HTTP/1.1 200 OK\r\nContent-Type: text/plain; charset=utf-8\r\nContent-Length: " + std::to_string(body.size()) +
      "\r\nConnection: close\r\n\r\n" + std::string(body);
  static_cast<void>(send_all_to_socket(fd, response));
}

ava::core::Result<OAuthCallback> wait_for_oauth_callback(std::string_view expected_state) {
  const ScopedSocket server(::socket(AF_INET, SOCK_STREAM | SOCK_CLOEXEC, 0));
  if (server.get() < 0) {
    auto error = connect_error(ava::core::ErrorCategory::Io, "failed to create OAuth callback socket");
    error.with_context("cause", errno_message());
    return std::unexpected(std::move(error));
  }
  int reuse = 1;
  static_cast<void>(::setsockopt(server.get(), SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse)));

  sockaddr_in address{};
  address.sin_family = AF_INET;
  address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  address.sin_port = htons(1455);
  if (::bind(server.get(), reinterpret_cast<sockaddr*>(&address), sizeof(address)) != 0) {
    auto error = connect_error(ava::core::ErrorCategory::Io, "failed to bind OAuth callback server");
    error.with_context("address", "127.0.0.1:1455");
    error.with_context("cause", errno_message());
    return std::unexpected(std::move(error));
  }
  if (::listen(server.get(), 1) != 0) {
    auto error = connect_error(ava::core::ErrorCategory::Io, "failed to listen for OAuth callback");
    error.with_context("cause", errno_message());
    return std::unexpected(std::move(error));
  }

  const auto deadline = std::chrono::steady_clock::now() + std::chrono::minutes(5);
  while (std::chrono::steady_clock::now() < deadline) {
    const auto remaining =
        std::chrono::duration_cast<std::chrono::milliseconds>(deadline - std::chrono::steady_clock::now());
    pollfd descriptor{.fd = server.get(), .events = POLLIN, .revents = 0};
    int ready = 0;
    do {
      ready = ::poll(&descriptor, 1, static_cast<int>(std::max<long long>(1, remaining.count())));
    } while (ready < 0 && errno == EINTR);
    if (ready == 0) break;
    if (ready < 0) {
      auto error = connect_error(ava::core::ErrorCategory::Io, "failed while waiting for OAuth callback");
      error.with_context("cause", errno_message());
      return std::unexpected(std::move(error));
    }

    const ScopedSocket client(::accept4(server.get(), nullptr, nullptr, SOCK_CLOEXEC));
    if (client.get() < 0) {
      auto error = connect_error(ava::core::ErrorCategory::Io, "failed to accept OAuth callback");
      error.with_context("cause", errno_message());
      return std::unexpected(std::move(error));
    }

    const auto client_deadline = std::min(deadline, std::chrono::steady_clock::now() + std::chrono::seconds(5));
    auto request = read_http_request_with_deadline(client.get(), client_deadline);
    if (!request) {
      send_callback_response(client.get(), "AVA could not read this OAuth callback. Return to the terminal.\n");
      continue;
    }
    auto callback = parse_oauth_callback_request(*request);
    if (!callback) {
      send_callback_response(client.get(), "AVA ignored this OAuth callback. Return to the terminal.\n");
      continue;
    }
    if (callback->state != expected_state) {
      send_callback_response(client.get(), "AVA ignored an OAuth callback with the wrong state.\n");
      continue;
    }
    if (!callback->error.empty()) {
      send_callback_response(client.get(), "AVA could not complete OAuth. Return to the terminal.\n");
      auto error = connect_error(ava::core::ErrorCategory::Provider, "OpenAI OAuth callback returned an error");
      error.with_context("error", callback->error);
      if (!callback->error_description.empty()) error.with_context("description", callback->error_description);
      return std::unexpected(std::move(error));
    }
    send_callback_response(client.get(), "AVA connected. You can close this tab.\n");
    return callback;
  }
  return std::unexpected(connect_error(ava::core::ErrorCategory::Io, "timed out waiting for OAuth callback"));
}

}  // namespace

int run_connect_openai(const ava::config::XdgPaths& paths) {
  auto session = ava::config::make_openai_oauth_session();
  if (!session) {
    std::cerr << ava::tui::sanitize_terminal_text(session.error().format()) << '\n';
    return 1;
  }
  std::cout << "Open this URL to connect AVA to OpenAI:\n\n" << session->authorization_url << "\n\n";
  std::cout << "Waiting for browser callback on http://localhost:1455/auth/callback ...\n";

  auto callback = wait_for_oauth_callback(session->state);
  if (!callback) {
    std::cerr << ava::tui::sanitize_terminal_text(callback.error().format()) << '\n';
    return 1;
  }

  ava::provider::CurlCliTransport transport;
  auto credential =
      ava::config::exchange_openai_oauth_code(callback->code, session->code_verifier, transport, unix_time_seconds());
  if (!credential) {
    std::cerr << ava::tui::sanitize_terminal_text(credential.error().format()) << '\n';
    return 1;
  }
  auto stored = ava::config::store_openai_credential(paths, *credential);
  if (!stored) {
    std::cerr << ava::tui::sanitize_terminal_text(stored.error().format()) << '\n';
    return 1;
  }
  std::cout << "OpenAI OAuth credential stored at " << paths.auth_file.string() << '\n';
  return 0;
}

}  // namespace ava::app
