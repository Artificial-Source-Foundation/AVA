#include "ava/tools/webfetch_tool.h"

#include <algorithm>
#include <string>
#include <utility>
#include <vector>

#include "ava/provider/curl_transport.h"
#include "ava/tools/webfetch_support.h"

namespace ava::tools {

ava::core::Result<WebFetchResult> webfetch(ToolContext const& context, std::string_view url, WebFetchOptions options)
{
  if (context.cancel_requested && context.cancel_requested()) {
    return std::unexpected(ava::core::Error(ava::core::ErrorCategory::Unknown, "tool canceled"));
  }
  auto safe_url = detail::validate_webfetch_url(url);
  if (!safe_url) return std::unexpected(std::move(safe_url.error()));

  if (auto permission = ensure_permission(context, ava::permissions::Operation::NetworkFetch, {}, safe_url->url,
                                          "webfetch", "network fetch requires permission");
      !permission) {
    return std::unexpected(std::move(permission.error()));
  }
  if (context.cancel_requested && context.cancel_requested()) {
    return std::unexpected(ava::core::Error(ava::core::ErrorCategory::Unknown, "tool canceled"));
  }

  auto const max_bytes = detail::normalized_webfetch_max_bytes(options.max_bytes);
  auto const timeout_ms = detail::normalized_webfetch_timeout_ms(options.timeout_ms);

  std::vector<std::string> resolve_hosts;
  if (options.transport == nullptr) {
    auto resolved = detail::validate_resolved_webfetch_host(safe_url->host);
    if (!resolved) return std::unexpected(std::move(resolved.error()));
    resolve_hosts.push_back(safe_url->host + ":" + safe_url->port + ":" + *resolved);
  }

  ava::provider::CurlCliTransport default_transport;
  auto& transport = options.transport ? *options.transport : static_cast<ava::provider::Transport&>(default_transport);
  auto response = transport.send(
      ava::provider::HttpRequest{
          .method = "GET",
          .url = safe_url->url,
          .headers = {{"Accept", "text/html,text/plain,application/json,application/xml;q=0.9,*/*;q=0.1"},
                      {"User-Agent", "AVA/1.0 webfetch"}},
          .body = "",
          .timeout_ms = timeout_ms,
          .follow_redirects = false,
          .include_response_headers = true,
          .resolve_hosts = std::move(resolve_hosts)},
      context.cancel_requested);
  if (!response) return std::unexpected(std::move(response.error()));
  if (context.cancel_requested && context.cancel_requested()) {
    return std::unexpected(ava::core::Error(ava::core::ErrorCategory::Unknown, "tool canceled"));
  }
  if (response->status_code < 200 || response->status_code >= 300) {
    auto error = ava::core::Error(ava::core::ErrorCategory::Provider, "webfetch HTTP request failed");
    error.with_context("status_code", std::to_string(response->status_code));
    error.with_context("url", safe_url->url);
    return std::unexpected(std::move(error));
  }

  auto const content_type = detail::webfetch_header_value(response->headers, "content-type");
  if (detail::webfetch_looks_binary(response->body, content_type)) {
    auto error = ava::core::Error(ava::core::ErrorCategory::InvalidArgument, "webfetch response appears to be binary");
    if (!content_type.empty()) error.with_context("content_type", content_type);
    return std::unexpected(std::move(error));
  }

  WebFetchResult result;
  result.url = safe_url->url;
  result.status_code = response->status_code;
  result.content_type = content_type;
  result.total_bytes = response->body.size();
  result.output_bytes = std::min(max_bytes, response->body.size());
  result.truncated = result.output_bytes < result.total_bytes;
  result.content = response->body.substr(0, result.output_bytes);
  return result;
}

}  // namespace ava::tools
