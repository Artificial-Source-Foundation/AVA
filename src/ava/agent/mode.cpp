#include "ava/agent/mode.h"

#include <string_view>
#include <utility>

namespace ava::agent {

std::string to_string(Mode mode)
{
  switch (mode)
  {
    case Mode::Build:
      return "build";
    case Mode::Plan:
      return "plan";
  }
  return "build";
}

ava::core::Result<Mode> parse_mode(std::string_view value)
{
  if (value == "build")
  {
    return Mode::Build;
  }
  if (value == "plan")
  {
    return Mode::Plan;
  }

  auto error = ava::core::Error(ava::core::ErrorCategory::InvalidArgument, "unknown mode");
  error.with_context("mode", std::string(value));
  return std::unexpected(std::move(error));
}

Mode toggle_mode(Mode mode) noexcept
{
  return mode == Mode::Build ? Mode::Plan : Mode::Build;
}

}  // namespace ava::agent
