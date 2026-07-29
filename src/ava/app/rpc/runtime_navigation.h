#pragma once

#include "ava/app/runtime.h"

namespace ava::app::rpc {

[[nodiscard]] ava::core::Result<ava::config::ModelInfo> next_runtime_model(runtime::Session const& session);
[[nodiscard]] ava::core::Result<ava::config::ModelInfo> previous_runtime_model(runtime::Session const& session);

}  // namespace ava::app::rpc
