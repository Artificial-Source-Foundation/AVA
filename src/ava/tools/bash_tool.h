#pragma once

#include <chrono>
#include <cstddef>
#include <string>

#include "ava/core/result.h"
#include "ava/tools/file_tools.h"

namespace ava::tools {

struct BashOptions {
  std::chrono::milliseconds timeout = std::chrono::milliseconds(30'000);
  std::size_t max_bytes = 50 * 1024;
};

struct BashResult {
  int exit_code = -1;
  bool timed_out = false;
  bool truncated = false;
  std::size_t total_bytes = 0;
  std::string output;
};

[[nodiscard]] ava::core::Result<BashResult> run_bash(const ToolContext& context,
                                                     std::string_view command,
                                                     BashOptions options = {});

}  // namespace ava::tools
