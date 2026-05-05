#pragma once

#include <cstddef>
#include <filesystem>
#include <fstream>
#include <regex>
#include <string>
#include <string_view>

#include "ava/core/result.h"
#include "ava/tools/file_tools.h"

namespace ava::tools::detail {

inline constexpr std::size_t kSearchVisitedProgressInterval = 10000;
inline constexpr std::size_t kSearchMatchProgressInterval = 500;

[[nodiscard]] std::string search_regex_escape(char ch);
[[nodiscard]] ava::core::Result<std::regex> search_glob_to_regex(std::string_view pattern);
[[nodiscard]] std::string search_relative_slash_path(std::filesystem::path const& root,
                                                     std::filesystem::path const& path);
[[nodiscard]] bool search_looks_binary(std::string_view line);
[[nodiscard]] std::string search_permission_tool_name(ToolContext const& context);
[[nodiscard]] bool is_search_canceled(ToolContext const& context);
[[nodiscard]] ava::core::Error search_canceled_error(std::string_view tool_name);
[[nodiscard]] ava::core::VoidResult check_search_canceled(ToolContext const& context, std::string_view tool_name);
[[nodiscard]] ava::core::Result<bool> can_read_search_match(ToolContext const& context,
                                                            std::filesystem::path const& path);
[[nodiscard]] ava::core::Result<bool> read_limited_search_line(std::ifstream& file, std::string& line,
                                                               std::filesystem::path const& path,
                                                               std::size_t max_line_length, bool& line_truncated,
                                                               bool& line_binary);

}  // namespace ava::tools::detail
