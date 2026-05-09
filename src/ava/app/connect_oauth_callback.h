#pragma once

#include "ava/core/result.h"

#include <functional>
#include <string>
#include <string_view>

namespace ava::app {

struct OAuthCallbackResult
{
  std::string code;
};

[[nodiscard]] ava::core::Result<OAuthCallbackResult> wait_for_oauth_callback(std::string_view expected_state, std::function<bool()> const& cancel_requested);

}  // namespace ava::app
