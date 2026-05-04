#pragma once

#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace ava::core::json {

[[nodiscard]] std::string escape(std::string_view value);
[[nodiscard]] std::optional<std::size_t> field_value_start(std::string_view object, std::string_view key);
[[nodiscard]] std::optional<std::string> string_field(std::string_view object, std::string_view key);
[[nodiscard]] std::optional<long long> integer_field(std::string_view object, std::string_view key);
[[nodiscard]] std::optional<std::string> object_field(std::string_view object, std::string_view key);
[[nodiscard]] std::vector<std::string> objects_in_array_field(std::string_view object, std::string_view key);
[[nodiscard]] bool is_valid_object(std::string_view value);

}  // namespace ava::core::json
