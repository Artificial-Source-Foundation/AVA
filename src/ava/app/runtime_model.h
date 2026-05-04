#pragma once

#include <optional>
#include <vector>

#include "ava/app/runtime.h"

namespace ava::app::runtime {

[[nodiscard]] std::optional<ava::config::ModelInfo> latest_persisted_model(
    ava::config::ModelRegistry const& registry, std::vector<ava::session::SessionEntry> const& entries);

}  // namespace ava::app::runtime
