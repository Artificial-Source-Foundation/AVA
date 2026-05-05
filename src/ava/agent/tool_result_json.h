#pragma once

#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

#include "ava/core/error.h"

namespace ava::agent {

[[nodiscard]] std::string json_bool_literal(bool value);
[[nodiscard]] std::string tool_error_result_json(std::string_view tool, ava::core::Error const& error);
void append_tool_result_spill_fields(std::string& text, std::filesystem::path const& path, bool spill_truncated);
void append_changed_files_json(std::string& text, std::vector<std::filesystem::path> const& paths);
void append_diff_json(std::string& text, std::string_view diff, bool truncated);

}  // namespace ava::agent
