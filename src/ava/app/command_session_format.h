#pragma once

#include <string>

#include "ava/app/runtime.h"
#include "ava/session/stats.h"

namespace ava::app {

[[nodiscard]] std::string format_session_stats_text(RuntimeSession const& session,
                                                    ava::session::SessionStats const& stats);

}  // namespace ava::app
