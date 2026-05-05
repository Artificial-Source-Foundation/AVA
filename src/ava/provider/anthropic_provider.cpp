#include "ava/provider/anthropic_provider.h"

#include <cstdlib>
#include <string_view>
#include <utility>

#include "ava/provider/anthropic_request.h"

namespace ava::provider {
namespace {

constexpr std::string_view kDefaultAnthropicBaseUrl = "https://api.anthropic.com";
constexpr std::string_view kAnthropicVersion = "2023-06-01";

std::string configured_base_url()
{
  if (char const* value = std::getenv("ANTHROPIC_BASE_URL"); value && value[0] != '\0') return value;
  return std::string(kDefaultAnthropicBaseUrl);
}

std::string trim_trailing_slashes(std::string value)
{
  while (value.size() > 1 && value.back() == '/') value.pop_back();
  return value;
}

}  // namespace

AnthropicProvider::AnthropicProvider(std::string base_url)
    : base_url_(trim_trailing_slashes(base_url.empty() ? configured_base_url() : std::move(base_url)))
{
}

ava::core::Result<HttpRequest> AnthropicProvider::build_request(ProviderRequest const& request,
                                                                std::string_view access_token) const
{
  if (request.model_id.empty()) {
    return std::unexpected(ava::core::Error(ava::core::ErrorCategory::InvalidArgument, "model id is required"));
  }
  if (access_token.empty()) {
    return std::unexpected(
        ava::core::Error(ava::core::ErrorCategory::PermissionDenied, "Anthropic API key is required"));
  }
  auto body = anthropic_request_body_json(request);
  if (!body) return std::unexpected(std::move(body.error()));
  return HttpRequest{.method = "POST",
                     .url = base_url_ + "/v1/messages",
                     .headers = {{"x-api-key", std::string(access_token)},
                                 {"anthropic-version", std::string(kAnthropicVersion)},
                                 {"Content-Type", "application/json"},
                                 {"Accept", request.stream ? "text/event-stream" : "application/json"}},
                     .body = std::move(*body),
                     .timeout_ms = 60000,
                     .follow_redirects = false,
                     .include_response_headers = false,
                     .resolve_hosts = {}};
}

std::unique_ptr<StreamParser> AnthropicProvider::create_stream_parser() const
{
  return std::make_unique<AnthropicStreamParser>();
}

ava::core::VoidResult AnthropicProvider::apply_auth_options(HttpRequest& request, ProviderAuthContext const& auth) const
{
  if (auth.credential_type != "oauth") return {};
  request.headers.erase("x-api-key");
  request.headers["Authorization"] = "Bearer " + auth.access_token;
  request.headers["user-agent"] = "ava";
  request.headers["x-app"] = "cli";
  return {};
}

ava::core::Result<std::vector<StreamEvent>> AnthropicProvider::parse_response(HttpResponse const& response,
                                                                              bool stream) const
{
  return stream ? parse_anthropic_sse_response(response) : parse_anthropic_response(response);
}

}  // namespace ava::provider
