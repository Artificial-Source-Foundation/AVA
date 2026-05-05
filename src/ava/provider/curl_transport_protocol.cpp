#include "ava/provider/curl_transport_protocol.h"

#include <algorithm>
#include <map>
#include <string>

namespace ava::provider::detail {
namespace {

bool is_http_status_line(std::string_view line)
{
  if (!line.starts_with("HTTP/")) return false;
  auto const space = line.find(' ');
  if (space == std::string_view::npos || space + 4 > line.size()) return false;
  return std::ranges::all_of(line.substr(space + 1, 3), [](char ch) { return ch >= '0' && ch <= '9'; });
}

int http_status_line_code(std::string_view line)
{
  auto const space = line.find(' ');
  if (space == std::string_view::npos || space + 4 > line.size()) return 0;
  int code = 0;
  for (char const ch : line.substr(space + 1, 3)) {
    if (ch < '0' || ch > '9') return 0;
    code = (code * 10) + (ch - '0');
  }
  return code;
}

}  // namespace

std::string curl_config_escape(std::string_view value)
{
  std::string escaped;
  escaped.reserve(value.size());
  for (char const ch : value) {
    switch (ch) {
      case '\\':
        escaped += "\\\\";
        break;
      case '"':
        escaped += "\\\"";
        break;
      case '\n':
      case '\r':
        escaped += ' ';
        break;
      default:
        escaped += ch;
        break;
    }
  }
  return escaped;
}

std::string build_curl_config(HttpRequest const& request, std::string const& body_path)
{
  std::string config;
  config += "url = \"" + curl_config_escape(request.url) + "\"\n";
  config += "request = \"" + curl_config_escape(request.method.empty() ? "POST" : request.method) + "\"\n";
  if (request.follow_redirects) {
    config += "location\n";
  }
  config += "max-redirs = \"5\"\n";
  config += "proto = \"=http,https\"\n";
  config += "proto-redir = \"=http,https\"\n";
  if (request.include_response_headers) {
    config += "include\n";
  }
  for (auto const& override : request.resolve_hosts) {
    config += "resolve = \"" + curl_config_escape(override) + "\"\n";
  }
  if (!request.resolve_hosts.empty()) {
    config += "noproxy = \"*\"\n";
  }
  config += "silent\n";
  config += "show-error\n";
  config += "no-progress-meter\n";
  config += "max-time = \"" + std::to_string(static_cast<double>(std::max(1, request.timeout_ms)) / 1000.0) + "\"\n";
  for (auto const& [name, value] : request.headers) {
    config += "header = \"" + curl_config_escape(name + ": " + value) + "\"\n";
  }
  if (!request.body.empty()) {
    config += "data-binary = \"@" + curl_config_escape(body_path) + "\"\n";
  }
  return config;
}

ava::core::Result<HttpResponse> parse_curl_output(std::string output, bool include_response_headers)
{
  auto const marker_pos = output.rfind(kCurlStatusMarker);
  if (marker_pos == std::string::npos) {
    auto error = ava::core::Error(ava::core::ErrorCategory::Provider, "curl response did not include an HTTP status");
    if (!output.empty()) error.with_context("output", output.substr(0, 512));
    return std::unexpected(std::move(error));
  }

  auto const status_text = output.substr(marker_pos + kCurlStatusMarker.size());
  int status = 0;
  for (char const ch : status_text) {
    if (ch < '0' || ch > '9') break;
    status = (status * 10) + (ch - '0');
  }
  output.resize(marker_pos);

  std::map<std::string, std::string> headers;
  while (include_response_headers && output.starts_with("HTTP/")) {
    auto body_start = output.find("\r\n\r\n");
    std::size_t separator_size = 4;
    if (body_start == std::string::npos) {
      body_start = output.find("\n\n");
      separator_size = 2;
    }
    if (body_start == std::string::npos) break;
    headers.clear();
    auto const header_text = output.substr(0, body_start);
    auto status_line = header_text.substr(0, header_text.find('\n'));
    if (!status_line.empty() && status_line.back() == '\r') status_line.pop_back();
    if (!is_http_status_line(status_line)) break;
    auto const block_status = http_status_line_code(status_line);
    std::size_t line_start = 0;
    bool first_line = true;
    while (line_start <= header_text.size()) {
      auto line_end = header_text.find('\n', line_start);
      auto line =
          header_text.substr(line_start, line_end == std::string::npos ? std::string::npos : line_end - line_start);
      if (!line.empty() && line.back() == '\r') line.pop_back();
      if (!first_line) {
        if (auto const colon = line.find(':'); colon != std::string::npos) {
          auto name = line.substr(0, colon);
          auto value = line.substr(colon + 1);
          while (!value.empty() && (value.front() == ' ' || value.front() == '\t')) value.erase(value.begin());
          headers[std::move(name)] = std::move(value);
        }
      }
      first_line = false;
      if (line_end == std::string::npos) break;
      line_start = line_end + 1;
    }
    output.erase(0, body_start + separator_size);
    if (block_status == status) break;
  }
  return HttpResponse{.status_code = status, .headers = std::move(headers), .body = std::move(output)};
}

}  // namespace ava::provider::detail
