#pragma once

#include <string>

namespace ava::config {
struct ModelInfo;
}

namespace ava::app::runtime {
class Session;
}

namespace ava::app::rpc {

[[nodiscard]] std::string model_info_json(ava::config::ModelInfo const& model, ava::app::runtime::Session const& session, bool configured);

}  // namespace ava::app::rpc
