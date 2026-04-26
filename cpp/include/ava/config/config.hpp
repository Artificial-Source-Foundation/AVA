#pragma once

#include "ava/config/credentials.hpp"
#include "ava/config/agents.hpp"
#include "ava/config/model_spec.hpp"
#include "ava/config/model_registry.hpp"
#include "ava/config/paths.hpp"
#include "ava/config/project_state.hpp"
#include "ava/config/routing.hpp"
#include "ava/config/keychain.hpp"
#include "ava/config/trust.hpp"

namespace ava::config {

struct ConfigSummary {
  bool xdg_paths{false};
  bool trust_store{false};
  bool credential_store{false};
  bool embedded_model_registry{false};
  bool routing_config{false};
  bool project_state{false};
  bool keychain_redaction{false};
};

[[nodiscard]] ConfigSummary summary();

}  // namespace ava::config
