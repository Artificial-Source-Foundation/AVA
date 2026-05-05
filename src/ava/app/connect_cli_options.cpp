#include "ava/app/connect_cli_options.h"

#include <iostream>
#include <utility>

namespace ava::app::detail {
namespace {

ava::core::Error connect_cli_error(std::string message)
{
  return ava::core::Error(ava::core::ErrorCategory::InvalidArgument, std::move(message));
}

}  // namespace

ava::core::Result<ConnectCliInvocation> parse_connect_cli_invocation(std::vector<std::string_view> const& args,
                                                                     std::size_t first_arg_index)
{
  ConnectCliInvocation invocation;
  auto index = first_arg_index;
  if (index < args.size() && !args[index].starts_with("--")) {
    invocation.provider_id = std::string(args[index]);
    ++index;
  }

  auto set_source = [&](ConnectCliCredentialSource source,
                        ava::app::ConnectCredentialType credential_type) -> ava::core::VoidResult {
    if (invocation.credential_source != ConnectCliCredentialSource::None) {
      return std::unexpected(connect_cli_error("connect accepts only one credential source"));
    }
    invocation.credential_source = source;
    invocation.credential_type = credential_type;
    return {};
  };

  while (index < args.size()) {
    auto const option = args[index++];
    if (option == "--api-key") {
      if (auto set = set_source(ConnectCliCredentialSource::Prompt, ava::app::ConnectCredentialType::ApiKey); !set) {
        return std::unexpected(std::move(set.error()));
      }
      continue;
    }
    if (option == "--oauth-token") {
      if (auto set = set_source(ConnectCliCredentialSource::Prompt, ava::app::ConnectCredentialType::OAuthToken);
          !set) {
        return std::unexpected(std::move(set.error()));
      }
      continue;
    }
    if (option == "--api-key-stdin") {
      if (auto set = set_source(ConnectCliCredentialSource::Stdin, ava::app::ConnectCredentialType::ApiKey); !set) {
        return std::unexpected(std::move(set.error()));
      }
      continue;
    }
    if (option == "--oauth-token-stdin") {
      if (auto set = set_source(ConnectCliCredentialSource::Stdin, ava::app::ConnectCredentialType::OAuthToken); !set) {
        return std::unexpected(std::move(set.error()));
      }
      continue;
    }
    if (option == "--api-key-env" || option == "--oauth-token-env") {
      auto const credential_type = option == "--oauth-token-env" ? ava::app::ConnectCredentialType::OAuthToken
                                                                 : ava::app::ConnectCredentialType::ApiKey;
      if (auto set = set_source(ConnectCliCredentialSource::Env, credential_type); !set) {
        return std::unexpected(std::move(set.error()));
      }
      if (index >= args.size()) {
        return std::unexpected(connect_cli_error(std::string(option) + " requires an environment variable name"));
      }
      invocation.env_var = std::string(args[index++]);
      continue;
    }
    return std::unexpected(connect_cli_error("unknown connect option"));
  }

  return invocation;
}

int run_connect_cli_invocation(ava::config::XdgPaths const& paths, ConnectCliInvocation const& invocation,
                               bool preserve_openai_browser_default, bool stdin_is_tty, std::istream& in,
                               std::ostream& out, std::ostream& err)
{
  if (invocation.credential_source == ConnectCliCredentialSource::Stdin ||
      invocation.credential_source == ConnectCliCredentialSource::Env) {
    if (!invocation.provider_id) {
      err << "connect requires a provider with headless credential sources\n";
      return 2;
    }
    return run_connect_provider_credential(
        paths,
        ava::app::ConnectProviderCredentialOptions{.provider_id = *invocation.provider_id,
                                                   .credential_type = invocation.credential_type.value(),
                                                   .env_var = invocation.env_var},
        in, out, err);
  }

  if (invocation.credential_source == ConnectCliCredentialSource::Prompt) {
    return run_connect_provider_wizard(
        paths,
        ava::app::ConnectProviderWizardOptions{.provider_id = invocation.provider_id,
                                               .credential_type = invocation.credential_type,
                                               .stdin_is_tty = stdin_is_tty},
        in, out, err);
  }

  if (preserve_openai_browser_default && invocation.provider_id && *invocation.provider_id == "openai") {
    return run_connect_openai(paths);
  }

  return run_connect_provider_wizard(
      paths,
      ava::app::ConnectProviderWizardOptions{.provider_id = invocation.provider_id, .stdin_is_tty = stdin_is_tty}, in,
      out, err);
}

}  // namespace ava::app::detail
