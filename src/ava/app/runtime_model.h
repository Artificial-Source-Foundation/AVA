#pragma once

#include "ava/app/runtime.h"

#include <optional>
#include <vector>

namespace ava::app::runtime {

[[nodiscard]] ava::core::VoidResult validate_runtime_model_history(ava::session::SessionReadAuthority read_authority, ava::config::ModelInfo const& target);
[[nodiscard]] std::optional<ava::config::ModelInfo> latest_persisted_model(ava::config::ModelRegistry const& registry,
                                                                           std::vector<ava::session::SessionEntry> const& entries);

}  // namespace ava::app::runtime
