#pragma once

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <optional>

namespace ava::platform {

struct FileInfo {
  std::filesystem::path path;
  std::uintmax_t size{0};
  bool is_directory{false};
  std::optional<std::filesystem::file_time_type> modified_at;
};

}  // namespace ava::platform
