#pragma once

#include <filesystem>
#include <string>

#include "ava/platform/file_info.hpp"

namespace ava::platform {

class LocalFileSystem {
 public:
  [[nodiscard]] std::string read_file(const std::filesystem::path& path) const;
  void write_file(const std::filesystem::path& path, const std::string& content) const;
  void create_dir_all(const std::filesystem::path& path) const;

  [[nodiscard]] bool exists(const std::filesystem::path& path) const noexcept;
  [[nodiscard]] bool is_directory(const std::filesystem::path& path) const noexcept;
  [[nodiscard]] FileInfo metadata(const std::filesystem::path& path) const;
};

}  // namespace ava::platform
