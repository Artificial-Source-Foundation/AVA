#pragma once

#include "runtime.h"
#include "runtime/Session.h"
#include "runtime/session_ts.h"

#include <memory>
#include <string_view>
#include <vector>

namespace ava::provider {
class Provider;
}

namespace ava::app {

[[nodiscard]] ava::core::Result<std::unique_ptr<ava::provider::Provider>> create_runtime_provider(runtime::Session const& session,
                                                                                                  std::string_view provider_id);
// Compatibility overload for adapters without a session; uses builtins-only catalog.
[[nodiscard]] ava::core::Result<std::unique_ptr<ava::provider::Provider>> create_runtime_provider(std::string_view provider_id);
[[nodiscard]] ava::core::Result<std::vector<ava::config::ModelInfo>> runtime_model_catalog(runtime::session_ts::crat const& session_r);
[[nodiscard]] ava::core::Result<ava::config::ModelInfo> select_runtime_model(runtime::session_ts::crat const& session_r,
                                                                             std::optional<std::string_view> provider_id, std::string_view model_id);
[[nodiscard]] ava::core::Result<ava::config::ModelInfo> cycle_runtime_model(runtime::session_ts::crat const& session_r, int direction);

}  // namespace ava::app
