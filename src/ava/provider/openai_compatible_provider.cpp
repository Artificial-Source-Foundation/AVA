#include "ava/provider/openai_compatible_provider.h"

#include <map>
#include <utility>

#include "ava/provider/openai_compatible_response.h"
#include "ava/provider/openai_provider.h"
#include "ava/provider/provider_utils.h"

namespace ava::provider {

OpenAICompatibleProvider::OpenAICompatibleProvider(OpenAICompatibleProviderOptions options)
    : options_(std::move(options))
{
}

ava::core::Result<HttpRequest> OpenAICompatibleProvider::build_request(ProviderRequest const& request,
                                                                       std::string_view access_token) const
{
  if (request.model_id.empty()) {
    return std::unexpected(ava::core::Error(ava::core::ErrorCategory::InvalidArgument, "model id is required"));
  }
  if (access_token.empty()) {
    return std::unexpected(ava::core::Error(ava::core::ErrorCategory::PermissionDenied,
                                            options_.provider_name + " bearer token is required"));
  }
  if (auto valid_tools = validate_openai_compatible_tools_json(request); !valid_tools)
    return std::unexpected(std::move(valid_tools.error()));
  for (auto const& tool : request.tools_json) {
    auto converted = chat_completion_tool_json(tool);
    if (!converted) return std::unexpected(std::move(converted.error()));
  }

  std::map<std::string, std::string> headers{{"Authorization", "Bearer " + std::string(access_token)},
                                             {"Content-Type", "application/json"},
                                             {"Accept", request.stream ? "text/event-stream" : "application/json"}};
  if (!options_.user_agent.empty()) headers["User-Agent"] = options_.user_agent;
  return HttpRequest{.method = "POST",
                     .url = openai_compatible_join_url(options_.base_url, options_.chat_completions_path),
                     .headers = std::move(headers),
                     .body = openai_compatible_request_body_json(request, options_),
                     .timeout_ms = 60000,
                     .follow_redirects = true,
                     .include_response_headers = false,
                     .resolve_hosts = {}};
}

std::unique_ptr<StreamParser> OpenAICompatibleProvider::create_stream_parser() const
{
  return std::make_unique<OpenAICompatibleStreamParser>(options_.reasoning_format);
}

ava::core::Result<std::vector<StreamEvent>> OpenAICompatibleProvider::parse_response(HttpResponse const& response,
                                                                                     bool stream) const
{
  if (response.status_code < 200 || response.status_code >= 300) {
    auto const kind = classify_provider_error(response);
    auto error = ava::core::Error(
        ava::core::ErrorCategory::Provider,
        options_.provider_name + " HTTP request failed with status " + std::to_string(response.status_code));
    error.with_context("status", std::to_string(response.status_code));
    error.with_context("provider_error_kind", to_string(kind));
    if (auto const retry_after = retry_after_header(response)) error.with_context("retry_after", *retry_after);
    if (!response.body.empty()) {
      error.with_context("body_snippet", detail::sanitized_openai_compatible_snippet(response.body));
    }
    return std::unexpected(std::move(error));
  }
  if (stream) return parse_openai_compatible_sse(response.body, options_.reasoning_format);
  return detail::parse_openai_compatible_chat_response(response.body, options_.reasoning_format);
}

}  // namespace ava::provider
