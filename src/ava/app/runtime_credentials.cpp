#include "sys.h"
#include "ava/http/curl_transport.h"
#include "ava/app/runtime/Session.h"
#include "ava/app/runtime_credentials.h"
#include "ava/config/auth.h"
#include "ava/config/openai_oauth.h"
#include "ava/provider/catalog.h"
#include "ava/provider/registry.h"

#include <memory>
#include <string>
#include <utility>

namespace ava::app {

ava::core::Result<runtime::RunOptions> prepare_runtime_credentials(ava::config::XdgPaths const& paths, std::string_view provider_id,
                                                                   runtime::RunOptions options, ava::http::Transport& auth_transport, std::string_view purpose)
{
  if (options.offline)
    return std::unexpected(offline_provider_error(purpose));
  if (!options.access_token.empty())
    return options;

  auto credential = ava::config::provider_credential_for_request(paths, provider_id, auth_transport);
  if (!credential)
    return std::unexpected(std::move(credential.error()));
  if (!*credential)
  {
    auto error =
        ava::core::Error(ava::core::ErrorCategory::InvalidArgument, std::string(purpose) + " requires auth for provider `" + std::string(provider_id) + "`");
    error.with_context("provider", std::string(provider_id));
    error.with_context("auth_file", paths.auth_file.string());
    error.with_context("hint", "configure a provider API key environment variable or run `ava connect " + std::string(provider_id) + "`");
    return std::unexpected(std::move(error));
  }

  options.access_token = (*credential)->access_token;
  options.credential_type = (*credential)->credential_type;
  options.openai_oauth = (*credential)->provider_id == "openai" && (*credential)->credential_type == "oauth";
  options.openai_account_id = (*credential)->account_id;
  if (options.openai_oauth && options.openai_account_id.empty())
    options.openai_account_id = ava::config::openai_oauth_account_id_from_token((*credential)->access_token).value_or("");
  return options;
}

ava::core::Result<RuntimeProviderRunBundle> create_runtime_provider_run_bundle(runtime::Session const& session, runtime::RunOptions options,
                                                                               std::string_view purpose)
{
  auto auth_transport = std::make_unique<ava::http::CurlCliTransport>();
  auto prepared = prepare_runtime_credentials(session.paths(), session.model().provider_id, std::move(options), *auth_transport, purpose);
  if (!prepared)
    return std::unexpected(std::move(prepared.error()));

  auto catalog = session.provider_catalog();
  if (!catalog)
  {
    auto built = ava::provider::ensure_provider_catalog(nullptr, session.paths());
    if (!built)
      return std::unexpected(std::move(built.error()));
    catalog = std::move(*built);
  }
  auto provider = catalog->create(session.model().provider_id);
  if (!provider)
    return std::unexpected(std::move(provider.error()));

  std::unique_ptr<ava::http::Transport> transport = std::make_unique<ava::http::CurlCliTransport>();
  return RuntimeProviderRunBundle{
      .provider = std::move(*provider), .transport = std::move(transport), .auth_transport = std::move(auth_transport), .options = std::move(*prepared)};
}

}  // namespace ava::app
