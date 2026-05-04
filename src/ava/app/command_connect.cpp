#include "ava/app/command_connect.h"

#include <algorithm>
#include <cctype>

#include "ava/app/command_format.h"
#include "ava/config/auth.h"
#include "ava/config/provider_profiles.h"

namespace ava::app {
namespace {

bool is_valid_connect_provider_id(std::string_view provider_id) {
  if (provider_id.empty() || provider_id.size() > 128) return false;
  return std::ranges::all_of(provider_id, [](char ch) {
    auto const uch = static_cast<unsigned char>(ch);
    return std::isalnum(uch) != 0 || ch == '-' || ch == '_';
  });
}

std::string trim_secret_text(std::string secret) {
  auto is_edge_space = [](char ch) { return ch == ' ' || ch == '\t' || ch == '\n' || ch == '\r'; };
  auto first = std::find_if_not(secret.begin(), secret.end(), is_edge_space);
  auto last = std::find_if_not(secret.rbegin(), secret.rend(), is_edge_space).base();
  if (first >= last) return {};
  return std::string(first, last);
}

std::string credential_type_value(std::string_view method) {
  if (method == "api" || method == "api-key" || method == "apikey" || method == "key" || method == "api_key") {
    return "api_key";
  }
  if (method == "oauth" || method == "oauth-token" || method == "oauth_token" || method == "bearer" ||
      method == "token") {
    return "oauth";
  }
  return {};
}

std::string credential_type_label(std::string_view credential_type) {
  return credential_type == "oauth" ? "OAuth bearer token" : "API key";
}

std::string selected_or_custom_answer(ava::agent::QuestionAnswer const& answer) {
  if (!answer.custom_text.empty()) return answer.custom_text;
  if (!answer.selected_options.empty()) return answer.selected_options.front();
  return {};
}

ava::core::Result<ava::agent::QuestionAnswer> ask_connect_question(CommandRequest const& request,
                                                                   ava::agent::QuestionPrompt prompt) {
  if (!request.question_resolver) {
    return std::unexpected(
        ava::core::Error(ava::core::ErrorCategory::InvalidArgument,
                         "/connect requires the interactive TUI; use `ava connect <provider> --api-key-stdin`, "
                         "`--api-key-env ENV`, `--oauth-token-stdin`, or `--oauth-token-env ENV` for headless setup"));
  }
  return request.question_resolver(prompt);
}

std::vector<ava::agent::QuestionOption> provider_options(RuntimeSession const& session) {
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
                                                        std::vector<std::string> const& args) {
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

ava::core::Result<std::string> resolve_connect_credential_type(CommandRequest const& request,
                                                               std::vector<std::string> const& args) {
  if (args.size() >= 2) {
    auto const credential_type = credential_type_value(args[1]);
    if (!credential_type.empty()) return credential_type;
    auto error = ava::core::Error(ava::core::ErrorCategory::InvalidArgument,
                                  "/connect credential type must be api-key or oauth");
    error.with_context("usage", "/connect [provider] [api-key|oauth]");
    return std::unexpected(std::move(error));
  }

  auto answer = ask_connect_question(
      request, ava::agent::QuestionPrompt{
                   .header = "Connect a provider",
                   .question = "Choose credential type",
                   .options = {ava::agent::QuestionOption{.value = "api_key", .label = "API key"},
                               ava::agent::QuestionOption{.value = "oauth", .label = "OAuth bearer token"}},
                   .multiple = false,
                   .allow_custom = false,
                   .secret = false,
                   .modal = true});
  if (!answer) return std::unexpected(std::move(answer.error()));
  return selected_or_custom_answer(*answer);
}

ava::core::Result<std::string> prompt_connect_secret(CommandRequest const& request, std::string_view provider_id,
                                                     std::string_view credential_type) {
  auto answer = ask_connect_question(
      request, ava::agent::QuestionPrompt{
                   .header = "Connect a provider",
                   .question = "Paste " + credential_type_label(credential_type) + " for " + std::string(provider_id),
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

}  // namespace

ava::core::Result<CommandResult> run_connect_command(RuntimeSession& session, CommandRequest const& request) {
  CommandResult result;
  result.handled = true;
  auto const args = split_command_arguments(command_argument(request.command, "/connect"));
  if (args.size() > 2) {
    add_output(result, missing_argument("/connect [provider] [api-key|oauth]"));
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

  auto credential_type = resolve_connect_credential_type(request, args);
  if (!credential_type) {
    add_output(result, credential_type.error().format());
    return result;
  }

  auto secret = prompt_connect_secret(request, *provider_id, *credential_type);
  if (!secret) {
    add_output(result, secret.error().format());
    return result;
  }

  auto stored = ava::config::store_provider_credential(
      session.paths, ava::config::ProviderCredential{.provider_id = *provider_id,
                                                     .access_token = *secret,
                                                     .credential_type = *credential_type,
                                                     .account_id = "",
                                                     .source = "connect"});
  if (!stored) {
    add_output(result, stored.error().format());
    return result;
  }
  add_output(result, "Stored " + *provider_id + " " + credential_type_label(*credential_type) + " credential at " +
                         session.paths.auth_file.string());
  return result;
}

}  // namespace ava::app
