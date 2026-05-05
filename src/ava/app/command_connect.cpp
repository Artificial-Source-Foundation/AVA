#include "ava/app/command_connect.h"

#include "ava/app/command_format.h"
#include "ava/app/connect_openai.h"

#include "ava/config/auth.h"
#include "ava/config/openai_oauth.h"
#include "ava/config/provider_profiles.h"

#include "ava/provider/curl_transport.h"

#include <algorithm>
#include <atomic>
#include <cctype>
#include <chrono>
#include <future>
#include <optional>

namespace ava::app {
namespace {

enum class ConnectMethod {
  ApiKey,
  OAuthToken,
  OpenAIBrowserOAuth,
  OpenAIHeadlessOAuth,
};

bool is_valid_connect_provider_id(std::string_view provider_id)
{
  if (provider_id.empty() || provider_id.size() > 128) return false;
  return std::ranges::all_of(provider_id, [](char ch) {
    auto const uch = static_cast<unsigned char>(ch);
    return std::isalnum(uch) != 0 || ch == '-' || ch == '_';
  });
}

std::string trim_secret_text(std::string secret)
{
  auto is_edge_space = [](char ch) { return ch == ' ' || ch == '\t' || ch == '\n' || ch == '\r'; };
  auto first = std::find_if_not(secret.begin(), secret.end(), is_edge_space);
  auto last = std::find_if_not(secret.rbegin(), secret.rend(), is_edge_space).base();
  if (first >= last) return {};
  return std::string(first, last);
}

std::string credential_type_value(std::string_view method)
{
  if (method == "api" || method == "api-key" || method == "apikey" || method == "key" || method == "api_key") {
    return "api_key";
  }
  if (method == "oauth" || method == "oauth-token" || method == "oauth_token" || method == "bearer" ||
      method == "token") {
    return "oauth";
  }
  return {};
}

std::string credential_type_value(ConnectMethod method)
{
  if (method == ConnectMethod::OAuthToken) return "oauth";
  return "api_key";
}

std::string credential_type_label(ConnectMethod method)
{
  return method == ConnectMethod::OAuthToken ? "OAuth bearer token" : "API key";
}

std::string connect_method_label(ConnectMethod method)
{
  switch (method) {
    case ConnectMethod::ApiKey:
      return "API key";
    case ConnectMethod::OAuthToken:
      return "OAuth bearer token";
    case ConnectMethod::OpenAIBrowserOAuth:
      return "ChatGPT Pro/Plus browser OAuth";
    case ConnectMethod::OpenAIHeadlessOAuth:
      return "ChatGPT Pro/Plus headless OAuth";
  }
  return "credential";
}

std::string lower_ascii(std::string_view text)
{
  std::string lowered;
  lowered.reserve(text.size());
  for (char const ch : text) lowered.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(ch))));
  return lowered;
}

std::optional<ConnectMethod> parse_connect_method(std::string_view value, std::string_view provider_id)
{
  auto const lowered = lower_ascii(value);
  if (provider_id == "openai") {
    if (lowered == "browser" || lowered == "browser-oauth" || lowered == "browser_oauth" || lowered == "chatgpt" ||
        lowered == "oauth") {
      return ConnectMethod::OpenAIBrowserOAuth;
    }
    if (lowered == "headless" || lowered == "headless-oauth" || lowered == "headless_oauth" || lowered == "device" ||
        lowered == "device-oauth" || lowered == "device_oauth") {
      return ConnectMethod::OpenAIHeadlessOAuth;
    }
    if (lowered == "oauth-token" || lowered == "oauth_token" || lowered == "bearer" || lowered == "token") {
      return ConnectMethod::OAuthToken;
    }
  }

  auto const credential_type = credential_type_value(value);
  if (credential_type == "api_key") return ConnectMethod::ApiKey;
  if (credential_type == "oauth") return ConnectMethod::OAuthToken;
  return std::nullopt;
}

long long unix_time_seconds()
{
  return std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch()).count();
}

std::string selected_or_custom_answer(ava::agent::QuestionAnswer const& answer)
{
  if (!answer.custom_text.empty()) return answer.custom_text;
  if (!answer.selected_options.empty()) return answer.selected_options.front();
  return {};
}

ava::core::Result<ava::agent::QuestionAnswer> ask_connect_question(CommandRequest const& request,
                                                                   ava::agent::QuestionPrompt prompt)
{
  if (!request.question_resolver) {
    return std::unexpected(
        ava::core::Error(ava::core::ErrorCategory::InvalidArgument,
                         "/connect requires the interactive TUI; use `ava connect <provider> --api-key-stdin`, "
                         "`--api-key-env ENV`, `--oauth-token-stdin`, `--oauth-token-env ENV`, or `ava connect "
                         "openai --headless-oauth` for headless setup"));
  }
  return request.question_resolver(prompt);
}

std::vector<ava::agent::QuestionOption> provider_options(RuntimeSession const& session)
{
  std::vector<ava::agent::QuestionOption> options;
  auto add = [&](std::string value, std::string label) {
    if (std::ranges::any_of(options, [&](auto const& option) { return option.value == value; })) return;
    options.push_back(ava::agent::QuestionOption{.value = std::move(value), .label = std::move(label)});
  };
  if (!session.model.provider_id.empty()) {
    add(session.model.provider_id, ava::config::provider_display_name(session.model.provider_id) + " (current)");
  }
  for (auto const& profile : ava::config::builtin_provider_profiles()) {
    auto label = profile.display_name;
    if (!profile.connect_detail.empty()) label += " - " + profile.connect_detail;
    add(profile.provider_id, std::move(label));
  }
  return options;
}

ava::core::Result<std::string> resolve_connect_provider(RuntimeSession& session, CommandRequest const& request,
                                                        std::vector<std::string> const& args)
{
  if (!args.empty()) return args[0];
  auto answer = ask_connect_question(request, ava::agent::QuestionPrompt{.header = "Connect a provider",
                                                                         .question = "Select provider",
                                                                         .options = provider_options(session),
                                                                         .multiple = false,
                                                                         .allow_custom = true,
                                                                         .secret = false,
                                                                         .modal = true,
                                                                         .searchable = true});
  if (!answer) return std::unexpected(std::move(answer.error()));
  return selected_or_custom_answer(*answer);
}

std::vector<ava::agent::QuestionOption> method_options_for_provider(std::string_view provider_id)
{
  if (provider_id == "openai") {
    return {
        ava::agent::QuestionOption{.value = "openai_browser_oauth", .label = "B ChatGPT Pro/Plus (browser OAuth)"},
        ava::agent::QuestionOption{.value = "openai_headless_oauth", .label = "H ChatGPT Pro/Plus (headless OAuth)"},
        ava::agent::QuestionOption{.value = "api_key", .label = "A OpenAI API key"},
        ava::agent::QuestionOption{.value = "oauth", .label = "T OAuth bearer token"}};
  }
  return {ava::agent::QuestionOption{.value = "api_key", .label = "A API key"},
          ava::agent::QuestionOption{.value = "oauth", .label = "T OAuth bearer token"}};
}

ava::core::Result<ConnectMethod> resolve_connect_method(CommandRequest const& request,
                                                        std::vector<std::string> const& args,
                                                        std::string_view provider_id)
{
  if (args.size() >= 2) {
    auto method = parse_connect_method(args[1], provider_id);
    if (method) return *method;
    auto error = ava::core::Error(ava::core::ErrorCategory::InvalidArgument,
                                  "/connect credential type must be api-key, oauth, browser-oauth, or headless-oauth");
    error.with_context("usage", "/connect [provider] [api-key|oauth|browser-oauth|headless-oauth]");
    return std::unexpected(std::move(error));
  }

  auto answer = ask_connect_question(
      request, ava::agent::QuestionPrompt{.header = provider_id == "openai" ? "Connect OpenAI" : "Connect a provider",
                                          .question = "Choose login method",
                                          .options = method_options_for_provider(provider_id),
                                          .multiple = false,
                                          .allow_custom = false,
                                          .secret = false,
                                          .modal = true});
  if (!answer) return std::unexpected(std::move(answer.error()));
  auto const selected = selected_or_custom_answer(*answer);
  if (selected == "openai_browser_oauth") return ConnectMethod::OpenAIBrowserOAuth;
  if (selected == "openai_headless_oauth") return ConnectMethod::OpenAIHeadlessOAuth;
  if (selected == "api_key") return ConnectMethod::ApiKey;
  if (selected == "oauth") return ConnectMethod::OAuthToken;
  return std::unexpected(ava::core::Error(ava::core::ErrorCategory::InvalidArgument, "unknown connect login method"));
}

ava::core::Result<std::string> prompt_connect_secret(CommandRequest const& request, std::string_view provider_id,
                                                     ConnectMethod method)
{
  auto answer =
      ask_connect_question(request, ava::agent::QuestionPrompt{.header = "Connect a provider",
                                                               .question = "Paste " + credential_type_label(method) +
                                                                           " for " + std::string(provider_id),
                                                               .options = {},
                                                               .multiple = false,
                                                               .allow_custom = true,
                                                               .secret = true,
                                                               .modal = true});
  if (!answer) return std::unexpected(std::move(answer.error()));
  auto secret = trim_secret_text(selected_or_custom_answer(*answer));
  if (secret.empty()) {
    return std::unexpected(ava::core::Error(ava::core::ErrorCategory::InvalidArgument, "credential was empty"));
  }
  return secret;
}

ava::core::VoidResult prompt_continue(CommandRequest const& request, std::string header, std::string question)
{
  auto answer = ask_connect_question(
      request,
      ava::agent::QuestionPrompt{.header = std::move(header),
                                 .question = std::move(question),
                                 .options = {ava::agent::QuestionOption{.value = "continue", .label = "C Continue"}},
                                 .multiple = false,
                                 .allow_custom = false,
                                 .secret = false,
                                 .modal = true});
  if (!answer) return std::unexpected(std::move(answer.error()));
  return {};
}

ava::core::VoidResult store_connect_credential(RuntimeSession const& session, std::string_view provider_id,
                                               ConnectMethod method, std::string secret)
{
  if (provider_id == "openai") {
    auto credential = ava::config::OpenAICredential{.type = method == ConnectMethod::OAuthToken
                                                                ? ava::config::OpenAICredentialType::OAuth
                                                                : ava::config::OpenAICredentialType::ApiKey,
                                                    .access_token = std::move(secret),
                                                    .refresh_token = "",
                                                    .expires_at = 0,
                                                    .account_id = "",
                                                    .source_path = {}};
    if (credential.type == ava::config::OpenAICredentialType::OAuth) {
      credential.account_id = ava::config::openai_oauth_account_id_from_token(credential.access_token).value_or("");
    }
    return ava::config::store_openai_credential(session.paths, credential);
  }

  return ava::config::store_provider_credential(
      session.paths, ava::config::ProviderCredential{.provider_id = std::string(provider_id),
                                                     .access_token = std::move(secret),
                                                     .credential_type = credential_type_value(method),
                                                     .account_id = "",
                                                     .source = "connect"});
}

ava::core::Result<std::string> store_openai_oauth_result(RuntimeSession const& session,
                                                         ava::config::OpenAICredential const& credential)
{
  auto stored = ava::config::store_openai_credential(session.paths, credential);
  if (!stored) return std::unexpected(std::move(stored.error()));
  return "Stored OpenAI OAuth credential at " + session.paths.auth_file.string();
}

ava::core::Result<std::string> run_openai_browser_oauth(RuntimeSession const& session, CommandRequest const& request)
{
  auto oauth_session = ava::config::make_openai_oauth_session();
  if (!oauth_session) return std::unexpected(std::move(oauth_session.error()));

  ava::provider::CurlCliTransport transport;
  std::atomic_bool prompt_cancelled{false};
  auto cancel_requested = [&]() {
    return prompt_cancelled.load() || (request.cancel_requested && request.cancel_requested());
  };
  auto credential_future = std::async(std::launch::async, [&]() {
    return complete_openai_browser_oauth(*oauth_session, transport, unix_time_seconds(), cancel_requested);
  });

  auto prompt = prompt_continue(request, "Connect OpenAI",
                                "Open this URL to connect AVA to OpenAI:\n" + oauth_session->authorization_url +
                                    "\n\nAVA is listening on http://localhost:1455/auth/callback.");
  if (!prompt) {
    prompt_cancelled.store(true);
    static_cast<void>(credential_future.get());
    return std::unexpected(std::move(prompt.error()));
  }
  auto credential = credential_future.get();
  if (!credential) return std::unexpected(std::move(credential.error()));
  return store_openai_oauth_result(session, *credential);
}

ava::core::Result<std::string> run_openai_headless_oauth(RuntimeSession const& session, CommandRequest const& request)
{
  ava::provider::CurlCliTransport transport;
  auto authorization = ava::config::start_openai_oauth_device_authorization(transport);
  if (!authorization) return std::unexpected(std::move(authorization.error()));

  std::atomic_bool prompt_cancelled{false};
  auto cancel_requested = [&]() {
    return prompt_cancelled.load() || (request.cancel_requested && request.cancel_requested());
  };
  auto credential_future = std::async(std::launch::async, [&]() {
    return wait_for_openai_device_oauth(*authorization, transport, unix_time_seconds(), cancel_requested);
  });

  auto prompt = prompt_continue(request, "Connect OpenAI",
                                "Open this URL on any device:\n" + authorization->verification_url +
                                    "\n\nEnter code: " + authorization->user_code +
                                    "\n\nAVA will poll until OpenAI confirms authorization.");
  if (!prompt) {
    prompt_cancelled.store(true);
    static_cast<void>(credential_future.get());
    return std::unexpected(std::move(prompt.error()));
  }
  auto credential = credential_future.get();
  if (!credential) return std::unexpected(std::move(credential.error()));
  return store_openai_oauth_result(session, *credential);
}

}  // namespace

ava::core::Result<CommandResult> run_connect_command(RuntimeSession& session, CommandRequest const& request)
{
  CommandResult result;
  result.handled = true;
  auto const args = split_command_arguments(command_argument(request.command, "/connect"));
  if (args.size() > 2) {
    add_output(result, missing_argument("/connect [provider] [api-key|oauth|browser-oauth|headless-oauth]"));
    return result;
  }

  auto provider_id = resolve_connect_provider(session, request, args);
  if (!provider_id) {
    add_output(result, provider_id.error().format());
    return result;
  }
  if (!is_valid_connect_provider_id(*provider_id)) {
    add_output(result, "provider id must contain only letters, numbers, '-' or '_'");
    return result;
  }

  auto method = resolve_connect_method(request, args, *provider_id);
  if (!method) {
    add_output(result, method.error().format());
    return result;
  }

  if (*method == ConnectMethod::OpenAIBrowserOAuth) {
    auto stored = run_openai_browser_oauth(session, request);
    add_output(result, stored ? *stored : stored.error().format());
    return result;
  }
  if (*method == ConnectMethod::OpenAIHeadlessOAuth) {
    auto stored = run_openai_headless_oauth(session, request);
    add_output(result, stored ? *stored : stored.error().format());
    return result;
  }

  auto secret = prompt_connect_secret(request, *provider_id, *method);
  if (!secret) {
    add_output(result, secret.error().format());
    return result;
  }

  auto stored = store_connect_credential(session, *provider_id, *method, *secret);
  if (!stored) {
    add_output(result, stored.error().format());
    return result;
  }
  add_output(result, "Stored " + *provider_id + " " + connect_method_label(*method) + " credential at " +
                         session.paths.auth_file.string());
  return result;
}

}  // namespace ava::app
