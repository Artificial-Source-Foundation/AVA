#include "ava/config/model_spec.hpp"

#include <algorithm>

#include "ava/config/credentials.hpp"
#include "ava/config/model_registry.hpp"

namespace ava::config {

ParsedModelSpec parse_model_spec(const std::string& spec) {
  if(const auto slash = spec.find('/'); slash != std::string::npos) {
    auto provider = normalize_provider_name(spec.substr(0, slash));
    auto model = spec.substr(slash + 1);
    if(!model.empty()) {
      const auto& known = known_providers();
      const auto known_provider = std::find(known.begin(), known.end(), provider) != known.end();
      if(known_provider || provider.starts_with("cli:")) {
        return ParsedModelSpec{.provider = provider, .model = model};
      }
    }
  }

  if(const auto normalized = registry().normalize(spec); normalized.has_value()) {
    if(const auto* registered = registry().find(*normalized); registered != nullptr) {
      return ParsedModelSpec{.provider = registered->provider, .model = registered->id};
    }
  }

  return ParsedModelSpec{.provider = "openrouter", .model = spec};
}

}  // namespace ava::config
