#pragma once

#include "ava/app/runtime.h"

namespace ava::app::rpc {

[[nodiscard]] ava::core::Result<RuntimeSession> open_requested_session(RuntimeSession const& current, RuntimeOpenOptions const& base_options,
                                                                       std::string_view requested_session_id);
[[nodiscard]] ava::core::Result<ava::config::ModelInfo> next_runtime_model(RuntimeSession const& session);
[[nodiscard]] ava::core::Result<ava::config::ModelInfo> previous_runtime_model(RuntimeSession const& session);

}  // namespace ava::app::rpc
