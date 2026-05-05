#pragma once

#include <string_view>

namespace ava::session {

[[nodiscard]] bool supported_entry_version(long long version);
[[nodiscard]] bool bool_field_is_true(std::string_view object, std::string_view key);
[[nodiscard]] bool bool_field_is_false(std::string_view object, std::string_view key);
[[nodiscard]] bool valid_status(std::string_view status);
[[nodiscard]] bool valid_operation(std::string_view operation);
[[nodiscard]] bool valid_mode(std::string_view mode);
[[nodiscard]] bool valid_action(std::string_view action);
[[nodiscard]] bool valid_resolution(std::string_view resolution);
[[nodiscard]] bool valid_resolution_source(std::string_view source);
[[nodiscard]] bool valid_risk(std::string_view risk);
[[nodiscard]] bool present_non_empty_string(std::string_view object, std::string_view key);
[[nodiscard]] bool present_boolean(std::string_view object, std::string_view key);
[[nodiscard]] bool required_boolean(std::string_view object, std::string_view key);
[[nodiscard]] bool present_integer_matching(std::string_view object, std::string_view key, bool require_positive);

}  // namespace ava::session
