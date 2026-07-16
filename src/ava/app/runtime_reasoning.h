#pragma once

#include "ava/app/runtime.h"
#include "ava/provider/provider.h"

#include <optional>
#include <vector>

namespace ava::app::runtime {

[[nodiscard]] std::optional<runtime::RuntimeReasoningSelection> latest_persisted_reasoning(std::vector<ava::session::SessionEntry> const& entries,
                                                                                  ava::config::ModelInfo const& model);
[[nodiscard]] ava::provider::ProviderReasoningOptions provider_reasoning_options(runtime::RuntimeReasoningSelection const& selection);

}  // namespace ava::app::runtime
