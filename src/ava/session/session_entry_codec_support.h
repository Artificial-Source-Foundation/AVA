#pragma once

#include <optional>
#include <string>
#include <string_view>

#include "ava/core/result.h"

namespace ava::session::detail {

[[nodiscard]] int hex_value(char ch);
void append_utf8(std::string& out, int codepoint);
void append_utf8_codepoint(std::string& out, int codepoint);
[[nodiscard]] bool is_json_value_delimiter(char ch);
[[nodiscard]] ava::core::Result<std::optional<long long>> extract_entry_version(std::string_view line);
[[nodiscard]] std::string extract_json_string(std::string_view line, std::string_view key);
[[nodiscard]] bool json_object_field_has_object_value(std::string_view line, std::string_view key);
[[nodiscard]] std::string extract_json_object(std::string_view line, std::string_view key);

}  // namespace ava::session::detail
