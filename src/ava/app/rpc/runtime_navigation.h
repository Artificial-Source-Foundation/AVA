#pragma once

#include "ava/app/runtime.h"
#include "ava/app/runtime/Session.h"
#include "ava/app/runtime/session_ts.h"

namespace ava::app::rpc {

[[nodiscard]] ava::core::Result<ava::config::ModelInfo> next_runtime_model(ava::app::runtime::session_ts::rat const& session_r);
[[nodiscard]] ava::core::Result<ava::config::ModelInfo> previous_runtime_model(ava::app::runtime::session_ts::rat const& session_r);

}  // namespace ava::app::rpc
