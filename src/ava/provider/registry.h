#pragma once

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

[[nodiscard]] ProviderRegistry builtin_provider_registry();

}  // namespace ava::provider
