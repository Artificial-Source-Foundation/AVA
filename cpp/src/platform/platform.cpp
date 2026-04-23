#include "ava/platform/platform.hpp"

namespace ava::platform {

std::string platform_tag() {
#if defined(_WIN32)
  constexpr auto os_name = "windows";
#elif defined(__APPLE__)
  constexpr auto os_name = "macos";
#elif defined(__linux__)
  constexpr auto os_name = "linux";
#else
  constexpr auto os_name = "unknown";
#endif
  return std::string(os_name) + "-cpp20";
}

std::string StandardPlatform::read_file(const std::filesystem::path& path) const {
  return file_system_.read_file(path);
}

void StandardPlatform::write_file(const std::filesystem::path& path, const std::string& content) const {
  file_system_.write_file(path, content);
}

void StandardPlatform::create_dir_all(const std::filesystem::path& path) const {
  file_system_.create_dir_all(path);
}

bool StandardPlatform::exists(const std::filesystem::path& path) const noexcept {
  return file_system_.exists(path);
}

bool StandardPlatform::is_directory(const std::filesystem::path& path) const noexcept {
  return file_system_.is_directory(path);
}

FileInfo StandardPlatform::metadata(const std::filesystem::path& path) const {
  return file_system_.metadata(path);
}

}  // namespace ava::platform
