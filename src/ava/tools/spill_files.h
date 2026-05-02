#pragma once

#include <cstddef>
#include <filesystem>
#include <string>
#include <string_view>

#include "ava/core/result.h"
#include "ava/tools/file_tools.h"

namespace ava::tools {

inline constexpr std::size_t kMaxSpillFileBytes = 5 * 1024 * 1024;

class SpillBuffer {
 public:
  explicit SpillBuffer(std::size_t max_bytes = kMaxSpillFileBytes);

  void append(std::string_view text);

  [[nodiscard]] const std::string& content() const noexcept;
  [[nodiscard]] std::size_t total_bytes() const noexcept;
  [[nodiscard]] bool truncated() const noexcept;

 private:
  std::size_t max_bytes_ = 0;
  std::size_t total_bytes_ = 0;
  bool truncated_ = false;
  std::string content_;
};

struct SpillFileResult {
  std::filesystem::path path;
  bool truncated = false;
  std::size_t bytes_written = 0;
};

[[nodiscard]] ava::core::Result<SpillFileResult> write_spill_file(const ToolContext& context,
                                                                  std::string_view tool_name,
                                                                  std::string_view extension,
                                                                  const SpillBuffer& buffer);

[[nodiscard]] ava::core::VoidResult emit_tool_progress(const ToolContext& context, std::string text,
                                                       std::string status = "running");

}  // namespace ava::tools
