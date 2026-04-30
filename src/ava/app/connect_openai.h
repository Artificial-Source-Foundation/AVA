#pragma once

#include "ava/config/xdg_paths.h"

namespace ava::app {

[[nodiscard]] int run_connect_openai(const ava::config::XdgPaths& paths);

}  // namespace ava::app
