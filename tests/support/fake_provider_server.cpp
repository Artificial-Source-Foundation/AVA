#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include <array>
#include <cerrno>
#include <chrono>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <optional>
#include <string>
#include <string_view>
#include <thread>
#include <utility>

namespace {

class Fd {
 public:
  explicit Fd(int fd = -1) : fd_(fd) {}
  Fd(const Fd&) = delete;
  Fd& operator=(const Fd&) = delete;
  Fd(Fd&& other) noexcept : fd_(std::exchange(other.fd_, -1)) {}
  Fd& operator=(Fd&& other) noexcept {
    if (this != &other) {
      close();
      fd_ = std::exchange(other.fd_, -1);
    }
    return *this;
  }
  ~Fd() { close(); }

  [[nodiscard]] int get() const noexcept { return fd_; }

 private:
  void close() noexcept {
    if (fd_ >= 0) static_cast<void>(::close(fd_));
    fd_ = -1;
  }

  int fd_ = -1;
};

std::string errno_text() { return std::strerror(errno); }

std::string_view trim_ascii(std::string_view text) {
  while (!text.empty() && (text.front() == ' ' || text.front() == '\t')) text.remove_prefix(1);
  while (!text.empty() && (text.back() == ' ' || text.back() == '\t' || text.back() == '\r')) text.remove_suffix(1);
  return text;
}

bool starts_with_case_insensitive(std::string_view text, std::string_view prefix) {
  if (text.size() < prefix.size()) return false;
  for (std::size_t index = 0; index < prefix.size(); ++index) {
    const auto left = static_cast<unsigned char>(text[index]);
    const auto right = static_cast<unsigned char>(prefix[index]);
    const auto lower_left = static_cast<char>(left >= 'A' && left <= 'Z' ? left - 'A' + 'a' : left);
    const auto lower_right = static_cast<char>(right >= 'A' && right <= 'Z' ? right - 'A' + 'a' : right);
    if (lower_left != lower_right) return false;
  }
  return true;
}

std::optional<std::size_t> content_length(std::string_view headers) {
  std::size_t start = 0;
  while (start < headers.size()) {
    const auto end = headers.find('\n', start);
    const auto line = headers.substr(start, end == std::string_view::npos ? headers.size() - start : end - start);
    if (starts_with_case_insensitive(line, "content-length:")) {
      auto value = trim_ascii(line.substr(std::string_view("content-length:").size()));
      std::size_t parsed = 0;
      for (const char ch : value) {
        if (ch < '0' || ch > '9') return std::nullopt;
        parsed = parsed * 10 + static_cast<std::size_t>(ch - '0');
      }
      return parsed;
    }
    if (end == std::string_view::npos) break;
    start = end + 1;
  }
  return std::nullopt;
}

bool write_all(int fd, std::string_view text) {
  while (!text.empty()) {
    const auto written = ::send(fd, text.data(), text.size(), 0);
    if (written <= 0) return false;
    text.remove_prefix(static_cast<std::size_t>(written));
  }
  return true;
}

std::string json_escape(std::string_view text) {
  std::string escaped;
  escaped.reserve(text.size());
  for (const char ch : text) {
    switch (ch) {
      case '"':
        escaped += "\\\"";
        break;
      case '\\':
        escaped += "\\\\";
        break;
      case '\n':
        escaped += "\\n";
        break;
      case '\r':
        escaped += "\\r";
        break;
      case '\t':
        escaped += "\\t";
        break;
      default:
        if (static_cast<unsigned char>(ch) < 0x20) {
          escaped += '?';
        } else {
          escaped.push_back(ch);
        }
        break;
    }
  }
  return escaped;
}

std::string read_http_request(int fd) {
  std::string request;
  std::array<char, 4096> buffer{};
  constexpr std::size_t kMaxRequestBytes = 1024 * 1024;
  while (request.size() < kMaxRequestBytes) {
    const auto read = ::recv(fd, buffer.data(), buffer.size(), 0);
    if (read <= 0) break;
    request.append(buffer.data(), static_cast<std::size_t>(read));
    const auto header_end = request.find("\r\n\r\n");
    if (header_end == std::string::npos) continue;
    const auto length = content_length(std::string_view(request).substr(0, header_end + 2)).value_or(0);
    if (request.size() >= header_end + 4 + length) break;
  }
  return request;
}

std::string text_body(std::string_view text) {
  return "{\"choices\":[{\"message\":{\"content\":\"" + json_escape(text) +
         "\"},\"finish_reason\":\"stop\"}],"
         "\"usage\":{\"prompt_tokens\":1,\"completion_tokens\":1,\"total_tokens\":2}}";
}

std::string read_tool_body(std::string_view path) {
  const auto arguments = std::string("{\"path\":\"") + json_escape(path) + "\"}";
  return "{\"choices\":[{\"message\":{\"tool_calls\":[{\"id\":\"call_read\",\"type\":\"function\","
         "\"function\":{\"name\":\"read_file\",\"arguments\":\"" +
         json_escape(arguments) + "\"}}]},\"finish_reason\":\"tool_calls\"}]}";
}

std::string write_tool_body(std::string_view path) {
  const auto arguments = std::string("{\"path\":\"") + json_escape(path) + "\",\"content\":\"rpc new\\n\"}";
  return "{\"choices\":[{\"message\":{\"tool_calls\":[{\"id\":\"call_write\",\"type\":\"function\","
         "\"function\":{\"name\":\"write_file\",\"arguments\":\"" +
         json_escape(arguments) + "\"}}]},\"finish_reason\":\"tool_calls\"}]}";
}

std::string question_tool_body() {
  const std::string arguments =
      "{\"header\":\"Pick\",\"question\":\"Continue?\",\"options\":[{\"value\":\"yes\",\"label\":\"Yes\"}],"
      "\"allow_custom\":true}";
  return "{\"choices\":[{\"message\":{\"tool_calls\":[{\"id\":\"call_question\",\"type\":\"function\","
         "\"function\":{\"name\":\"question\",\"arguments\":\"" +
         json_escape(arguments) + "\"}}]},\"finish_reason\":\"tool_calls\"}]}";
}

std::string response_body(std::string_view scenario, int request_index, std::string_view target_path) {
  if (scenario == "read-tool") {
    return request_index == 0 ? read_tool_body(target_path) : text_body("after permission deny");
  }
  if (scenario == "write-tool") {
    return request_index == 0 ? write_tool_body(target_path) : text_body("after permission deny");
  }
  if (scenario == "question-tool") {
    return request_index == 0 ? question_tool_body() : text_body("after question reply");
  }
  if (scenario == "compact") {
    return request_index == 0 ? text_body("before compact")
                              : text_body("# Goal\nHeadless compact summary\n# Next Steps\nContinue.");
  }
  return text_body("headless active prompt complete");
}

}  // namespace

int main(int argc, char** argv) {
  if (argc != 4 && argc != 6) {
    std::cerr << "usage: ava_fake_provider_server PORT_FILE REQUEST_LOG DELAY_MS [SCENARIO TARGET_PATH]\n";
    return 2;
  }

  const std::filesystem::path port_file = argv[1];
  const std::filesystem::path request_log = argv[2];
  const auto delay = std::chrono::milliseconds(std::stoi(argv[3]));
  const std::string scenario = argc == 6 ? argv[4] : "text";
  const std::string target_path = argc == 6 ? argv[5] : "";
  const int request_count =
      scenario == "read-tool" || scenario == "write-tool" || scenario == "question-tool" || scenario == "compact" ? 2
                                                                                                                  : 1;

  Fd server(::socket(AF_INET, SOCK_STREAM, 0));
  if (server.get() < 0) {
    std::cerr << "socket failed: " << errno_text() << '\n';
    return 1;
  }

  int reuse = 1;
  if (::setsockopt(server.get(), SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse)) != 0) {
    std::cerr << "setsockopt failed: " << errno_text() << '\n';
    return 1;
  }

  sockaddr_in address{};
  address.sin_family = AF_INET;
  address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  address.sin_port = 0;
  if (::bind(server.get(), reinterpret_cast<sockaddr*>(&address), sizeof(address)) != 0) {
    std::cerr << "bind failed: " << errno_text() << '\n';
    return 1;
  }
  if (::listen(server.get(), 1) != 0) {
    std::cerr << "listen failed: " << errno_text() << '\n';
    return 1;
  }

  socklen_t address_len = sizeof(address);
  if (::getsockname(server.get(), reinterpret_cast<sockaddr*>(&address), &address_len) != 0) {
    std::cerr << "getsockname failed: " << errno_text() << '\n';
    return 1;
  }
  {
    std::ofstream file(port_file, std::ios::binary | std::ios::trunc);
    file << ntohs(address.sin_port) << '\n';
  }

  { std::ofstream file(request_log, std::ios::binary | std::ios::trunc); }

  for (int request_index = 0; request_index < request_count; ++request_index) {
    Fd client(::accept(server.get(), nullptr, nullptr));
    if (client.get() < 0) {
      std::cerr << "accept failed: " << errno_text() << '\n';
      return 1;
    }

    const auto request = read_http_request(client.get());
    {
      std::ofstream file(request_log, std::ios::binary | std::ios::app);
      file << "--- request " << (request_index + 1) << " ---\n" << request << '\n';
    }
    if (request_index == 0) std::this_thread::sleep_for(delay);

    const std::string body = response_body(scenario, request_index, target_path);
    const std::string response =
        "HTTP/1.1 200 OK\r\nContent-Type: application/json\r\nContent-Length: " + std::to_string(body.size()) +
        "\r\nConnection: close\r\n\r\n" + body;
    if (!write_all(client.get(), response)) {
      std::cerr << "response write failed: " << errno_text() << '\n';
      return 1;
    }
  }
  return 0;
}
