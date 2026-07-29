#pragma once

#include "ava/app/runtime.h"

namespace ava::app::rpc {

[[nodiscard]] ava::core::Result<runtime::Session> open_requested_session(runtime::Session const& current, runtime::OpenContext const& base_context,
                                                                         std::string_view requested_session_id);
[[nodiscard]] ava::core::Result<ava::config::ModelInfo> next_runtime_model(runtime::Session const& session);
[[nodiscard]] ava::core::Result<ava::config::ModelInfo> previous_runtime_model(runtime::Session const& session);

}  // namespace ava::app::rpc
