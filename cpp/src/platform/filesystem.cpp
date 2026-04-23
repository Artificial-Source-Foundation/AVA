#include "ava/platform/filesystem.hpp"

#include <fstream>
#include <sstream>
#include <stdexcept>

namespace ava::platform {

std::string LocalFileSystem::read_file(const std::filesystem::path& path) const {
  std::ifstream input(path, std::ios::in | std::ios::binary);
  if(!input) {
    throw std::runtime_error("Failed to read file: " + path.string());
  }

  std::ostringstream buffer;
  buffer << input.rdbuf();
  return buffer.str();
}

void LocalFileSystem::write_file(const std::filesystem::path& path, const std::string& content) const {
  if(path.has_parent_path()) {
    create_dir_all(path.parent_path());
  }

  std::ofstream output(path, std::ios::out | std::ios::binary | std::ios::trunc);
  if(!output) {
    throw std::runtime_error("Failed to write file: " + path.string());
  }

  output << content;
}

void LocalFileSystem::create_dir_all(const std::filesystem::path& path) const {
  if(path.empty()) {
    return;
  }

  std::error_code ec;
  std::filesystem::create_directories(path, ec);
  if(ec) {
    throw std::runtime_error("Failed to create directory path: " + path.string() + " (" + ec.message() + ")");
  }
}

bool LocalFileSystem::exists(const std::filesystem::path& path) const noexcept {
  std::error_code ec;
  return std::filesystem::exists(path, ec);
}

bool LocalFileSystem::is_directory(const std::filesystem::path& path) const noexcept {
  std::error_code ec;
  return std::filesystem::is_directory(path, ec);
}

FileInfo LocalFileSystem::metadata(const std::filesystem::path& path) const {
  std::error_code ec;
  const auto status = std::filesystem::status(path, ec);
  if(ec) {
    throw std::runtime_error("Failed to stat path: " + path.string() + " (" + ec.message() + ")");
  }

  FileInfo info;
  info.path = path;
  info.is_directory = std::filesystem::is_directory(status);

  if(!info.is_directory) {
    info.size = std::filesystem::file_size(path, ec);
    if(ec) {
      throw std::runtime_error("Failed to read file size: " + path.string() + " (" + ec.message() + ")");
    }
  }

  const auto modified = std::filesystem::last_write_time(path, ec);
  if(!ec) {
    info.modified_at = modified;
  }

  return info;
}

}  // namespace ava::platform
