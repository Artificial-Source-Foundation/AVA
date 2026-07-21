#pragma once

#include "ava/core/result.h"
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>

namespace ava::app::runtime {

constexpr std::size_t kMaxCommandFileBytes = 64 * 1024;
constexpr std::size_t kMaxCommandTokenBytes = 256;

bool valid_command_segment(std::string_view segment);
bool valid_prompt_command_name(std::string_view name);
std::optional<std::string> command_name_for_file(std::filesystem::path const& root, std::filesystem::path const& file);
ava::core::Result<std::string> read_bounded_file(std::filesystem::path const& path, std::size_t max_bytes = kMaxCommandFileBytes);

} // namespace ava::app::runtime
