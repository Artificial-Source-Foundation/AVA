#include <chrono>
#include <cstdlib>
#include <iostream>
#include <string>
#include <thread>

#include <nlohmann/json.hpp>

namespace {

void write_message(const nlohmann::json& message) {
  std::cout << message.dump() << '\n' << std::flush;
}

[[nodiscard]] int parse_positive_int(const char* raw, int fallback) {
  if(raw == nullptr) {
    return fallback;
  }

  const int parsed = std::atoi(raw);
  if(parsed <= 0) {
    return fallback;
  }
  return parsed;
}

int run_blank_heartbeat_mode(int total_ms, int interval_ms) {
  const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(total_ms);
  while(std::chrono::steady_clock::now() < deadline) {
    std::cout << '\n' << std::flush;
    std::this_thread::sleep_for(std::chrono::milliseconds(interval_ms));
  }

  write_message(nlohmann::json{
      {"jsonrpc", "2.0"},
      {"id", 1},
      {"result", nlohmann::json::object()},
  });
  return 0;
}

int run_echo_env_mode(const std::string& env_var_name) {
  std::string line;
  while(std::getline(std::cin, line)) {
    if(line.find_first_not_of(" \t\r\n") == std::string::npos) {
      continue;
    }

    nlohmann::json request;
    try {
      request = nlohmann::json::parse(line);
    } catch(const nlohmann::json::exception&) {
      continue;
    }

    if(!request.contains("id") || request.at("id").is_null()) {
      continue;
    }

    const char* value = std::getenv(env_var_name.c_str());
    write_message(nlohmann::json{
        {"jsonrpc", "2.0"},
        {"id", request.at("id")},
        {"result", nlohmann::json{{"value", value != nullptr ? value : ""}}},
    });
    return 0;
  }

  return 0;
}

}  // namespace

int main(int argc, char** argv) {
  if(argc >= 2 && std::string{argv[1]} == "--blank-heartbeat") {
    const auto total_ms = parse_positive_int(argc >= 3 ? argv[2] : nullptr, 250);
    const auto interval_ms = parse_positive_int(argc >= 4 ? argv[3] : nullptr, 5);
    return run_blank_heartbeat_mode(total_ms, interval_ms);
  }

  if(argc >= 3 && std::string{argv[1]} == "--echo-env") {
    return run_echo_env_mode(argv[2]);
  }

  std::string line;
  while(std::getline(std::cin, line)) {
    if(line.find_first_not_of(" \t\r\n") == std::string::npos) {
      continue;
    }

    nlohmann::json request;
    try {
      request = nlohmann::json::parse(line);
    } catch(const nlohmann::json::exception&) {
      continue;
    }

    if(!request.is_object() || request.value("jsonrpc", std::string{}) != "2.0" ||
       !request.contains("method") || !request.at("method").is_string()) {
      continue;
    }

    const auto method = request.at("method").get<std::string>();

    if(!request.contains("id") || request.at("id").is_null()) {
      continue;
    }

    const auto& id = request.at("id");

    if(method == "initialize") {
      write_message(nlohmann::json{
          {"jsonrpc", "2.0"},
          {"id", id},
          {"result",
           nlohmann::json{
               {"protocolVersion", "2024-11-05"},
               {"capabilities", nlohmann::json{{"tools", nlohmann::json::object()}}},
               {"serverInfo", nlohmann::json{{"name", "mock-local"}, {"version", "1.0"}}},
           }},
      });
      continue;
    }

    if(method == "tools/list") {
      write_message(nlohmann::json{
          {"jsonrpc", "2.0"},
          {"id", id},
          {"result",
           nlohmann::json{{"tools", nlohmann::json::array({nlohmann::json{
                                              {"name", "echo"},
                                              {"description", "Echo from local mock server"},
                                              {"inputSchema", nlohmann::json{{"type", "object"}}},
                                          }})}}},
      });
      continue;
    }

    if(method == "tools/call") {
      const auto params = request.value("params", nlohmann::json::object());
      const auto arguments = params.value("arguments", nlohmann::json::object());
      const auto text = arguments.value("text", std::string{});
      write_message(nlohmann::json{
          {"jsonrpc", "2.0"},
          {"id", id},
          {"result",
           nlohmann::json{
               {"content", nlohmann::json::array({nlohmann::json{{"type", "text"}, {"text", text}}})},
               {"isError", false},
           }},
      });
      continue;
    }

    if(method == "ping") {
      write_message(nlohmann::json{{"jsonrpc", "2.0"}, {"id", id}, {"result", nlohmann::json::object()}});
      continue;
    }

    write_message(nlohmann::json{
        {"jsonrpc", "2.0"},
        {"id", id},
        {"error", nlohmann::json{{"code", -32601}, {"message", "method not found"}}},
    });
  }

  return 0;
}
