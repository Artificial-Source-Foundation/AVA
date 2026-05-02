#include "ava/provider/registry.h"

#include <utility>

#include "ava/provider/anthropic_provider.h"
#include "ava/provider/openai_provider.h"

namespace ava::provider {

ava::core::VoidResult ProviderRegistry::register_provider(std::string provider_id, Factory factory) {
  if (provider_id.empty()) {
    return std::unexpected(ava::core::Error(ava::core::ErrorCategory::InvalidArgument, "provider id is required"));
  }
  if (!factory) {
    auto error = ava::core::Error(ava::core::ErrorCategory::InvalidArgument, "provider factory is required");
    error.with_context("provider", provider_id);
    return std::unexpected(std::move(error));
  }
  if (contains(provider_id)) {
    auto error = ava::core::Error(ava::core::ErrorCategory::InvalidArgument, "provider is already registered");
    error.with_context("provider", provider_id);
    return std::unexpected(std::move(error));
  }
  providers_.push_back(std::make_pair(std::move(provider_id), std::move(factory)));
  return {};
}

bool ProviderRegistry::contains(std::string_view provider_id) const noexcept {
  for (const auto& [id, _] : providers_) {
    if (id == provider_id) return true;
  }
  return false;
}

ava::core::Result<std::unique_ptr<Provider>> ProviderRegistry::create(std::string_view provider_id) const {
  for (const auto& [id, factory] : providers_) {
    if (id == provider_id) return factory();
  }
  auto error = ava::core::Error(ava::core::ErrorCategory::NotFound, "provider is not registered");
  error.with_context("provider", std::string(provider_id));
  return std::unexpected(std::move(error));
}

std::vector<std::string> ProviderRegistry::provider_ids() const {
  std::vector<std::string> ids;
  ids.reserve(providers_.size());
  for (const auto& [id, _] : providers_) ids.push_back(id);
  return ids;
}

ProviderRegistry builtin_provider_registry() {
  ProviderRegistry registry;
  static_cast<void>(registry.register_provider("anthropic", [] { return std::make_unique<AnthropicProvider>(); }));
  static_cast<void>(registry.register_provider("openai", [] { return std::make_unique<OpenAIProvider>(); }));
  return registry;
}

}  // namespace ava::provider
