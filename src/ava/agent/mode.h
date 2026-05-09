#pragma once

#include "ava/core/result.h"

#include <string>

namespace ava::agent {

enum class Mode
{
  Build,
  Plan,
};

[[nodiscard]] std::string to_string(Mode mode);
[[nodiscard]] ava::core::Result<Mode> parse_mode(std::string_view value);
[[nodiscard]] Mode toggle_mode(Mode mode) noexcept;

}  // namespace ava::agent
