#pragma once

#include <filesystem>
#include <string>

#include "ava/platform/execute.hpp"
#include "ava/platform/filesystem.hpp"

namespace ava::platform {

[[nodiscard]] std::string platform_tag();

class StandardPlatform {
 public:
  [[nodiscard]] std::string read_file(const std::filesystem::path& path) const;
  void write_file(const std::filesystem::path& path, const std::string& content) const;
  void create_dir_all(const std::filesystem::path& path) const;

  [[nodiscard]] bool exists(const std::filesystem::path& path) const noexcept;
  [[nodiscard]] bool is_directory(const std::filesystem::path& path) const noexcept;
  [[nodiscard]] FileInfo metadata(const std::filesystem::path& path) const;

  // Milestone 3 boundary: command execution is still deferred.
  [[nodiscard]] static constexpr bool supports_command_execution() noexcept { return false; }

 private:
  LocalFileSystem file_system_;
};

}  // namespace ava::platform
