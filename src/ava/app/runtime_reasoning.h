#pragma once

#include <optional>
#include <vector>

#include "ava/app/runtime.h"
#include "ava/provider/provider.h"

namespace ava::app::runtime {

[[nodiscard]] std::optional<RuntimeReasoningSelection> latest_persisted_reasoning(
    const std::vector<ava::session::SessionEntry>& entries, const ava::config::ModelInfo& model);
[[nodiscard]] ava::provider::ProviderReasoningOptions provider_reasoning_options(
    const RuntimeReasoningSelection& selection);

}  // namespace ava::app::runtime
