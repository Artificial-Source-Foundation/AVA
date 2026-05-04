#pragma once

#include <optional>
#include <vector>

#include "ava/app/runtime.h"

namespace ava::app::runtime {

[[nodiscard]] std::optional<ava::config::ModelInfo> latest_persisted_model(
    const ava::config::ModelRegistry& registry, const std::vector<ava::session::SessionEntry>& entries);

}  // namespace ava::app::runtime
