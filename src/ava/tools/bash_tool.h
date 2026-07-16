#pragma once

#include "ava/tools/file_tools.h"
#include "ava/core/result.h"

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>

namespace ava::tools {

struct BashOptions
{
  std::chrono::milliseconds timeout = std::chrono::milliseconds(30'000);
  std::size_t max_bytes = 50 * 1024;
  std::size_t max_lines = 200;

  AVA_DEBUG_PRINT_MEMBERS_ON
};

struct BashResult
{
  std::int64_t exit_code = -1;
  bool timed_out = false;
  bool canceled = false;
  bool truncated = false;
  bool byte_limited = false;
  bool line_limited = false;
  bool spill_truncated = false;
  bool totals_known = true;
  std::size_t total_bytes = 0;
  std::size_t output_bytes = 0;
  std::size_t total_lines = 0;
  std::size_t output_lines = 0;
  std::size_t omitted_lines = 0;
  std::string output;
  std::filesystem::path spill_path;

  AVA_DEBUG_PRINT_MEMBERS_ON
};

[[nodiscard]] ava::core::Result<BashResult> run_bash(ToolContext const& context, std::string_view command, BashOptions options = {});

}  // namespace ava::tools
