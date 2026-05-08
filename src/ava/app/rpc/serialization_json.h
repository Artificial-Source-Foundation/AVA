#pragma once

#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace ava::app::rpc {

[[nodiscard]] std::string decimal_field_json(std::string_view key, long double value);
void append_optional_bool(std::string& json, std::string_view key, std::optional<bool> const& value);
void append_optional_integer(std::string& json, std::string_view key, std::optional<long long> const& value);
[[nodiscard]] std::string string_field_json(std::string_view key, std::string_view value);
[[nodiscard]] std::string bool_field_json(std::string_view key, bool value);
[[nodiscard]] std::string number_field_json(std::string_view key, std::size_t value);
[[nodiscard]] std::string integer_field_json(std::string_view key, long long value);
[[nodiscard]] std::string output_array_json(std::vector<std::string> const& output);
[[nodiscard]] std::string string_array_json(std::vector<std::string> const& values);

}  // namespace ava::app::rpc
