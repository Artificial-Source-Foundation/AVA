#pragma once

#include "ava/core/Application.h"
#include <string_view>
#include "debug.h"

namespace ava::app {

class Application final : public ava::core::Application
{
 public:
  [[nodiscard]] std::string_view application_name() const noexcept override { return "AVA"; }

  // Can't print ava::core::Application.
  AVA_DEBUG_PRINT_MEMBERS_OPT_OUT
};

}  // namespace ava::app
