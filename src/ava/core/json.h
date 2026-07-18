#pragma once

#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace ava::core::json {

[[nodiscard]] bool is_valid_utf8(std::string_view value) noexcept;
[[nodiscard]] std::string replace_invalid_utf8(std::string_view value);
[[nodiscard]] std::string escape(std::string_view value);
[[nodiscard]] std::optional<std::size_t> field_value_start(std::string_view object, std::string_view key);
[[nodiscard]] std::optional<std::string> string_field(std::string_view object, std::string_view key);
[[nodiscard]] std::optional<long long> integer_field(std::string_view object, std::string_view key);
[[nodiscard]] std::optional<std::string> object_field(std::string_view object, std::string_view key);
[[nodiscard]] std::vector<std::string> objects_in_array_field(std::string_view object, std::string_view key);
// Returns nullopt unless the named value is a syntactically valid array whose
// every element is a JSON object. Unlike objects_in_array_field(), this never
// skips scalar or malformed elements.
[[nodiscard]] std::optional<std::vector<std::string>> strict_objects_in_array_field(std::string_view object, std::string_view key);
[[nodiscard]] std::vector<std::string> strings_in_array_field(std::string_view object, std::string_view key);
[[nodiscard]] bool is_valid_object(std::string_view value);

}  // namespace ava::core::json
