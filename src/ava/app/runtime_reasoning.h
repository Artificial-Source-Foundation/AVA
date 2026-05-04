#pragma once

#include <optional>
#include <vector>

#include "ava/app/runtime.h"
#include "ava/provider/provider.h"

namespace ava::app::runtime {

[[nodiscard]] std::optional<RuntimeReasoningSelection> latest_persisted_reasoning(
    std::vector<ava::session::SessionEntry> const& entries, ava::config::ModelInfo const& model);
[[nodiscard]] ava::provider::ProviderReasoningOptions provider_reasoning_options(
    RuntimeReasoningSelection const& selection);

}  // namespace ava::app::runtime
