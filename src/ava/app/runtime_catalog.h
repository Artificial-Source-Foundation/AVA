#pragma once

#include "ava/app/runtime.h"

#include <memory>
#include <string_view>
#include <vector>

namespace ava::app {

[[nodiscard]] ava::core::Result<std::unique_ptr<ava::provider::Provider>> create_runtime_provider(std::string_view provider_id);
[[nodiscard]] ava::core::Result<std::vector<ava::config::ModelInfo>> runtime_model_catalog(RuntimeSession const& session);
[[nodiscard]] ava::core::Result<ava::config::ModelInfo> select_runtime_model(RuntimeSession const& session, std::optional<std::string_view> provider_id,
                                                                             std::string_view model_id);
[[nodiscard]] ava::core::Result<ava::config::ModelInfo> cycle_runtime_model(RuntimeSession const& session, int direction);

}  // namespace ava::app
