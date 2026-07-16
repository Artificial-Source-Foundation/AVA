#pragma once

#include "ava/app/runtime.h"

#include <optional>
#include <vector>

namespace ava::app::runtime {

[[nodiscard]] ava::core::VoidResult validate_runtime_model_history(ava::session::SessionStore const& store, ava::config::ModelInfo const& target,
                                                                   ava::session::SessionReadLimits read_limits);
[[nodiscard]] std::optional<ava::config::ModelInfo> latest_persisted_model(ava::config::ModelRegistry const& registry,
                                                                           std::vector<ava::session::SessionEntry> const& entries);

}  // namespace ava::app::runtime
