#pragma once

#include <string>

namespace ava::config {
struct ModelInfo;
}

namespace ava::app {
struct RuntimeSession;
}

namespace ava::app::rpc {

[[nodiscard]] std::string model_info_json(ava::config::ModelInfo const& model, ava::app::RuntimeSession const& session, bool configured);

}  // namespace ava::app::rpc
