#include <unistd.h>

#include <cctype>
#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <optional>
#include <string>
#include <string_view>

#include "ava/core/json.h"

namespace {

enum class Mode {
  Normal,
  EchoUriDiagnostics,
  SleepInitialize,
  MalformedDiagnostics,
  CrashDiagnostics,
  SleepDiagnostics,
  HugeContentLength,
  HugeHeader,
};

ssize_t read_retry(char* data, std::size_t size) {
  while (true) {
    auto const bytes = read(STDIN_FILENO, data, size);
    if (bytes < 0 && errno == EINTR) continue;
    return bytes;
  }
}

std::optional<std::string> read_exact(std::size_t size) {
  std::string result(size, '\0');
  std::size_t offset = 0;
  while (offset < size) {
    auto const bytes = read_retry(result.data() + offset, size - offset);
    if (bytes <= 0) return std::nullopt;
    offset += static_cast<std::size_t>(bytes);
  }
  return result;
}

std::optional<std::size_t> content_length(std::string_view header) {
  constexpr std::string_view key = "Content-Length:";
  auto const position = header.find(key);
  if (position == std::string_view::npos) return std::nullopt;
  std::size_t index = position + key.size();
  while (index < header.size() && (header[index] == ' ' || header[index] == '\t')) ++index;
  std::size_t end = index;
  while (end < header.size() && std::isdigit(static_cast<unsigned char>(header[end])) != 0) ++end;
  if (end == index) return std::nullopt;
  return static_cast<std::size_t>(std::stoull(std::string(header.substr(index, end - index))));
}

std::optional<std::string> read_message() {
  std::string header;
  char ch = '\0';
  while (header.find("\r\n\r\n") == std::string::npos) {
    auto const bytes = read_retry(&ch, 1);
    if (bytes <= 0) return std::nullopt;
    header.push_back(ch);
  }
  auto const length = content_length(header);
  if (!length) return std::nullopt;
  return read_exact(*length);
}

void write_message(std::string_view body) {
  std::cout << "Content-Length: " << body.size() << "\r\n\r\n" << body;
  std::cout.flush();
}

Mode parse_mode(int argc, char** argv) {
  for (int index = 1; index < argc; ++index) {
    if (std::strcmp(argv[index], "--malformed-diagnostics") == 0) return Mode::MalformedDiagnostics;
    if (std::strcmp(argv[index], "--crash-diagnostics") == 0) return Mode::CrashDiagnostics;
    if (std::strcmp(argv[index], "--echo-uri-diagnostics") == 0) return Mode::EchoUriDiagnostics;
    if (std::strcmp(argv[index], "--sleep-initialize") == 0) return Mode::SleepInitialize;
    if (std::strcmp(argv[index], "--sleep-diagnostics") == 0) return Mode::SleepDiagnostics;
    if (std::strcmp(argv[index], "--huge-content-length") == 0) return Mode::HugeContentLength;
    if (std::strcmp(argv[index], "--huge-header") == 0) return Mode::HugeHeader;
  }
  return Mode::Normal;
}

void respond_initialize(long long id) {
  std::string const body = "{\"jsonrpc\":\"2.0\",\"id\":" + std::to_string(id) +
                           ",\"result\":{\"capabilities\":{\"diagnosticProvider\":{\"interFileDependencies\":false,"
                           "\"workspaceDiagnostics\":false}}}}";
  write_message(body);
}

std::string request_uri(std::string_view message) {
  auto const params = ava::core::json::object_field(message, "params");
  auto const document = params ? ava::core::json::object_field(*params, "textDocument") : std::nullopt;
  auto const uri = document ? ava::core::json::string_field(*document, "uri") : std::nullopt;
  return uri.value_or(std::string{});
}

void respond_diagnostics(long long id, Mode mode, std::string_view uri) {
  if (mode == Mode::CrashDiagnostics) std::exit(23);
  if (mode == Mode::SleepDiagnostics) {
    usleep(1000000);
    return;
  }
  if (mode == Mode::MalformedDiagnostics) {
    write_message("not-json");
    return;
  }
  if (mode == Mode::HugeContentLength) {
    std::cout << "Content-Length: 4194305\r\n\r\n";
    std::cout.flush();
    return;
  }
  if (mode == Mode::HugeHeader) {
    std::cout << std::string(70 * 1024, 'X');
    std::cout.flush();
    return;
  }
  auto const message = mode == Mode::EchoUriDiagnostics ? std::string(uri) : std::string("fake diagnostic from LSP");
  std::string const body = "{\"jsonrpc\":\"2.0\",\"id\":" + std::to_string(id) +
                           ",\"result\":{\"kind\":\"full\",\"items\":[{\"range\":{\"start\":{\"line\":2,"
                           "\"character\":4},\"end\":{\"line\":2,\"character\":9}},\"severity\":1,"
                           "\"code\":\"AVA_FAKE\",\"source\":\"ava_fake_lsp\","
                           "\"message\":\"" +
                           ava::core::json::escape(message) + "\"}]}}";
  write_message(body);
}

}  // namespace

int main(int argc, char** argv) {
  auto const mode = parse_mode(argc, argv);
  while (auto message = read_message()) {
    auto const method = ava::core::json::string_field(*message, "method");
    auto const id = ava::core::json::integer_field(*message, "id");
    if (!method) continue;
    if (*method == "initialize" && id) {
      if (mode == Mode::SleepInitialize) {
        usleep(1000000);
        continue;
      }
      respond_initialize(*id);
    } else if (*method == "textDocument/diagnostic" && id) {
      respond_diagnostics(*id, mode, request_uri(*message));
    } else if (*method == "exit") {
      return 0;
    }
  }
  return 0;
}
