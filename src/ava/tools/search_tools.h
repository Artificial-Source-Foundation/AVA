#pragma once

#include <cstddef>
#include <filesystem>
#include <string>
#include <vector>

#include "ava/core/result.h"
#include "ava/tools/file_tools.h"

namespace ava::tools {

struct GlobOptions {
  std::size_t max_results = 2000;
  std::size_t max_visited = 100000;
  std::size_t max_depth = 32;
};

struct GrepOptions {
  std::size_t max_matches = 2000;
  std::size_t max_line_length = 500;
};

struct GlobResult {
  std::vector<std::filesystem::path> paths;
  bool truncated = false;
  std::size_t total_matches = 0;
};

struct GrepMatch {
  std::filesystem::path path;
  std::size_t line_number = 0;
  std::string line;
  bool line_truncated = false;
};

struct GrepResult {
  std::vector<GrepMatch> matches;
  bool truncated = false;
  std::size_t total_matches = 0;
};

[[nodiscard]] ava::core::Result<GlobResult> glob_files(const ToolContext& context,
                                                       std::string_view pattern,
                                                       GlobOptions options = {});
[[nodiscard]] ava::core::Result<GrepResult> grep_files(const ToolContext& context,
                                                       std::string_view literal_pattern,
                                                       std::string_view include_glob = "**/*",
                                                       GrepOptions options = {});

}  // namespace ava::tools
