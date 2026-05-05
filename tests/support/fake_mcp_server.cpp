#include "ava/core/json.h"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <iostream>
#include <optional>
#include <string>
#include <string_view>
#include <thread>

namespace {

std::string lowercase(std::string value)
{
  std::ranges::transform(value, value.begin(), [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
  return value;
}

std::string trim(std::string value)
{
  auto first = value.begin();
  while (first != value.end() && std::isspace(static_cast<unsigned char>(*first)) != 0) ++first;
  auto last = value.end();
  while (last != first && std::isspace(static_cast<unsigned char>(*(last - 1))) != 0) --last;
  return std::string(first, last);
}

std::optional<std::string> read_message()
{
  std::optional<std::size_t> content_length;
  std::string line;
  bool saw_header = false;
  while (std::getline(std::cin, line)) {
    saw_header = true;
    if (!line.empty() && line.back() == '\r') line.pop_back();
    if (line.empty()) break;
    auto const colon = line.find(':');
    if (colon == std::string::npos) continue;
    if (lowercase(trim(line.substr(0, colon))) != "content-length") continue;
    try {
      content_length = static_cast<std::size_t>(std::stoull(trim(line.substr(colon + 1))));
    } catch (...) {
      return std::nullopt;
    }
  }
  if (!saw_header || !content_length) return std::nullopt;

  std::string body(*content_length, '\0');
  std::cin.read(body.data(), static_cast<std::streamsize>(body.size()));
  if (std::cin.gcount() != static_cast<std::streamsize>(body.size())) return std::nullopt;
  return body;
}

void write_message(std::string_view body)
{
  std::cout << "Content-Length: " << body.size() << "\r\n\r\n" << body;
  std::cout.flush();
}

std::string json_string(std::string_view value)
{
  return "\"" + ava::core::json::escape(value) + "\"";
}

std::string response(std::string_view id, std::string_view result)
{
  return "{\"jsonrpc\":\"2.0\",\"id\":" + json_string(id) + ",\"result\":" + std::string(result) + "}";
}

std::string error_response(std::string_view id, std::string_view message)
{
  return "{\"jsonrpc\":\"2.0\",\"id\":" + json_string(id) +
         ",\"error\":{\"code\":-32000,\"message\":" + json_string(message) + "}}";
}

}  // namespace

int main(int argc, char** argv)
{
  std::string mode = "ok";
  if (argc > 1) mode = argv[1];
  if (mode == "stderr-noise") {
    std::cerr << std::string(96, 'x') << "mcp-stderr-tail!";
    std::cerr.flush();
  }

  while (auto message = read_message()) {
    auto const method = ava::core::json::string_field(*message, "method");
    auto const id = ava::core::json::string_field(*message, "id");
    if (!method || !id) continue;

    if (mode == "timeout-initialize" && *method == "initialize") {
      std::this_thread::sleep_for(std::chrono::seconds(2));
      continue;
    }
    if (mode == "exit-initialize" && *method == "initialize") {
      std::cerr << "fake MCP exited during initialize";
      std::cerr.flush();
      return 42;
    }
    if (mode == "malformed" && *method == "initialize") {
      write_message("not-json");
      continue;
    }

    if (*method == "initialize") {
      write_message(response(*id,
                             "{\"protocolVersion\":\"2024-11-05\",\"capabilities\":{\"tools\":{}},"
                             "\"serverInfo\":{\"name\":\"fake-mcp\",\"version\":\"1.0.0\"}}"));
    } else if (*method == "tools/list") {
      if (mode == "exit-after-initialize") {
        std::cerr << "fake MCP exited during tools/list";
        std::cerr.flush();
        return 43;
      }
      write_message(response(*id,
                             "{\"tools\":[{\"name\":\"echo\",\"description\":\"Echo test tool\","
                             "\"inputSchema\":{\"type\":\"object\",\"properties\":{\"text\":{\"type\":"
                             "\"string\"}},\"required\":[\"text\"]}}]}"));
    } else if (*method == "tools/call") {
      if (mode == "tool-error") {
        write_message(response(*id,
                               "{\"isError\":true,\"content\":[{\"type\":\"text\",\"text\":\"MCP tool "
                               "failed\"}]}"));
      } else if (mode == "slow-tool") {
        std::this_thread::sleep_for(std::chrono::seconds(2));
        write_message(response(*id,
                               "{\"isError\":false,\"content\":[{\"type\":\"text\",\"text\":\"MCP slow "
                               "call ok\"}]}"));
      } else {
        write_message(response(*id,
                               "{\"isError\":false,\"content\":[{\"type\":\"text\",\"text\":\"MCP call "
                               "ok\"}]}"));
      }
    } else {
      write_message(error_response(*id, "unsupported method"));
    }
  }
  return 0;
}
