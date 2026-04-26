#pragma once

#include <filesystem>
#include <string>
#include <string_view>

namespace ava::tools {

[[nodiscard]] std::string read_file_text(const std::filesystem::path& path);
void write_file_text(const std::filesystem::path& path, const std::string& content);
void ensure_regular_file_size_within_limit(
    const std::filesystem::path& path,
    std::uintmax_t limit,
    std::string_view operation
);

}  // namespace ava::tools
