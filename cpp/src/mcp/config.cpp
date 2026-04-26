#include "ava/mcp/config.hpp"

#include <cstdint>
#include <fstream>
#include <limits>
#include <stdexcept>
#include <string_view>

#include "http_validation.hpp"

namespace ava::mcp {
namespace {

constexpr std::uint32_t kDefaultReceiveTimeoutMs = 5000;
constexpr std::uint32_t kMaxReceiveTimeoutMs = 120000;
constexpr std::uint32_t kDefaultRequestTimeoutMs = 5000;
constexpr std::uint32_t kMaxRequestTimeoutMs = 120000;

[[nodiscard]] std::vector<std::string>
parse_string_array(const nlohmann::json &value, const char *field,
                   std::string_view error_prefix) {
  std::vector<std::string> result;
  if (!value.contains(field)) {
    return result;
  }
  const auto &array = value.at(field);
  if (!array.is_array()) {
    throw std::runtime_error(std::string(error_prefix) + " " + field +
                             " must be an array");
  }
  for (const auto &entry : array) {
    result.push_back(entry.get<std::string>());
  }
  return result;
}

[[nodiscard]] std::map<std::string, std::string>
parse_string_map(const nlohmann::json &value, const char *field,
                 std::string_view error_prefix) {
  std::map<std::string, std::string> env;
  if (!value.contains(field)) {
    return env;
  }
  const auto &env_value = value.at(field);
  if (!env_value.is_object()) {
    throw std::runtime_error(std::string(error_prefix) + " " + field +
                             " must be an object");
  }
  for (const auto &[key, item] : env_value.items()) {
    env.emplace(key, item.get<std::string>());
  }
  return env;
}

[[nodiscard]] std::map<std::string, std::string>
parse_env(const nlohmann::json &value) {
  return parse_string_map(value, "env", "MCP stdio transport");
}

[[nodiscard]] std::uint32_t
parse_receive_timeout_ms(const nlohmann::json &transport) {
  if (!transport.contains("receiveTimeoutMs")) {
    return kDefaultReceiveTimeoutMs;
  }

  const auto &raw_timeout = transport.at("receiveTimeoutMs");
  if (!raw_timeout.is_number_integer()) {
    throw std::runtime_error(
        "MCP stdio transport receiveTimeoutMs must be a positive integer");
  }

  const auto value = raw_timeout.get<std::int64_t>();
  if (value <= 0) {
    throw std::runtime_error(
        "MCP stdio transport receiveTimeoutMs must be positive");
  }
  if (value > static_cast<std::int64_t>(kMaxReceiveTimeoutMs)) {
    throw std::runtime_error(
        "MCP stdio transport receiveTimeoutMs must not exceed " +
        std::to_string(kMaxReceiveTimeoutMs));
  }
  return static_cast<std::uint32_t>(value);
}

[[nodiscard]] std::uint32_t
parse_request_timeout_ms(const nlohmann::json &transport) {
  if (!transport.contains("requestTimeoutMs")) {
    return kDefaultRequestTimeoutMs;
  }

  const auto &raw_timeout = transport.at("requestTimeoutMs");
  if (!raw_timeout.is_number_integer()) {
    throw std::runtime_error(
        "MCP remote transport requestTimeoutMs must be a positive integer");
  }

  const auto value = raw_timeout.get<std::int64_t>();
  if (value <= 0) {
    throw std::runtime_error(
        "MCP remote transport requestTimeoutMs must be positive");
  }
  if (value > static_cast<std::int64_t>(kMaxRequestTimeoutMs)) {
    throw std::runtime_error(
        "MCP remote transport requestTimeoutMs must not exceed " +
        std::to_string(kMaxRequestTimeoutMs));
  }
  return static_cast<std::uint32_t>(value);
}

[[nodiscard]] RemoteAuthConfig
parse_remote_auth(const nlohmann::json &transport) {
  RemoteAuthConfig auth;
  if (transport.contains("bearerTokenEnv")) {
    auth.bearer_token_env = transport.at("bearerTokenEnv").get<std::string>();
    if (auth.bearer_token_env.empty()) {
      throw std::runtime_error(
          "MCP remote transport bearerTokenEnv must not be empty");
    }
  }

  if (!transport.contains("oauth")) {
    return auth;
  }

  const auto &oauth = transport.at("oauth");
  if (!oauth.is_object()) {
    throw std::runtime_error("MCP remote transport oauth must be an object");
  }
  for (const auto *forbidden :
       {"accessToken", "refreshToken", "clientSecret"}) {
    if (oauth.contains(forbidden)) {
      throw std::runtime_error(
          std::string("MCP remote transport oauth must not contain inline "
                      "credential field: ") +
          forbidden);
    }
  }

  auth.oauth_issuer = oauth.value("issuer", std::string{});
  auth.oauth_client_id = oauth.value("clientId", std::string{});
  auth.oauth_scopes =
      parse_string_array(oauth, "scopes", "MCP remote transport oauth");
  return auth;
}

[[nodiscard]] bool has_configured_remote_auth(const RemoteAuthConfig &auth) {
  return !auth.bearer_token_env.empty() || !auth.oauth_issuer.empty() ||
         !auth.oauth_client_id.empty() || !auth.oauth_scopes.empty();
}

[[nodiscard]] RemoteTransportConfig
parse_remote_transport(const nlohmann::json &transport, std::string_view type) {
  RemoteTransportConfig remote;
  remote.url = transport.at("url").get<std::string>();
  if (remote.url.empty()) {
    throw std::runtime_error("MCP remote transport url must not be empty");
  }
  if (!detail::has_http_scheme(remote.url)) {
    throw std::runtime_error(
        "MCP remote transport url must start with http:// or https://");
  }
  if (detail::has_url_userinfo(remote.url)) {
    throw std::runtime_error(
        "MCP remote transport url must not include URL userinfo");
  }

  remote.headers =
      parse_string_map(transport, "headers", "MCP remote transport");
  detail::validate_remote_headers(remote.headers, "MCP remote transport");
  remote.request_timeout_ms = parse_request_timeout_ms(transport);
  remote.auth = parse_remote_auth(transport);
  if (has_configured_remote_auth(remote.auth) &&
      !detail::has_https_scheme(remote.url)) {
    throw std::runtime_error(
        "MCP remote transport with authentication requires https://");
  }
  if (type == "sse" && detail::has_header(remote.headers, "accept")) {
    throw std::runtime_error(
        "MCP SSE transport owns the Accept header; configure auth separately");
  }
  return remote;
}

[[nodiscard]] McpServerConfig parse_server(const nlohmann::json &value) {
  if (!value.is_object()) {
    throw std::runtime_error("MCP server config must be an object");
  }
  McpServerConfig server;
  server.name = value.at("name").get<std::string>();
  if (server.name.empty()) {
    throw std::runtime_error("MCP server name must not be empty");
  }
  server.enabled = value.value("enabled", true);

  const auto &transport = value.at("transport");
  if (!transport.is_object()) {
    throw std::runtime_error("MCP server transport must be an object");
  }
  const auto type = transport.at("type").get<std::string>();
  if (type == "http" || type == "sse") {
    server.transport_type =
        type == "http" ? TransportType::Http : TransportType::Sse;
    server.remote = parse_remote_transport(transport, type);
    return server;
  }
  if (type != "stdio") {
    throw std::runtime_error("MCP transport type is not recognized: " + type);
  }
  server.transport_type = TransportType::Stdio;
  server.stdio.command = transport.at("command").get<std::string>();
  if (server.stdio.command.empty()) {
    throw std::runtime_error("MCP stdio transport command must not be empty");
  }
  server.stdio.args =
      parse_string_array(transport, "args", "MCP stdio transport");
  server.stdio.env = parse_env(transport);
  server.stdio.receive_timeout_ms = parse_receive_timeout_ms(transport);
  return server;
}

} // namespace

McpConfig parse_mcp_config_json(const nlohmann::json &value) {
  try {
    if (!value.is_object()) {
      throw std::runtime_error("MCP config must be an object");
    }
    McpConfig config;
    const auto servers = value.value("servers", nlohmann::json::array());
    if (!servers.is_array()) {
      throw std::runtime_error("MCP config servers must be an array");
    }
    for (const auto &server : servers) {
      config.servers.push_back(parse_server(server));
    }
    return config;
  } catch (const nlohmann::json::exception &e) {
    throw std::runtime_error(std::string("MCP config is malformed: ") +
                             e.what());
  }
}

McpConfig load_mcp_config_file(const std::filesystem::path &path) {
  if (!std::filesystem::exists(path)) {
    return {};
  }
  std::ifstream input(path);
  if (!input) {
    throw std::runtime_error("Failed to open MCP config file: " +
                             path.string());
  }
  try {
    nlohmann::json value;
    input >> value;
    return parse_mcp_config_json(value);
  } catch (const nlohmann::json::exception &e) {
    throw std::runtime_error(std::string("MCP config file is malformed: ") +
                             e.what());
  }
}

} // namespace ava::mcp
