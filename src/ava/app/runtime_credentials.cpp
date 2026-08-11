#include "sys.h"
#include "ava/http/curl_transport.h"
#include "ava/app/runtime/Session.h"
#include "ava/app/runtime_credentials.h"
#include "ava/config/auth.h"
#include "ava/config/openai_oauth.h"
#include "ava/provider/catalog.h"

#include <memory>
#include <string>
#include <utility>

namespace ava::app {
namespace {

ava::config::ProviderCredentialPolicy credential_policy_for(std::shared_ptr<ava::provider::ProviderCatalog const> const& catalog, std::string_view provider_id)
{
  ava::config::ProviderCredentialPolicy policy;
  if (!catalog)
    return policy;
  policy.auth_none = catalog->provider_auth_is_none(provider_id);
  policy.user_defined = catalog->provider_is_user_defined(provider_id);
  policy.api_key_env = catalog->provider_api_key_env(provider_id);
  return policy;
}

}  // namespace

ava::core::Result<runtime::RunOptions> prepare_runtime_credentials(ava::config::XdgPaths const& paths, std::string_view provider_id,
                                                                   runtime::RunOptions options, ava::http::Transport& auth_transport, std::string_view purpose,
                                                                   std::shared_ptr<ava::provider::ProviderCatalog const> catalog)
{
  if (options.offline)
    return std::unexpected(offline_provider_error(purpose));
  if (options.credential_type == "none")
  {
    options.access_token.clear();
    options.openai_oauth = false;
    options.openai_account_id.clear();
    return options;
  }
  if (!options.access_token.empty())
    return options;

  auto policy = credential_policy_for(catalog, provider_id);
  auto credential = ava::config::provider_credential_for_request(paths, provider_id, auth_transport, policy);
  if (!credential)
    return std::unexpected(std::move(credential.error()));
  if (!*credential)
  {
    auto error =
        ava::core::Error(ava::core::ErrorCategory::InvalidArgument, std::string(purpose) + " requires auth for provider `" + std::string(provider_id) + "`");
    error.with_context("provider", std::string(provider_id));
    error.with_context("auth_file", paths.auth_file.string());
    if (policy.user_defined && !policy.api_key_env.empty())
      error.with_context("api_key_env", policy.api_key_env);
    error.with_context("hint", "configure a provider API key environment variable or run `ava connect " + std::string(provider_id) + "`");
    return std::unexpected(std::move(error));
  }

  options.access_token = (*credential)->access_token;
  options.credential_type = (*credential)->credential_type;
  options.openai_oauth = (*credential)->provider_id == "openai" && (*credential)->credential_type == "oauth";
  options.openai_account_id = (*credential)->account_id;
  if (options.credential_type == "none")
  {
    options.access_token.clear();
    options.openai_oauth = false;
    options.openai_account_id.clear();
  }
  if (options.openai_oauth && options.openai_account_id.empty())
    options.openai_account_id = ava::config::openai_oauth_account_id_from_token((*credential)->access_token).value_or("");
  return options;
}

ava::core::Result<runtime::RunOptions> prepare_runtime_credentials(ava::config::XdgPaths const& paths, std::string_view provider_id,
                                                                   runtime::RunOptions options, ava::http::Transport& auth_transport, std::string_view purpose)
{
  return prepare_runtime_credentials(paths, provider_id, std::move(options), auth_transport, purpose, nullptr);
}

ava::core::Result<RuntimeProviderRunBundle> create_runtime_provider_run_bundle(runtime::session_ts const& unlocked_session, runtime::RunOptions options,
                                                                               std::string_view purpose)
{
  auto auth_transport = std::make_unique<ava::http::CurlCliTransport>();
  CRITICAL_AREA_BEGIN_CR(session);
  auto catalog = session_r->provider_catalog();
  if (!catalog)
  {
    auto built = ava::provider::ensure_provider_catalog(nullptr, session_r->paths());
    if (!built)
      return std::unexpected(std::move(built.error()));
    catalog = std::move(*built);
  }
  auto prepared = prepare_runtime_credentials(session_r->paths(), session_r->model().provider_id, std::move(options), *auth_transport, purpose, catalog);
  if (!prepared)
    return std::unexpected(std::move(prepared.error()));

  auto provider = catalog->create(session_r->model().provider_id);
  if (!provider)
    return std::unexpected(std::move(provider.error()));
  CRITICAL_AREA_END_R(session);

  std::unique_ptr<ava::http::Transport> transport = std::make_unique<ava::http::CurlCliTransport>();
  return RuntimeProviderRunBundle{
      .provider = std::move(*provider), .transport = std::move(transport), .auth_transport = std::move(auth_transport), .options = std::move(*prepared)};
}

}  // namespace ava::app
