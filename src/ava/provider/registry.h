#pragma once

#include "ava/config/builtin_generic_providers.h"
#include "ava/provider/provider.h"
#include "ava/core/result.h"

#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace ava::provider {

class ProviderRegistry
{
 public:
  using Factory = std::function<std::unique_ptr<Provider>()>;

  [[nodiscard]] ava::core::VoidResult register_provider(std::string provider_id, Factory factory);
  [[nodiscard]] bool contains(std::string_view provider_id) const noexcept;
  [[nodiscard]] ava::core::Result<std::unique_ptr<Provider>> create(std::string_view provider_id) const;
  [[nodiscard]] std::vector<std::string> provider_ids() const;

  AVA_DEBUG_PRINT_MEMBERS_ON

 private:
  std::vector<std::pair<std::string, Factory>> providers_;
};

// Built-in registry composition. Generic built-in base URLs are captured from
// `resolved_generics` (no getenv at Provider::create time).
struct BuiltinProviderRegistryComposition
{
  ProviderRegistry registry;
  std::vector<ava::config::ResolvedBuiltinGenericProvider> resolved_generics;

  AVA_DEBUG_PRINT_MEMBERS_ON
};

// Compose legacy built-ins plus the six declarative generic built-ins.
// When read_generic_base_url_env is true, generic *_BASE_URL overrides are
// resolved once under the providers.json URL policy; invalid overrides fail.
// When false, compiled defaults are canonicalized (no process env reads).
[[nodiscard]] ava::core::Result<BuiltinProviderRegistryComposition> compose_builtin_provider_registry(bool read_generic_base_url_env);

// Defaults-only composition (no generic base URL env reads). Never fails when
// compiled defaults remain valid.
[[nodiscard]] ProviderRegistry builtin_provider_registry();

}  // namespace ava::provider
