#pragma once

#include "ava/app/runtime/RuntimeSession.h"

namespace ava::app {

[[nodiscard]] int run_interactive(runtime::RuntimeSession& session);

}  // namespace ava::app
