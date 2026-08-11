#include "sys.h"
#include "ava/config/builtin_generic_providers.h"
#include "ava/config/provider_profiles.h"
#include "ava/provider/anthropic_provider.h"
#include "ava/provider/gemini_provider.h"
#include "ava/provider/openai_compatible_provider.h"
#include "ava/provider/openai_provider.h"
#include "ava/provider/registry.h"

#include <cstdlib>
#include <string>
#include <utility>

namespace ava::provider {
namespace {

std::string env_or_default(char const* name, std::string fallback)
{
  char const* value = std::getenv(name);
  if (value == nullptr || std::string_view(value).empty())
    return fallback;
  return value;
}

void register_legacy_builtin_providers(ProviderRegistry& registry)
{
  // Legacy built-ins retain historical create-time env base URL lookups. That
  // path is outside AVA-PROV-001 (scoped to the six declarative generic
  // factories) and is intentionally unchanged here.
  static_cast<void>(registry.register_provider(ava::config::anthropic_provider_profile().provider_id, [] { return std::make_unique<AnthropicProvider>(); }));
  static_cast<void>(registry.register_provider(ava::config::deepseek_provider_profile().provider_id, [] {
    auto const& profile = ava::config::deepseek_provider_profile();
    return std::make_unique<OpenAICompatibleProvider>(
        OpenAICompatibleProviderOptions{.base_url = env_or_default(profile.default_base_url_env.c_str(), profile.default_base_url),
                                        .chat_completions_path = profile.chat_completions_path,
                                        .provider_name = profile.display_name,
                                        .reasoning_format = profile.default_reasoning_format,
                                        .reasoning_request_field = profile.reasoning_request_field,
                                        .reasoning_request_effort_string = profile.reasoning_request_effort_string,
                                        .include_stream_usage = profile.include_stream_usage});
  }));
  static_cast<void>(registry.register_provider(ava::config::kimi_provider_profile().provider_id, [] {
    auto const& profile = ava::config::kimi_provider_profile();
    return std::make_unique<OpenAICompatibleProvider>(
        OpenAICompatibleProviderOptions{.base_url = env_or_default(profile.default_base_url_env.c_str(), profile.default_base_url),
                                        .chat_completions_path = profile.chat_completions_path,
                                        .provider_name = profile.display_name,
                                        .reasoning_format = profile.default_reasoning_format,
                                        .user_agent = profile.user_agent,
                                        .default_temperature = profile.default_temperature,
                                        .reasoning_request_field = profile.reasoning_request_field,
                                        .reasoning_request_effort_string = profile.reasoning_request_effort_string,
                                        .preserve_reasoning_content = profile.preserve_reasoning_content,
                                        .include_stream_usage = profile.include_stream_usage});
  }));
  static_cast<void>(registry.register_provider(ava::config::moonshot_provider_profile().provider_id, [] {
    auto const& profile = ava::config::moonshot_provider_profile();
    return std::make_unique<OpenAICompatibleProvider>(
        OpenAICompatibleProviderOptions{.base_url = env_or_default(profile.default_base_url_env.c_str(), profile.default_base_url),
                                        .chat_completions_path = profile.chat_completions_path,
                                        .provider_name = profile.display_name,
                                        .reasoning_format = profile.default_reasoning_format,
                                        .reasoning_request_field = profile.reasoning_request_field,
                                        .reasoning_request_effort_string = profile.reasoning_request_effort_string,
                                        .include_stream_usage = profile.include_stream_usage});
  }));
  static_cast<void>(registry.register_provider(ava::config::gemini_provider_profile().provider_id, [] {
    auto const& profile = ava::config::gemini_provider_profile();
    return std::make_unique<GeminiProvider>(env_or_default(profile.default_base_url_env.c_str(), profile.default_base_url));
  }));
  static_cast<void>(registry.register_provider(ava::config::openai_provider_profile().provider_id, [] { return std::make_unique<OpenAIProvider>(); }));
  static_cast<void>(registry.register_provider(ava::config::openrouter_provider_profile().provider_id, [] {
    auto const& profile = ava::config::openrouter_provider_profile();
    return std::make_unique<OpenAICompatibleProvider>(
        OpenAICompatibleProviderOptions{.base_url = env_or_default(profile.default_base_url_env.c_str(), profile.default_base_url),
                                        .chat_completions_path = profile.chat_completions_path,
                                        .provider_name = profile.display_name,
                                        .reasoning_format = profile.default_reasoning_format,
                                        .reasoning_request_field = profile.reasoning_request_field,
                                        .reasoning_request_effort_string = profile.reasoning_request_effort_string,
                                        .include_stream_usage = profile.include_stream_usage});
  }));
  auto register_zai_compatible = [&registry](ava::config::ProviderProfile const& (*profile_fn)()) {
    static_cast<void>(registry.register_provider(profile_fn().provider_id, [profile_fn] {
      auto const& profile = profile_fn();
      return std::make_unique<OpenAICompatibleProvider>(
          OpenAICompatibleProviderOptions{.base_url = env_or_default(profile.default_base_url_env.c_str(), profile.default_base_url),
                                          .chat_completions_path = profile.chat_completions_path,
                                          .provider_name = profile.display_name,
                                          .reasoning_format = profile.default_reasoning_format,
                                          .reasoning_request_field = profile.reasoning_request_field,
                                          .reasoning_request_effort_string = profile.reasoning_request_effort_string,
                                          .preserve_reasoning_content = profile.preserve_reasoning_content,
                                          .include_stream_usage = profile.include_stream_usage});
    }));
  };
  register_zai_compatible(ava::config::zai_provider_profile);
  register_zai_compatible(ava::config::zai_coding_cn_provider_profile);
}

ava::core::VoidResult register_resolved_builtin_generic_providers(ProviderRegistry& registry,
                                                                  std::vector<ava::config::ResolvedBuiltinGenericProvider> const& resolved)
{
  for (auto const& item : resolved)
  {
    auto registered = registry.register_provider(item.provider_id, [item]() -> std::unique_ptr<Provider> {
      // Capture the catalog-time canonical base/endpoint. Never getenv here.
      if (item.protocol == ava::config::BuiltinGenericProtocol::OpenAIResponses)
      {
        return std::make_unique<OpenAIProvider>(OpenAIProviderOptions{
            .base_url = item.base_url,
            .endpoint = item.endpoint,
            .follow_redirects = false,
            .require_credential = true,
            .send_authorization_bearer = true,
            .enable_codex_oauth_mutations = false,
            .force_include_max_output_tokens = true,
        });
      }
      return std::make_unique<OpenAICompatibleProvider>(OpenAICompatibleProviderOptions{
          .base_url = item.base_url,
          .chat_completions_path = item.request_path,
          .endpoint = item.endpoint,
          .provider_name = item.display_name,
          .include_stream_usage = item.include_stream_usage,
          .follow_redirects = false,
          .require_credential = true,
          .send_authorization_bearer = true,
      });
    });
    if (!registered)
      return std::unexpected(std::move(registered.error()));
  }
  return {};
}

}  // namespace

ava::core::VoidResult ProviderRegistry::register_provider(std::string provider_id, Factory factory)
{
  if (provider_id.empty())
  {
    return std::unexpected(ava::core::Error(ava::core::ErrorCategory::InvalidArgument, "provider id is required"));
  }
  if (!factory)
  {
    auto error = ava::core::Error(ava::core::ErrorCategory::InvalidArgument, "provider factory is required");
    error.with_context("provider", provider_id);
    return std::unexpected(std::move(error));
  }
  if (contains(provider_id))
  {
    auto error = ava::core::Error(ava::core::ErrorCategory::InvalidArgument, "provider is already registered");
    error.with_context("provider", provider_id);
    return std::unexpected(std::move(error));
  }
  providers_.push_back(std::make_pair(std::move(provider_id), std::move(factory)));
  return {};
}

bool ProviderRegistry::contains(std::string_view provider_id) const noexcept
{
  for (auto const& [id, _] : providers_)
  {
    if (id == provider_id)
      return true;
  }
  return false;
}

ava::core::Result<std::unique_ptr<Provider>> ProviderRegistry::create(std::string_view provider_id) const
{
  for (auto const& [id, factory] : providers_)
  {
    if (id == provider_id)
      return factory();
  }
  auto error = ava::core::Error(ava::core::ErrorCategory::NotFound, "provider is not registered");
  error.with_context("provider", std::string(provider_id));
  return std::unexpected(std::move(error));
}

std::vector<std::string> ProviderRegistry::provider_ids() const
{
  std::vector<std::string> ids;
  ids.reserve(providers_.size());
  for (auto const& [id, _] : providers_) ids.push_back(id);
  return ids;
}

ava::core::Result<BuiltinProviderRegistryComposition> compose_builtin_provider_registry(bool read_generic_base_url_env)
{
  auto resolved = ava::config::resolve_builtin_generic_providers(read_generic_base_url_env);
  if (!resolved)
    return std::unexpected(std::move(resolved.error()));

  ProviderRegistry registry;
  register_legacy_builtin_providers(registry);
  if (auto registered = register_resolved_builtin_generic_providers(registry, *resolved); !registered)
    return std::unexpected(std::move(registered.error()));

  return BuiltinProviderRegistryComposition{.registry = std::move(registry), .resolved_generics = std::move(*resolved)};
}

ProviderRegistry builtin_provider_registry()
{
  // Defaults-only: no generic *_BASE_URL env reads. Compiled defaults are valid
  // by construction, so composition cannot fail here.
  auto composed = compose_builtin_provider_registry(false);
  if (!composed)
  {
    // Defensive: surface a still-usable registry without the six generics rather
    // than aborting process startup from a unit-test helper path.
    ProviderRegistry registry;
    register_legacy_builtin_providers(registry);
    return registry;
  }
  return std::move(composed->registry);
}

}  // namespace ava::provider
