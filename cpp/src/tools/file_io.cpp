#include "file_io.hpp"

#include <fstream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <system_error>

namespace ava::tools {

std::string read_file_text(const std::filesystem::path& path) {
  std::ifstream file(path, std::ios::binary);
  if(!file) {
    throw std::runtime_error("Failed to read file: " + path.string());
  }
  std::ostringstream buffer;
  buffer << file.rdbuf();
  return buffer.str();
}

void write_file_text(const std::filesystem::path& path, const std::string& content) {
  if(path.has_parent_path()) {
    std::error_code ec;
    std::filesystem::create_directories(path.parent_path(), ec);
    if(ec) {
      throw std::runtime_error("Failed to create parent directories for: " + path.string());
    }
  }

  std::ofstream out(path, std::ios::binary | std::ios::trunc);
  if(!out) {
    throw std::runtime_error("Failed to write file: " + path.string());
  }
  out << content;
  out.flush();
  if(!out) {
    throw std::runtime_error("Failed to write content to file: " + path.string());
  }
  out.close();
  if(!out) {
    throw std::runtime_error("Failed to close written file: " + path.string());
  }
}

void ensure_regular_file_size_within_limit(
    const std::filesystem::path& path,
    std::uintmax_t limit,
    std::string_view operation
) {
  std::error_code ec;
  if(!std::filesystem::is_regular_file(path, ec) || ec) {
    throw std::runtime_error(std::string(operation) + " target must be a regular file: " + path.string());
  }
  const auto size = std::filesystem::file_size(path, ec);
  if(ec) {
    throw std::runtime_error(std::string("Failed to stat ") + std::string(operation) + " target: " + path.string());
  }
  if(size > limit) {
    throw std::runtime_error(std::string(operation) + " target is too large: " + path.string());
  }
}

}  // namespace ava::tools
