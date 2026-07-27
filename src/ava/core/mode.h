#pragma once

#include "ava/core/result.h"

#include <string>
#include <string_view>

namespace ava::core {

enum class Mode
{
  Build,
  Plan,
};

[[nodiscard]] std::string to_string(Mode mode);
[[nodiscard]] Result<Mode> parse_mode(std::string_view value);
[[nodiscard]] Mode toggle_mode(Mode mode) noexcept;

}  // namespace ava::core
