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

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

namespace {

class Fd {
 public:
  explicit Fd(int fd = -1) : fd_(fd) {}
  Fd(Fd const&) = delete;
  Fd& operator=(Fd const&) = delete;
  Fd(Fd&& other) noexcept : fd_(std::exchange(other.fd_, -1)) {}
  Fd& operator=(Fd&& other) noexcept
  {
    if (this != &other) {
      close();
      fd_ = std::exchange(other.fd_, -1);
    }
    return *this;
  }
  ~Fd() { close(); }

  [[nodiscard]] int get() const noexcept { return fd_; }

 private:
  void close() noexcept
  {
    if (fd_ >= 0) static_cast<void>(::close(fd_));
    fd_ = -1;
  }

  int fd_ = -1;
};

std::string errno_text()
{
  return std::strerror(errno);
}

std::string_view trim_ascii(std::string_view text)
{
  while (!text.empty() && (text.front() == ' ' || text.front() == '\t')) text.remove_prefix(1);
  while (!text.empty() && (text.back() == ' ' || text.back() == '\t' || text.back() == '\r')) text.remove_suffix(1);
  return text;
}

bool starts_with_case_insensitive(std::string_view text, std::string_view prefix)
{
  if (text.size() < prefix.size()) return false;
  for (std::size_t index = 0; index < prefix.size(); ++index) {
    auto const left = static_cast<unsigned char>(text[index]);
    auto const right = static_cast<unsigned char>(prefix[index]);
    auto const lower_left = static_cast<char>(left >= 'A' && left <= 'Z' ? left - 'A' + 'a' : left);
    auto const lower_right = static_cast<char>(right >= 'A' && right <= 'Z' ? right - 'A' + 'a' : right);
    if (lower_left != lower_right) return false;
  }
  return true;
}

std::optional<std::size_t> content_length(std::string_view headers)
{
  std::size_t start = 0;
  while (start < headers.size()) {
    auto const end = headers.find('\n', start);
    auto const line = headers.substr(start, end == std::string_view::npos ? headers.size() - start : end - start);
    if (starts_with_case_insensitive(line, "content-length:")) {
      auto value = trim_ascii(line.substr(std::string_view("content-length:").size()));
      std::size_t parsed = 0;
      for (char const ch : value) {
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

bool write_all(int fd, std::string_view text)
{
  while (!text.empty()) {
    auto const written = ::send(fd, text.data(), text.size(), 0);
    if (written <= 0) return false;
    text.remove_prefix(static_cast<std::size_t>(written));
  }
  return true;
}

std::string json_escape(std::string_view text)
{
  std::string escaped;
  escaped.reserve(text.size());
  for (char const ch : text) {
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

std::string read_http_request(int fd)
{
  std::string request;
  std::array<char, 4096> buffer{};
  constexpr std::size_t kMaxRequestBytes = 1024 * 1024;
  while (request.size() < kMaxRequestBytes) {
    auto const read = ::recv(fd, buffer.data(), buffer.size(), 0);
    if (read <= 0) break;
    request.append(buffer.data(), static_cast<std::size_t>(read));
    auto const header_end = request.find("\r\n\r\n");
    if (header_end == std::string::npos) continue;
    auto const length = content_length(std::string_view(request).substr(0, header_end + 2)).value_or(0);
    if (request.size() >= header_end + 4 + length) break;
  }
  return request;
}

std::string text_body(std::string_view text)
{
  return "{\"choices\":[{\"message\":{\"content\":\"" + json_escape(text) +
         "\"},\"finish_reason\":\"stop\"}],"
         "\"usage\":{\"prompt_tokens\":1,\"completion_tokens\":1,\"total_tokens\":2}}";
}

std::string read_tool_body(std::string_view path)
{
  auto const arguments = std::string("{\"path\":\"") + json_escape(path) + "\"}";
  return "{\"choices\":[{\"message\":{\"tool_calls\":[{\"id\":\"call_read\",\"type\":\"function\","
         "\"function\":{\"name\":\"read_file\",\"arguments\":\"" +
         json_escape(arguments) + "\"}}]},\"finish_reason\":\"tool_calls\"}]}";
}

std::string write_tool_body(std::string_view path)
{
  auto const arguments = std::string("{\"path\":\"") + json_escape(path) + "\",\"content\":\"rpc new\\n\"}";
  return "{\"choices\":[{\"message\":{\"tool_calls\":[{\"id\":\"call_write\",\"type\":\"function\","
         "\"function\":{\"name\":\"write_file\",\"arguments\":\"" +
         json_escape(arguments) + "\"}}]},\"finish_reason\":\"tool_calls\"}]}";
}

std::string bash_tool_body(std::string_view marker_path)
{
  auto const command = std::string("/bin/sh -c \"sleep 1; printf leaked > ") + std::string(marker_path) + " & wait\"";
  auto const arguments = std::string("{\"command\":\"") + json_escape(command) + "\",\"timeout_ms\":50}";
  return "{\"choices\":[{\"message\":{\"tool_calls\":[{\"id\":\"call_bash\",\"type\":\"function\","
         "\"function\":{\"name\":\"bash\",\"arguments\":\"" +
         json_escape(arguments) + "\"}}]},\"finish_reason\":\"tool_calls\"}]}";
}

std::string question_tool_body()
{
  std::string const arguments =
      "{\"header\":\"Pick\",\"question\":\"Continue?\",\"options\":[{\"value\":\"yes\",\"label\":\"Yes\"}],"
      "\"allow_custom\":true}";
  return "{\"choices\":[{\"message\":{\"tool_calls\":[{\"id\":\"call_question\",\"type\":\"function\","
         "\"function\":{\"name\":\"question\",\"arguments\":\"" +
         json_escape(arguments) + "\"}}]},\"finish_reason\":\"tool_calls\"}]}";
}

struct ProviderResponse {
  int status_code = 200;
  std::string reason = "OK";
  std::string body;
};

ProviderResponse response_for(std::string_view scenario, int request_index, std::string_view target_path)
{
  if (scenario == "http-error") {
    return ProviderResponse{.status_code = 500,
                            .reason = "Internal Server Error",
                            .body =
                                "{\"error\":{\"message\":\"provider unavailable\","
                                "\"reasoning_content\":\"secret reasoning\","
                                "\"thinking\":\"secret thinking\",\"api_key\":\"secret-key\"}}"};
  }
  if (scenario == "read-tool") {
    return ProviderResponse{.body =
                                request_index == 0 ? read_tool_body(target_path) : text_body("after permission deny")};
  }
  if (scenario == "read-tool-twice") {
    if (request_index == 0 || request_index == 2) return ProviderResponse{.body = read_tool_body(target_path)};
    return ProviderResponse{.body = text_body(request_index == 1 ? "first session grant" : "second session grant")};
  }
  if (scenario == "read-tool-thrice") {
    if (request_index == 0 || request_index == 2 || request_index == 4) {
      return ProviderResponse{.body = read_tool_body(target_path)};
    }
    if (request_index == 1) return ProviderResponse{.body = text_body("first controlled grant")};
    return ProviderResponse{.body =
                                text_body(request_index == 3 ? "second controlled grant" : "third controlled grant")};
  }
  if (scenario == "read-missing-tool") {
    return ProviderResponse{.body = request_index == 0 ? read_tool_body(target_path) : text_body("after tool failure")};
  }
  if (scenario == "write-tool") {
    return ProviderResponse{.body =
                                request_index == 0 ? write_tool_body(target_path) : text_body("after permission deny")};
  }
  if (scenario == "bash-timeout-tree") {
    return ProviderResponse{.body = request_index == 0 ? bash_tool_body(target_path)
                                                       : text_body("after bash process cleanup")};
  }
  if (scenario == "question-tool") {
    return ProviderResponse{.body = request_index == 0 ? question_tool_body() : text_body("after question reply")};
  }
  if (scenario == "compact") {
    return ProviderResponse{.body = request_index == 0
                                        ? text_body("before compact")
                                        : text_body("# Goal\nHeadless compact summary\n# Next Steps\nContinue.")};
  }
  return ProviderResponse{.body = text_body("headless active prompt complete")};
}

}  // namespace

int main(int argc, char** argv)
{
  if (argc != 4 && argc != 6) {
    std::cerr << "usage: ava_fake_provider_server PORT_FILE REQUEST_LOG DELAY_MS [SCENARIO TARGET_PATH]\n";
    return 2;
  }

  std::filesystem::path const port_file = argv[1];
  std::filesystem::path const request_log = argv[2];
  auto const delay = std::chrono::milliseconds(std::stoi(argv[3]));
  std::string const scenario = argc == 6 ? argv[4] : "text";
  std::string const target_path = argc == 6 ? argv[5] : "";
  int const request_count = scenario == "http-error"         ? 3
                            : scenario == "read-tool-twice"  ? 4
                            : scenario == "read-tool-thrice" ? 6
                            : (scenario == "read-tool" || scenario == "read-missing-tool" || scenario == "write-tool" ||
                               scenario == "bash-timeout-tree" || scenario == "question-tool" || scenario == "compact")
                                ? 2
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

  {
    std::ofstream file(request_log, std::ios::binary | std::ios::trunc);
  }

  for (int request_index = 0; request_index < request_count; ++request_index) {
    Fd client(::accept(server.get(), nullptr, nullptr));
    if (client.get() < 0) {
      std::cerr << "accept failed: " << errno_text() << '\n';
      return 1;
    }

    auto const request = read_http_request(client.get());
    {
      std::ofstream file(request_log, std::ios::binary | std::ios::app);
      file << "--- request " << (request_index + 1) << " ---\n" << request << '\n';
    }
    if (request_index == 0) std::this_thread::sleep_for(delay);

    auto const provider_response = response_for(scenario, request_index, target_path);
    std::string const response =
        "HTTP/1.1 " + std::to_string(provider_response.status_code) + " " + provider_response.reason +
        "\r\nContent-Type: application/json\r\nContent-Length: " + std::to_string(provider_response.body.size()) +
        "\r\nConnection: close\r\n\r\n" + provider_response.body;
    if (!write_all(client.get(), response)) {
      std::cerr << "response write failed: " << errno_text() << '\n';
      return 1;
    }
  }
  return 0;
}
