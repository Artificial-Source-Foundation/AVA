#pragma once

#include <memory>

#include "cli.hpp"
#include "ava/llm/provider.hpp"

namespace ava::app {

[[nodiscard]] int run_headless_blocking(const CliOptions& cli);
[[nodiscard]] int run_headless_blocking(const CliOptions& cli, ava::llm::ProviderPtr provider_override);

}  // namespace ava::app
