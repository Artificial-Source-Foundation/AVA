#pragma once

#include "ava/app/runtime.h"

#include <optional>
#include <string>
#include <string_view>

namespace ava::app {

[[nodiscard]] std::optional<std::string> first_run_auth_onboarding_message(runtime::session_ts const& unlocked_session);
[[nodiscard]] std::string provider_auth_required_message(runtime::session_ts const& unlocked_session, std::string_view offline_suffix = {});

}  // namespace ava::app
