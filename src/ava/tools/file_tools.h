#pragma once

#include <cstddef>
#include <filesystem>
#include <string>

#include "ava/agent/mode.h"
#include "ava/core/result.h"
#include "ava/permissions/permission.h"

namespace ava::tools {

struct ToolContext {
  std::filesystem::path workspace_dir;
  ava::agent::Mode mode = ava::agent::Mode::Build;
};

struct TextOutput {
  std::string content;
  bool truncated = false;
  std::size_t total_bytes = 0;
  std::size_t output_bytes = 0;
};

struct FileMutationResult {
  std::filesystem::path path;
  std::size_t bytes_written = 0;
};

struct ReadOptions {
  std::size_t max_bytes = 50 * 1024;
};

[[nodiscard]] ava::core::Result<TextOutput> read_file(const ToolContext& context,
                                                      const std::filesystem::path& path,
                                                      ReadOptions options = {});
[[nodiscard]] ava::core::Result<FileMutationResult> write_file(const ToolContext& context,
                                                               const std::filesystem::path& path,
                                                               std::string_view content);
[[nodiscard]] ava::core::Result<FileMutationResult> edit_file(const ToolContext& context,
                                                              const std::filesystem::path& path,
                                                              std::string_view old_text,
                                                              std::string_view new_text);

}  // namespace ava::tools
