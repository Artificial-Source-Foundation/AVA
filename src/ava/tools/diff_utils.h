#pragma once

#include <cstddef>
#include <filesystem>
#include <string>
#include <string_view>

namespace ava::tools {

struct DiffPreview {
  std::string text;
  bool truncated = false;
};

[[nodiscard]] DiffPreview unified_diff(std::string_view old_content, std::string_view new_content,
                                       const std::filesystem::path& old_path, const std::filesystem::path& new_path,
                                       std::size_t max_bytes = 32 * 1024);

}  // namespace ava::tools
