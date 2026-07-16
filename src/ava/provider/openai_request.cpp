#include "sys.h"
#include "ava/provider/openai_provider.h"
#include "ava/provider/openai_request.h"
#include "ava/provider/provider_utils.h"
#include "ava/core/json.h"

#include <string>
#include <utility>

namespace ava::provider {
namespace detail {
namespace {

constexpr std::string_view kCodexResponsesUrl = "https://chatgpt.com/backend-api/codex/responses";

std::string image_data_url(ContentPart const& part)
{
  return "data:" + ava::core::json::escape(part.mime_type) + ";base64," + ava::core::json::escape(part.data_base64);
}

bool has_image_parts(ChatMessage const& message)
{
  for (auto const& part : message.content_parts)
  {
    if (part.type == ContentPartType::Image)
      return true;
  }
  return false;
}

std::string input_item_json(ChatMessage const& message)
{
  if (!message.content_parts.empty() && has_image_parts(message))
  {
    std::string json = "{\"role\":\"" + ava::core::json::escape(message.role) + "\",\"content\":[";
    bool first = true;
    for (auto const& part : message.content_parts)
    {
      if (part.type == ContentPartType::Text)
      {
        if (part.text.empty())
          continue;
        if (!first)
          json += ',';
        first = false;
        json += "{\"type\":\"input_text\",\"text\":\"" + ava::core::json::escape(part.text) + "\"}";
      }
      else if (part.type == ContentPartType::Image)
      {
        if (!first)
          json += ',';
        first = false;
        json += "{\"type\":\"input_image\",\"image_url\":\"" + image_data_url(part) + "\"}";
      }
    }
    if (first)
      json += "{\"type\":\"input_text\",\"text\":\"\"}";
    json += "]}";
    return json;
  }
  return "{\"role\":\"" + ava::core::json::escape(message.role) + "\",\"content\":\"" + ava::core::json::escape(message.content) + "\"}";
}

ava::core::VoidResult validate_image_payloads(ProviderRequest const& request)
{
  for (std::size_t message_index = 0; message_index < request.messages.size(); ++message_index)
  {
    auto const& message = request.messages[message_index];
    for (std::size_t part_index = 0; part_index < message.content_parts.size(); ++part_index)
    {
      auto const& part = message.content_parts[part_index];
      if (part.type != ContentPartType::Image)
        continue;
      if (message.role != "user")
      {
        auto error = ava::core::Error(ava::core::ErrorCategory::InvalidArgument, "OpenAI image content requires user role");
        error.with_context("message_index", std::to_string(message_index));
        error.with_context("content_part_index", std::to_string(part_index));
        return std::unexpected(std::move(error));
      }
      if (!is_supported_image_mime_type(part.mime_type))
      {
        auto error = ava::core::Error(ava::core::ErrorCategory::InvalidArgument, "OpenAI image MIME type is not supported");
        error.with_context("message_index", std::to_string(message_index));
        error.with_context("content_part_index", std::to_string(part_index));
        return std::unexpected(std::move(error));
      }
      if (part.data_base64.empty() || !is_valid_base64(part.data_base64))
      {
        auto error = ava::core::Error(ava::core::ErrorCategory::InvalidArgument, "OpenAI image content requires verified attachment bytes");
        error.with_context("message_index", std::to_string(message_index));
        error.with_context("content_part_index", std::to_string(part_index));
        return std::unexpected(std::move(error));
      }
    }
  }
  return {};
}

ava::core::VoidResult validate_tools_json(ProviderRequest const& request)
{
  for (std::size_t index = 0; index < request.tools_json.size(); ++index)
  {
    if (is_json_object_shape(request.tools_json[index]))
      continue;
    auto error = ava::core::Error(ava::core::ErrorCategory::InvalidArgument, "OpenAI tool JSON must be an object");
    error.with_context("tool_index", std::to_string(index));
    return std::unexpected(std::move(error));
  }
  return {};
}

bool valid_openai_reasoning_effort(std::string_view effort)
{
  return effort == "none" || effort == "minimal" || effort == "low" || effort == "medium" || effort == "high" || effort == "xhigh" || effort == "max";
}

ava::core::VoidResult validate_reasoning_options(ProviderRequest const& request)
{
  if (!request.reasoning)
    return {};
  if (!valid_openai_reasoning_effort(request.reasoning->type))
  {
    auto error = ava::core::Error(ava::core::ErrorCategory::InvalidArgument, "OpenAI reasoning effort is not supported by this model");
    error.with_context("effort", request.reasoning->type);
    return std::unexpected(std::move(error));
  }
  if (request.reasoning->budget_tokens || !request.reasoning->display.empty())
  {
    return std::unexpected(ava::core::Error(ava::core::ErrorCategory::InvalidArgument, "OpenAI Responses reasoning supports effort only"));
  }
  return {};
}

std::string reasoning_options_json(ProviderRequest const& request)
{
  if (!request.reasoning)
    return {};
  std::string json = ",\"reasoning\":{\"effort\":\"" + ava::core::json::escape(request.reasoning->type) + "\"";
  if (request.reasoning->type != "none")
  {
    json += ",\"summary\":\"auto\"";
  }
  json += '}';
  return json;
}

std::string request_body_json(ProviderRequest const& request, bool include_max_output_tokens)
{
  std::string body = "{\"model\":\"" + ava::core::json::escape(request.model_id) + "\",\"instructions\":\"" + ava::core::json::escape(request.system_prompt) +
                     "\",\"stream\":" + (request.stream ? "true" : "false") + ",\"input\":[";
  for (std::size_t index = 0; index < request.messages.size(); ++index)
  {
    if (index > 0)
      body += ',';
    body += input_item_json(request.messages[index]);
  }
  body += ']';
  if (include_max_output_tokens && request.max_output_tokens && *request.max_output_tokens > 0)
  {
    body += ",\"max_output_tokens\":";
    body += std::to_string(*request.max_output_tokens);
  }
  body += reasoning_options_json(request);
  body += ",\"tools\":[";
  for (std::size_t index = 0; index < request.tools_json.size(); ++index)
  {
    if (index > 0)
      body += ',';
    body += request.tools_json[index];
  }
  body += "]}";
  return body;
}

void apply_codex_oauth_request_options(HttpRequest& request)
{
  request.url = std::string(kCodexResponsesUrl);
  request.headers["OpenAI-Beta"] = "responses=experimental";
  request.headers["originator"] = "ava";
  if (!request.body.empty() && request.body.back() == '}')
  {
    request.body.insert(request.body.size() - 1, ",\"store\":false");
  }
}

}  // namespace

ava::core::Result<HttpRequest> build_openai_responses_request(ProviderRequest const& request, std::string_view access_token, std::string_view base_url,
                                                              bool include_max_output_tokens)
{
  if (request.model_id.empty())
  {
    return std::unexpected(ava::core::Error(ava::core::ErrorCategory::InvalidArgument, "model id is required"));
  }
  if (access_token.empty())
  {
    return std::unexpected(ava::core::Error(ava::core::ErrorCategory::PermissionDenied, "OpenAI credential is required"));
  }
  if (auto valid_images = validate_image_payloads(request); !valid_images)
  {
    return std::unexpected(std::move(valid_images.error()));
  }
  if (auto valid_tools = validate_tools_json(request); !valid_tools)
  {
    return std::unexpected(std::move(valid_tools.error()));
  }
  if (auto valid_reasoning = validate_reasoning_options(request); !valid_reasoning)
  {
    return std::unexpected(std::move(valid_reasoning.error()));
  }

  return HttpRequest{
      .method = "POST",
      .url = std::string(base_url) + "/v1/responses",
      .headers = {{"Authorization", "Bearer " + std::string(access_token)}, {"Content-Type", "application/json"}, {"Accept", "text/event-stream"}},
      .body = request_body_json(request, include_max_output_tokens),
      .timeout_ms = 60000,
      .follow_redirects = true,
      .include_response_headers = false,
      .resolve_hosts = {},
  };
}

ava::core::VoidResult apply_openai_auth_options(HttpRequest& request, ProviderAuthContext const& auth)
{
  if (auth.credential_type != "oauth")
    return {};
  apply_codex_oauth_request_options(request);
  if (!auth.account_id.empty())
  {
    request.headers["ChatGPT-Account-Id"] = auth.account_id;
    request.headers["chatgpt-account-id"] = auth.account_id;
  }
  return {};
}

}  // namespace detail

ava::core::Result<HttpRequest> OpenAIProvider::build_request(ProviderRequest const& request, std::string_view access_token) const
{
  return detail::build_openai_responses_request(request, access_token, base_url_, true);
}

ava::core::Result<HttpRequest> OpenAIProvider::build_request(ProviderRequest const& request, ProviderAuthContext const& auth) const
{
  auto http_request = detail::build_openai_responses_request(request, auth.access_token, base_url_, auth.credential_type != "oauth");
  if (!http_request)
    return http_request;
  if (auto applied = apply_auth_options(*http_request, auth); !applied)
  {
    return std::unexpected(std::move(applied.error()));
  }
  return http_request;
}

ava::core::VoidResult OpenAIProvider::apply_auth_options(HttpRequest& request, ProviderAuthContext const& auth) const
{
  return detail::apply_openai_auth_options(request, auth);
}

ava::core::Result<HttpRequest> OpenAIProvider::build_request(ProviderRequest const& request, ava::config::OpenAICredential const& credential,
                                                             long long now_seconds) const
{
  auto access_token = ava::config::openai_access_token_for_request(credential, now_seconds);
  if (!access_token)
    return std::unexpected(std::move(access_token.error()));
  return build_request(request, ProviderAuthContext{.access_token = *access_token,
                                                    .credential_type = credential.type == ava::config::OpenAICredentialType::OAuth ? "oauth" : "api_key",
                                                    .account_id = credential.account_id});
}

ava::core::Result<HttpRequest> OpenAIProvider::build_request(ProviderRequest const& request, ava::config::OpenAICredential const& credential) const
{
  auto access_token = ava::config::openai_access_token_for_request(credential);
  if (!access_token)
    return std::unexpected(std::move(access_token.error()));
  return build_request(request, ProviderAuthContext{.access_token = *access_token,
                                                    .credential_type = credential.type == ava::config::OpenAICredentialType::OAuth ? "oauth" : "api_key",
                                                    .account_id = credential.account_id});
}

}  // namespace ava::provider
