#include "ava/config/config.hpp"

namespace ava::config {

ConfigSummary summary() {
  return ConfigSummary{
      .xdg_paths = true,
      .trust_store = true,
      .credential_store = true,
      .embedded_model_registry = true,
  };
}

}  // namespace ava::config
