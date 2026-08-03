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

ProviderRegistry builtin_provider_registry()
{
  ProviderRegistry registry;
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

  // One declarative registration path for first-class generic built-ins.
  for (auto const& spec : ava::config::builtin_generic_provider_specs())
  {
    static_cast<void>(registry.register_provider(std::string(spec.provider_id), [spec]() -> std::unique_ptr<Provider> {
      auto const base_url = env_or_default(std::string(spec.base_url_env).c_str(), std::string(spec.default_base_url));
      if (spec.protocol == ava::config::BuiltinGenericProtocol::OpenAIResponses)
      {
        return std::make_unique<OpenAIProvider>(OpenAIProviderOptions{
            .base_url = base_url,
            .follow_redirects = false,
            .require_credential = true,
            .send_authorization_bearer = true,
            .enable_codex_oauth_mutations = false,
            .force_include_max_output_tokens = true,
        });
      }
      return std::make_unique<OpenAICompatibleProvider>(OpenAICompatibleProviderOptions{
          .base_url = base_url,
          .chat_completions_path = std::string(spec.request_path),
          .provider_name = std::string(spec.display_name),
          .include_stream_usage = spec.include_stream_usage,
          .follow_redirects = false,
          .require_credential = true,
          .send_authorization_bearer = true,
      });
    }));
  }
  return registry;
}

}  // namespace ava::provider
