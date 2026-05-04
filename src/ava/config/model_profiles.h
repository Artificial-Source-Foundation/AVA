#pragma once

#include <string>
#include <string_view>

#include "ava/config/model_config.h"

namespace ava::config {

[[nodiscard]] ModelRegistry builtin_model_profiles();
[[nodiscard]] std::string model_display_label(std::string_view provider_id, std::string_view model_id);
[[nodiscard]] std::string model_display_label(std::string_view model_id);

}  // namespace ava::config
