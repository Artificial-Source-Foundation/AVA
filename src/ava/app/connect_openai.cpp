#include "ava/app/connect_openai.h"

#include "ava/tui/composer.h"

#include "ava/config/auth.h"
#include "ava/config/openai_oauth.h"
#include "ava/config/provider_profiles.h"

#include "ava/provider/curl_transport.h"

#include "ava/core/result.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cerrno>
#include <chrono>
#include <csignal>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

#include <netinet/in.h>
#include <poll.h>
#include <signal.h>
#include <sys/socket.h>
#include <termios.h>
#include <unistd.h>

namespace ava::app {
namespace {

constexpr std::size_t max_connect_secret_bytes = 64 * 1024;

sig_atomic_t volatile terminal_mode_signal_number = 0;
bool terminal_mode_signal_handlers_installed = false;
struct sigaction previous_sigint_action {};
struct sigaction previous_sigterm_action {};
struct sigaction previous_sighup_action {};

void terminal_mode_signal_handler(int signal_number)
{
  terminal_mode_signal_number = signal_number;
}

bool install_terminal_mode_signal_handlers()
{
  terminal_mode_signal_number = 0;
  struct sigaction action {};
  action.sa_handler = terminal_mode_signal_handler;
  sigemptyset(&action.sa_mask);
  action.sa_flags = 0;
  if (::sigaction(SIGINT, &action, &previous_sigint_action) != 0) return false;
  if (::sigaction(SIGTERM, &action, &previous_sigterm_action) != 0) {
    static_cast<void>(::sigaction(SIGINT, &previous_sigint_action, nullptr));
    return false;
  }
  if (::sigaction(SIGHUP, &action, &previous_sighup_action) != 0) {
    static_cast<void>(::sigaction(SIGTERM, &previous_sigterm_action, nullptr));
    static_cast<void>(::sigaction(SIGINT, &previous_sigint_action, nullptr));
    return false;
  }
  terminal_mode_signal_handlers_installed = true;
  return true;
}

void restore_terminal_mode_signal_handlers()
{
  if (!terminal_mode_signal_handlers_installed) return;
  terminal_mode_signal_handlers_installed = false;
  static_cast<void>(::sigaction(SIGINT, &previous_sigint_action, nullptr));
  static_cast<void>(::sigaction(SIGTERM, &previous_sigterm_action, nullptr));
  static_cast<void>(::sigaction(SIGHUP, &previous_sighup_action, nullptr));
}

bool terminal_mode_cancelled_by_signal()
{
  return terminal_mode_signal_number != 0;
}

long long unix_time_seconds()
{
  return std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch()).count();
}

class ScopedSocket {
 public:
  explicit ScopedSocket(int fd) : fd_(fd) {}
  ScopedSocket(ScopedSocket const&) = delete;
  ScopedSocket& operator=(ScopedSocket const&) = delete;
  ScopedSocket(ScopedSocket&& other) noexcept : fd_(std::exchange(other.fd_, -1)) {}
  ScopedSocket& operator=(ScopedSocket&& other) noexcept
  {
    if (this != &other) {
      close_if_open();
      fd_ = std::exchange(other.fd_, -1);
    }
    return *this;
  }
  ~ScopedSocket() { close_if_open(); }

  [[nodiscard]] int get() const noexcept { return fd_; }

 private:
  void close_if_open() noexcept
  {
    if (fd_ >= 0) static_cast<void>(::close(fd_));
  }

  int fd_ = -1;
};

class ScopedTerminalEcho {
 public:
  explicit ScopedTerminalEcho(bool enabled) : active_(enabled && ::tcgetattr(STDIN_FILENO, &original_) == 0)
  {
    if (!active_) return;
    auto current = original_;
    current.c_lflag &= ~ECHO;
    if (::tcsetattr(STDIN_FILENO, TCSAFLUSH, &current) != 0) active_ = false;
  }

  ScopedTerminalEcho(ScopedTerminalEcho const&) = delete;
  ScopedTerminalEcho& operator=(ScopedTerminalEcho const&) = delete;

  ~ScopedTerminalEcho()
  {
    if (active_) static_cast<void>(::tcsetattr(STDIN_FILENO, TCSAFLUSH, &original_));
  }

 private:
  bool active_ = false;
  termios original_{};
};

class ScopedTerminalRawMode {
 public:
  explicit ScopedTerminalRawMode(bool enabled) : active_(enabled && ::tcgetattr(STDIN_FILENO, &original_) == 0)
  {
    if (!active_) return;
    bool const signal_handlers_installed = install_terminal_mode_signal_handlers();
    auto current = original_;
    current.c_lflag &= static_cast<tcflag_t>(~(ICANON | ECHO));
    current.c_cc[VMIN] = 1;
    current.c_cc[VTIME] = 0;
    if (::tcsetattr(STDIN_FILENO, TCSAFLUSH, &current) != 0) {
      if (signal_handlers_installed) restore_terminal_mode_signal_handlers();
      active_ = false;
      return;
    }
    signal_handlers_installed_ = signal_handlers_installed;
  }

  ScopedTerminalRawMode(ScopedTerminalRawMode const&) = delete;
  ScopedTerminalRawMode& operator=(ScopedTerminalRawMode const&) = delete;

  ~ScopedTerminalRawMode()
  {
    if (!active_) return;
    static_cast<void>(::tcsetattr(STDIN_FILENO, TCSAFLUSH, &original_));
    if (signal_handlers_installed_) restore_terminal_mode_signal_handlers();
  }

 private:
  bool active_ = false;
  bool signal_handlers_installed_ = false;
  termios original_{};
};

struct ConnectProviderMenuItem {
  std::string id;
  std::string label;
  std::string detail;
};

enum class MenuInputKind { Character, Enter, Escape, Backspace, Up, Down, Other };

enum class OpenAIConnectMethod {
  BrowserOAuth,
  HeadlessOAuth,
  ApiKey,
  OAuthToken,
};

struct MenuInput {
  MenuInputKind kind = MenuInputKind::Other;
  std::string text;
};

std::string errno_message()
{
  return std::strerror(errno);
}

ava::core::Error connect_error(ava::core::ErrorCategory category, std::string message)
{
  return ava::core::Error(category, std::move(message));
}

ava::core::VoidResult send_all_to_socket(int fd, std::string_view text)
{
  std::size_t offset = 0;
  while (offset < text.size()) {
    auto const written = ::send(fd, text.data() + offset, text.size() - offset, MSG_NOSIGNAL);
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

std::string url_decode(std::string_view value)
{
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
      auto const high = hex_value(value[index + 1]);
      auto const low = hex_value(value[index + 2]);
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

std::map<std::string, std::string> parse_query(std::string_view query)
{
  std::map<std::string, std::string> fields;
  std::size_t start = 0;
  while (start <= query.size()) {
    auto const end = query.find('&', start);
    auto const part = query.substr(start, end == std::string_view::npos ? std::string_view::npos : end - start);
    auto const split = part.find('=');
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

ava::core::Result<OAuthCallback> parse_oauth_callback_request(std::string_view request)
{
  auto const request_line_end = request.find("\r\n");
  auto const request_line = request.substr(0, request_line_end);
  constexpr std::string_view prefix = "GET ";
  if (!request_line.starts_with(prefix)) {
    return std::unexpected(connect_error(ava::core::ErrorCategory::InvalidArgument, "OAuth callback was not a GET"));
  }
  auto const target_end = request_line.find(' ', prefix.size());
  if (target_end == std::string_view::npos) {
    return std::unexpected(
        connect_error(ava::core::ErrorCategory::InvalidArgument, "OAuth callback request line was invalid"));
  }
  auto const target = request_line.substr(prefix.size(), target_end - prefix.size());
  auto const query_start = target.find('?');
  auto const path = target.substr(0, query_start);
  if (path != "/auth/callback") {
    auto error = connect_error(ava::core::ErrorCategory::InvalidArgument, "OAuth callback path was invalid");
    error.with_context("path", std::string(path));
    return std::unexpected(std::move(error));
  }
  if (query_start == std::string_view::npos) {
    return std::unexpected(connect_error(ava::core::ErrorCategory::InvalidArgument, "OAuth callback query missing"));
  }
  auto const fields = parse_query(target.substr(query_start + 1));
  OAuthCallback callback;
  if (auto const state = fields.find("state"); state != fields.end()) callback.state = state->second;
  if (auto const error = fields.find("error"); error != fields.end()) callback.error = error->second;
  if (auto const description = fields.find("error_description"); description != fields.end()) {
    callback.error_description = description->second;
  }
  if (!callback.error.empty()) return callback;

  auto const code = fields.find("code");
  if (code == fields.end() || code->second.empty() || callback.state.empty()) {
    return std::unexpected(
        connect_error(ava::core::ErrorCategory::InvalidArgument, "OAuth callback was missing code or state"));
  }
  callback.code = code->second;
  return callback;
}

ava::core::Result<std::string> read_http_request_with_deadline(int fd, std::chrono::steady_clock::time_point deadline)
{
  std::string request;
  std::array<char, 4096> buffer{};
  while (request.find("\r\n\r\n") == std::string::npos && request.size() < 16384) {
    auto const remaining =
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

    auto const count = ::read(fd, buffer.data(), buffer.size());
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

void send_callback_response(int fd, std::string_view body)
{
  std::string const response =
      "HTTP/1.1 200 OK\r\nContent-Type: text/plain; charset=utf-8\r\nContent-Length: " + std::to_string(body.size()) +
      "\r\nConnection: close\r\n\r\n" + std::string(body);
  static_cast<void>(send_all_to_socket(fd, response));
}

ava::core::Result<OAuthCallback> wait_for_oauth_callback(std::string_view expected_state,
                                                         std::function<bool()> const& cancel_requested)
{
  ScopedSocket const server(::socket(AF_INET, SOCK_STREAM | SOCK_CLOEXEC, 0));
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

  auto const deadline = std::chrono::steady_clock::now() + std::chrono::minutes(5);
  while (std::chrono::steady_clock::now() < deadline) {
    if (cancel_requested && cancel_requested()) {
      return std::unexpected(connect_error(ava::core::ErrorCategory::InvalidArgument, "OpenAI OAuth login cancelled"));
    }
    auto const remaining =
        std::chrono::duration_cast<std::chrono::milliseconds>(deadline - std::chrono::steady_clock::now());
    pollfd descriptor{.fd = server.get(), .events = POLLIN, .revents = 0};
    int ready = 0;
    do {
      ready =
          ::poll(&descriptor, 1, static_cast<int>(std::min<long long>(500, std::max<long long>(1, remaining.count()))));
    } while (ready < 0 && errno == EINTR);
    if (ready == 0) continue;
    if (ready < 0) {
      auto error = connect_error(ava::core::ErrorCategory::Io, "failed while waiting for OAuth callback");
      error.with_context("cause", errno_message());
      return std::unexpected(std::move(error));
    }

    ScopedSocket const client(::accept4(server.get(), nullptr, nullptr, SOCK_CLOEXEC));
    if (client.get() < 0) {
      auto error = connect_error(ava::core::ErrorCategory::Io, "failed to accept OAuth callback");
      error.with_context("cause", errno_message());
      return std::unexpected(std::move(error));
    }

    auto const client_deadline = std::min(deadline, std::chrono::steady_clock::now() + std::chrono::seconds(5));
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

std::string credential_type_value(ConnectCredentialType type)
{
  return type == ConnectCredentialType::OAuthToken ? "oauth" : "api_key";
}

std::string credential_type_label(ConnectCredentialType type)
{
  return type == ConnectCredentialType::OAuthToken ? "OAuth bearer token" : "API key";
}

std::string openai_connect_method_label(OpenAIConnectMethod method)
{
  switch (method) {
    case OpenAIConnectMethod::BrowserOAuth:
      return "ChatGPT Pro/Plus (browser OAuth)";
    case OpenAIConnectMethod::HeadlessOAuth:
      return "ChatGPT Pro/Plus (headless OAuth)";
    case OpenAIConnectMethod::ApiKey:
      return "OpenAI API key";
    case OpenAIConnectMethod::OAuthToken:
      return "OAuth bearer token";
  }
  return "OpenAI login";
}

std::string lower_ascii(std::string_view text)
{
  std::string lowered;
  lowered.reserve(text.size());
  for (char const ch : text) lowered.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(ch))));
  return lowered;
}

std::vector<ConnectProviderMenuItem> connect_provider_menu_items()
{
  std::vector<ConnectProviderMenuItem> items;
  for (auto const& profile : ava::config::builtin_provider_profiles()) {
    items.push_back(
        ConnectProviderMenuItem{.id = profile.provider_id,
                                .label = profile.display_name,
                                .detail = profile.connect_detail.empty() ? "API key" : profile.connect_detail});
  }
  return items;
}

bool provider_menu_item_matches(ConnectProviderMenuItem const& item, std::string_view query)
{
  if (query.empty()) return true;
  auto const lowered_query = lower_ascii(query);
  auto const haystack = lower_ascii(item.id + " " + item.label + " " + item.detail);
  return haystack.find(lowered_query) != std::string::npos;
}

std::vector<ConnectProviderMenuItem> filtered_provider_menu_items(std::string_view query)
{
  std::vector<ConnectProviderMenuItem> filtered;
  for (auto item : connect_provider_menu_items()) {
    if (provider_menu_item_matches(item, query)) filtered.push_back(std::move(item));
  }
  return filtered;
}

void render_provider_menu(std::ostream& out, std::string_view query, std::size_t selected_index)
{
  auto const filtered = filtered_provider_menu_items(query);
  out << "\x1b[2J\x1b[H";
  out << "Add credential\n\n";
  out << "Select provider\n\n";
  out << "Search: " << ava::tui::sanitize_terminal_text(std::string(query)) << "\n\n";
  out << "Popular\n";
  if (filtered.empty()) {
    out << "  No matches. Press Enter to use custom provider id.\n";
  } else {
    for (std::size_t index = 0; index < filtered.size(); ++index) {
      auto const& item = filtered[index];
      out << (index == selected_index ? "> " : "  ") << ava::tui::sanitize_terminal_text(item.label) << "  "
          << ava::tui::sanitize_terminal_text(item.detail) << "\n";
    }
  }
  out << "\n↑/↓ to select • Enter: confirm • Type: search • Esc: cancel\n" << std::flush;
}

bool menu_input_pending(std::istream& in, bool stdin_is_tty)
{
  if (in.rdbuf() != nullptr && in.rdbuf()->in_avail() > 0) return true;
  if (!stdin_is_tty) return false;
  pollfd descriptor{.fd = STDIN_FILENO, .events = POLLIN, .revents = 0};
  int ready = 0;
  do {
    ready = ::poll(&descriptor, 1, 25);
  } while (ready < 0 && errno == EINTR);
  return ready > 0 && (descriptor.revents & POLLIN) != 0;
}

void consume_escape_sequence_tail(std::istream& in, bool stdin_is_tty)
{
  while (menu_input_pending(in, stdin_is_tty)) {
    char ignored = 0;
    if (!in.get(ignored)) return;
    if (ignored >= '@' && ignored <= '~') return;
  }
}

ava::core::Result<MenuInput> read_menu_input(std::istream& in, bool stdin_is_tty)
{
  if (terminal_mode_cancelled_by_signal()) return MenuInput{.kind = MenuInputKind::Escape, .text = {}};
  char ch = 0;
  if (!in.get(ch)) {
    if (terminal_mode_cancelled_by_signal()) return MenuInput{.kind = MenuInputKind::Escape, .text = {}};
    return std::unexpected(connect_error(ava::core::ErrorCategory::Io, "failed to read provider menu input"));
  }
  if (ch == '\n' || ch == '\r') return MenuInput{.kind = MenuInputKind::Enter, .text = {}};
  if (ch == '\x03' || ch == '\x04') return MenuInput{.kind = MenuInputKind::Escape, .text = {}};
  if (ch == '\x7f' || ch == '\b') return MenuInput{.kind = MenuInputKind::Backspace, .text = {}};
  if (ch == '\x1b') {
    if (menu_input_pending(in, stdin_is_tty)) {
      char prefix = 0;
      if (!in.get(prefix)) return MenuInput{.kind = MenuInputKind::Escape, .text = {}};
      if (prefix != '[') {
        static_cast<void>(in.unget());
        return MenuInput{.kind = MenuInputKind::Escape, .text = {}};
      }
      if (!menu_input_pending(in, stdin_is_tty)) {
        return MenuInput{.kind = MenuInputKind::Escape, .text = {}};
      }
      char code = 0;
      if (in.get(code)) {
        if (code == 'A') return MenuInput{.kind = MenuInputKind::Up, .text = {}};
        if (code == 'B') return MenuInput{.kind = MenuInputKind::Down, .text = {}};
        if (code >= '@' && code <= '~') return MenuInput{.kind = MenuInputKind::Other, .text = {}};
        consume_escape_sequence_tail(in, stdin_is_tty);
        return MenuInput{.kind = MenuInputKind::Other, .text = {}};
      }
    }
    return MenuInput{.kind = MenuInputKind::Escape, .text = {}};
  }
  if (std::isprint(static_cast<unsigned char>(ch)) != 0) {
    return MenuInput{.kind = MenuInputKind::Character, .text = std::string(1, ch)};
  }
  return MenuInput{.kind = MenuInputKind::Other, .text = {}};
}

ava::core::Result<std::string> select_provider_from_menu(std::istream& in, std::ostream& out, bool stdin_is_tty)
{
  std::string query;
  std::size_t selected_index = 0;
  ScopedTerminalRawMode const raw_mode(stdin_is_tty);
  while (true) {
    auto filtered = filtered_provider_menu_items(query);
    if (!filtered.empty()) selected_index = std::min(selected_index, filtered.size() - 1);
    render_provider_menu(out, query, selected_index);
    auto input = read_menu_input(in, stdin_is_tty);
    if (!input) return std::unexpected(std::move(input.error()));
    switch (input->kind) {
      case MenuInputKind::Character:
        query += input->text;
        selected_index = 0;
        break;
      case MenuInputKind::Backspace:
        if (!query.empty()) query.pop_back();
        selected_index = 0;
        break;
      case MenuInputKind::Up:
        filtered = filtered_provider_menu_items(query);
        if (!filtered.empty()) selected_index = selected_index == 0 ? filtered.size() - 1 : selected_index - 1;
        break;
      case MenuInputKind::Down:
        filtered = filtered_provider_menu_items(query);
        if (!filtered.empty()) selected_index = (selected_index + 1) % filtered.size();
        break;
      case MenuInputKind::Enter:
        filtered = filtered_provider_menu_items(query);
        if (!filtered.empty()) return filtered[std::min(selected_index, filtered.size() - 1)].id;
        if (!query.empty()) return query;
        break;
      case MenuInputKind::Escape:
        return std::unexpected(connect_error(ava::core::ErrorCategory::InvalidArgument, "provider login cancelled"));
      case MenuInputKind::Other:
        break;
    }
  }
}

std::optional<ConnectCredentialType> parse_connect_credential_type(std::string_view value)
{
  if (value == "api" || value == "api-key" || value == "apikey" || value == "key" || value == "api_key") {
    return ConnectCredentialType::ApiKey;
  }
  if (value == "oauth" || value == "oauth-token" || value == "oauth_token" || value == "bearer" || value == "token") {
    return ConnectCredentialType::OAuthToken;
  }
  return std::nullopt;
}

std::optional<OpenAIConnectMethod> parse_openai_connect_method(std::string_view value)
{
  auto const lowered = lower_ascii(value);
  if (lowered == "1" || lowered == "browser" || lowered == "browser-oauth" || lowered == "browser_oauth" ||
      lowered == "chatgpt" || lowered == "oauth") {
    return OpenAIConnectMethod::BrowserOAuth;
  }
  if (lowered == "2" || lowered == "headless" || lowered == "headless-oauth" || lowered == "headless_oauth" ||
      lowered == "device" || lowered == "device-oauth" || lowered == "device_oauth") {
    return OpenAIConnectMethod::HeadlessOAuth;
  }
  if (lowered == "3" || lowered == "api" || lowered == "api-key" || lowered == "apikey" || lowered == "key" ||
      lowered == "api_key") {
    return OpenAIConnectMethod::ApiKey;
  }
  if (lowered == "4" || lowered == "oauth-token" || lowered == "oauth_token" || lowered == "bearer" ||
      lowered == "token") {
    return OpenAIConnectMethod::OAuthToken;
  }
  return std::nullopt;
}

std::string trim_stdin_secret(std::string secret)
{
  auto is_edge_space = [](char ch) { return ch == ' ' || ch == '\t' || ch == '\n' || ch == '\r'; };
  auto first = std::find_if_not(secret.begin(), secret.end(), is_edge_space);
  auto last = std::find_if_not(secret.rbegin(), secret.rend(), is_edge_space).base();
  if (first >= last) return {};
  return std::string(first, last);
}

bool is_valid_env_var_name(std::string_view name)
{
  if (name.empty()) return false;
  auto const first = static_cast<unsigned char>(name.front());
  if (std::isalpha(first) == 0 && name.front() != '_') return false;
  for (char const ch : name.substr(1)) {
    auto const uch = static_cast<unsigned char>(ch);
    if (std::isalnum(uch) != 0 || ch == '_') continue;
    return false;
  }
  return true;
}

bool is_valid_connect_provider_id(std::string_view provider_id)
{
  if (provider_id.empty() || provider_id.size() > 128) return false;
  return std::ranges::all_of(provider_id, [](char ch) {
    auto const uch = static_cast<unsigned char>(ch);
    return std::isalnum(uch) != 0 || ch == '-' || ch == '_';
  });
}

ava::core::Result<std::string> read_prompt_line(std::istream& in, std::ostream& out, std::string_view prompt,
                                                bool secret, bool stdin_is_tty)
{
  out << prompt << std::flush;
  std::string line;
  {
    ScopedTerminalEcho const echo_guard(secret && stdin_is_tty);
    if (!std::getline(in, line)) {
      return std::unexpected(connect_error(ava::core::ErrorCategory::Io, "failed to read connect prompt input"));
    }
  }
  if (secret && stdin_is_tty) out << '\n';
  line = trim_stdin_secret(std::move(line));
  if (line.size() > max_connect_secret_bytes) {
    auto error = connect_error(ava::core::ErrorCategory::InvalidArgument, "connect prompt input is too large");
    error.with_context("max_bytes", std::to_string(max_connect_secret_bytes));
    return std::unexpected(std::move(error));
  }
  return line;
}

ava::core::VoidResult store_connect_secret(ava::config::XdgPaths const& paths, std::string_view provider_id,
                                           ConnectCredentialType credential_type, std::string secret)
{
  if (!is_valid_connect_provider_id(provider_id)) {
    return std::unexpected(connect_error(ava::core::ErrorCategory::InvalidArgument,
                                         "provider id must contain only letters, numbers, '-' or '_'"));
  }
  if (secret.empty()) {
    return std::unexpected(connect_error(ava::core::ErrorCategory::InvalidArgument, "credential was empty"));
  }
  return ava::config::store_provider_credential(
      paths, ava::config::ProviderCredential{.provider_id = std::string(provider_id),
                                             .access_token = std::move(secret),
                                             .credential_type = credential_type_value(credential_type),
                                             .account_id = "",
                                             .source = "connect"});
}

ava::core::Result<std::string> read_connect_secret(ConnectProviderCredentialOptions const& options, std::istream& in)
{
  if (options.env_var) {
    if (!is_valid_env_var_name(*options.env_var)) {
      return std::unexpected(
          connect_error(ava::core::ErrorCategory::InvalidArgument, "credential env var name is invalid"));
    }
    char const* value = std::getenv(options.env_var->c_str());
    if (value == nullptr || std::string_view(value).empty()) {
      return std::unexpected(
          connect_error(ava::core::ErrorCategory::PermissionDenied, "credential env var is not set"));
    }
    if (std::string_view(value).size() > max_connect_secret_bytes) {
      auto error = connect_error(ava::core::ErrorCategory::InvalidArgument, "credential env var value is too large");
      error.with_context("max_bytes", std::to_string(max_connect_secret_bytes));
      return std::unexpected(std::move(error));
    }
    return std::string(value);
  }

  std::string secret;
  char ch = 0;
  while (in.get(ch)) {
    if (secret.size() >= max_connect_secret_bytes) {
      auto error = connect_error(ava::core::ErrorCategory::InvalidArgument, "credential stdin is too large");
      error.with_context("max_bytes", std::to_string(max_connect_secret_bytes));
      return std::unexpected(std::move(error));
    }
    secret.push_back(ch);
  }
  if (in.bad()) {
    return std::unexpected(connect_error(ava::core::ErrorCategory::Io, "failed to read credential from stdin"));
  }
  secret = trim_stdin_secret(std::move(secret));
  if (secret.empty()) {
    return std::unexpected(connect_error(ava::core::ErrorCategory::InvalidArgument, "credential stdin was empty"));
  }
  return secret;
}

}  // namespace

ava::core::Result<ava::config::OpenAICredential> complete_openai_browser_oauth(
    ava::config::OpenAIOAuthSession const& session, ava::provider::Transport& transport, long long now_seconds,
    std::function<bool()> cancel_requested)
{
  auto callback = wait_for_oauth_callback(session.state, cancel_requested);
  if (!callback) return std::unexpected(std::move(callback.error()));
  return ava::config::exchange_openai_oauth_code(callback->code, session.code_verifier, transport, now_seconds);
}

ava::core::Result<ava::config::OpenAICredential> wait_for_openai_device_oauth(
    ava::config::OpenAIOAuthDeviceAuthorization const& authorization, ava::provider::Transport& transport,
    long long now_seconds, std::function<bool()> cancel_requested)
{
  auto const deadline = std::chrono::steady_clock::now() + std::chrono::minutes(10);
  auto const interval = std::chrono::seconds(std::max(1, authorization.interval_seconds) + 3);
  while (std::chrono::steady_clock::now() < deadline) {
    if (cancel_requested && cancel_requested()) {
      return std::unexpected(connect_error(ava::core::ErrorCategory::InvalidArgument, "OpenAI OAuth login cancelled"));
    }
    auto credential = ava::config::poll_openai_oauth_device_authorization(authorization, transport, now_seconds);
    if (!credential) return std::unexpected(std::move(credential.error()));
    if (*credential) return std::move(**credential);
    auto const sleep_until = std::min(deadline, std::chrono::steady_clock::now() + interval);
    while (std::chrono::steady_clock::now() < sleep_until) {
      if (cancel_requested && cancel_requested()) {
        return std::unexpected(
            connect_error(ava::core::ErrorCategory::InvalidArgument, "OpenAI OAuth login cancelled"));
      }
      std::this_thread::sleep_for(std::chrono::milliseconds(250));
    }
  }
  return std::unexpected(connect_error(ava::core::ErrorCategory::Io, "timed out waiting for OpenAI device OAuth"));
}

int run_connect_openai_browser(ava::config::XdgPaths const& paths, std::ostream& out, std::ostream& err)
{
  auto session = ava::config::make_openai_oauth_session();
  if (!session) {
    err << ava::tui::sanitize_terminal_text(session.error().format()) << '\n';
    return 1;
  }
  out << "Open this URL to connect AVA to OpenAI:\n\n" << session->authorization_url << "\n\n";
  out << "Waiting for browser callback on http://localhost:1455/auth/callback ...\n";

  ava::provider::CurlCliTransport transport;
  auto credential = complete_openai_browser_oauth(*session, transport, unix_time_seconds());
  if (!credential) {
    err << ava::tui::sanitize_terminal_text(credential.error().format()) << '\n';
    return 1;
  }
  auto stored = ava::config::store_openai_credential(paths, *credential);
  if (!stored) {
    err << ava::tui::sanitize_terminal_text(stored.error().format()) << '\n';
    return 1;
  }
  out << "OpenAI OAuth credential stored at " << ava::tui::sanitize_terminal_text(paths.auth_file.string()) << '\n';
  return 0;
}

int run_connect_openai_headless(ava::config::XdgPaths const& paths, std::ostream& out, std::ostream& err)
{
  ava::provider::CurlCliTransport transport;
  auto authorization = ava::config::start_openai_oauth_device_authorization(transport);
  if (!authorization) {
    err << ava::tui::sanitize_terminal_text(authorization.error().format()) << '\n';
    return 1;
  }
  out << "Open this URL on any device:\n\n" << authorization->verification_url << "\n\n";
  out << "Enter this code: " << ava::tui::sanitize_terminal_text(authorization->user_code) << "\n\n";
  out << "Waiting for OpenAI authorization ...\n";

  auto credential = wait_for_openai_device_oauth(*authorization, transport, unix_time_seconds());
  if (!credential) {
    err << ava::tui::sanitize_terminal_text(credential.error().format()) << '\n';
    return 1;
  }
  auto stored = ava::config::store_openai_credential(paths, *credential);
  if (!stored) {
    err << ava::tui::sanitize_terminal_text(stored.error().format()) << '\n';
    return 1;
  }
  out << "OpenAI OAuth credential stored at " << ava::tui::sanitize_terminal_text(paths.auth_file.string()) << '\n';
  return 0;
}

int run_connect_openai(ava::config::XdgPaths const& paths)
{
  return run_connect_openai_browser(paths, std::cout, std::cerr);
}

int run_connect_openai_wizard(ava::config::XdgPaths const& paths, ConnectProviderWizardOptions const& options,
                              std::istream& in, std::ostream& out, std::ostream& err)
{
  if (options.credential_type) {
    return run_connect_provider_wizard(paths,
                                       ConnectProviderWizardOptions{.provider_id = std::string("openai"),
                                                                    .credential_type = options.credential_type,
                                                                    .stdin_is_tty = options.stdin_is_tty},
                                       in, out, err);
  }

  if (!options.stdin_is_tty) {
    err << "interactive OpenAI login requires a terminal; use --browser-oauth, --headless-oauth, --api-key-stdin, "
           "--api-key-env, --oauth-token-stdin, or --oauth-token-env\n";
    return 2;
  }

  out << "OpenAI login method:\n";
  out << "  1. " << openai_connect_method_label(OpenAIConnectMethod::BrowserOAuth) << '\n';
  out << "  2. " << openai_connect_method_label(OpenAIConnectMethod::HeadlessOAuth) << '\n';
  out << "  3. " << openai_connect_method_label(OpenAIConnectMethod::ApiKey) << '\n';
  out << "  4. " << openai_connect_method_label(OpenAIConnectMethod::OAuthToken) << '\n';
  auto method_text = read_prompt_line(in, out, "Select method [1-4]: ", false, options.stdin_is_tty);
  if (!method_text) {
    err << ava::tui::sanitize_terminal_text(method_text.error().format()) << '\n';
    return 1;
  }
  auto method = parse_openai_connect_method(*method_text);
  if (!method) {
    err << "OpenAI login method must be 1, 2, 3, or 4\n";
    return 2;
  }
  switch (*method) {
    case OpenAIConnectMethod::BrowserOAuth:
      return run_connect_openai_browser(paths, out, err);
    case OpenAIConnectMethod::HeadlessOAuth:
      return run_connect_openai_headless(paths, out, err);
    case OpenAIConnectMethod::ApiKey:
      return run_connect_provider_wizard(paths,
                                         ConnectProviderWizardOptions{.provider_id = std::string("openai"),
                                                                      .credential_type = ConnectCredentialType::ApiKey,
                                                                      .stdin_is_tty = options.stdin_is_tty},
                                         in, out, err);
    case OpenAIConnectMethod::OAuthToken:
      return run_connect_provider_wizard(
          paths,
          ConnectProviderWizardOptions{.provider_id = std::string("openai"),
                                       .credential_type = ConnectCredentialType::OAuthToken,
                                       .stdin_is_tty = options.stdin_is_tty},
          in, out, err);
  }
  return 2;
}

int run_connect_provider_wizard(ava::config::XdgPaths const& paths, ConnectProviderWizardOptions const& options,
                                std::istream& in, std::ostream& out, std::ostream& err)
{
  if (!options.stdin_is_tty) {
    err << "interactive provider login requires a terminal; use --api-key-stdin, --api-key-env, --oauth-token-stdin, "
           "or --oauth-token-env\n";
    return 2;
  }

  std::string provider_id;
  if (options.provider_id) {
    provider_id = *options.provider_id;
  } else {
    auto provider = select_provider_from_menu(in, out, options.stdin_is_tty);
    if (!provider) {
      err << ava::tui::sanitize_terminal_text(provider.error().format()) << '\n';
      return 1;
    }
    provider_id = *provider;
  }

  if (!is_valid_connect_provider_id(provider_id)) {
    err << "provider id must contain only letters, numbers, '-' or '_'\n";
    return 2;
  }
  if (provider_id == "openai" && !options.credential_type) {
    return run_connect_openai_wizard(
        paths, ConnectProviderWizardOptions{.provider_id = provider_id, .stdin_is_tty = options.stdin_is_tty}, in, out,
        err);
  }

  ConnectCredentialType credential_type = ConnectCredentialType::ApiKey;
  if (options.credential_type) {
    credential_type = *options.credential_type;
  } else {
    auto method = read_prompt_line(in, out, "Credential type [api-key/oauth-token]: ", false, options.stdin_is_tty);
    if (!method) {
      err << ava::tui::sanitize_terminal_text(method.error().format()) << '\n';
      return 1;
    }
    auto parsed = parse_connect_credential_type(*method);
    if (!parsed) {
      err << "credential type must be api-key or oauth-token\n";
      return 2;
    }
    credential_type = *parsed;
  }

  auto secret = read_prompt_line(in, out, credential_type_label(credential_type) + " for " + provider_id + ": ", true,
                                 options.stdin_is_tty);
  if (!secret) {
    err << ava::tui::sanitize_terminal_text(secret.error().format()) << '\n';
    return 1;
  }
  auto stored = store_connect_secret(paths, provider_id, credential_type, *secret);
  if (!stored) {
    err << ava::tui::sanitize_terminal_text(stored.error().format()) << '\n';
    return 1;
  }
  out << "Stored " << ava::tui::sanitize_terminal_text(provider_id) << ' ' << credential_type_label(credential_type)
      << " credential at " << ava::tui::sanitize_terminal_text(paths.auth_file.string()) << '\n';
  if (provider_id == "openai") {
    out << "Tip: `ava connect openai` opens the OpenAI login method picker.\n";
  }
  return 0;
}

int run_connect_provider_credential(ava::config::XdgPaths const& paths, ConnectProviderCredentialOptions const& options,
                                    std::istream& in, std::ostream& out, std::ostream& err)
{
  auto secret = read_connect_secret(options, in);
  if (!secret) {
    err << ava::tui::sanitize_terminal_text(secret.error().format()) << '\n';
    return 1;
  }

  auto stored = store_connect_secret(paths, options.provider_id, options.credential_type, *secret);
  if (!stored) {
    err << ava::tui::sanitize_terminal_text(stored.error().format()) << '\n';
    return 1;
  }
  out << "Stored " << credential_type_label(options.credential_type) << " credential at "
      << ava::tui::sanitize_terminal_text(paths.auth_file.string()) << '\n';
  return 0;
}

}  // namespace ava::app
