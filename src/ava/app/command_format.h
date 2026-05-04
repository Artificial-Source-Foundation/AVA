#pragma once

#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace ava::app {

struct CommandResult;

void add_output(CommandResult& result, std::string text);

[[nodiscard]] std::string display_path(std::filesystem::path const& path, std::filesystem::path const& base);
[[nodiscard]] std::string sanitize_inline_text(std::string text);
[[nodiscard]] std::string joined_strings(std::vector<std::string> const& values, std::string_view separator);
[[nodiscard]] std::string missing_argument(std::string_view usage);
[[nodiscard]] std::string command_argument(std::string_view line, std::string_view command);
[[nodiscard]] std::vector<std::string> split_command_arguments(std::string_view text);

}  // namespace ava::app
