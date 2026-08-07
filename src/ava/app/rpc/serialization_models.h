#pragma once

#include "ava/app/runtime/session_ts.h"
#include <string>

namespace ava::config {
struct ModelInfo;
} // namespace ava::config

namespace ava::app::runtime {
class Session;
} // namespace ava::app::runtime

namespace ava::app::rpc {

[[nodiscard]] std::string model_info_json(ava::config::ModelInfo const& model, ava::app::runtime::session_ts const& unlocked_session, bool configured);

} // namespace ava::app::rpc
