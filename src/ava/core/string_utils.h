#pragma once

#include <string_view>
#include <string>

namespace ava::core {

[[nodiscard]] std::string_view trim_view(std::string_view value);
[[nodiscard]] std::string trim(std::string_view value);
[[nodiscard]] std::string_view strip_matching_quotes(std::string_view value);

// These overloads are provided so that the compiler produces an error when attempting to pass a temporary std::string.
std::string_view trim_view(std::string&&) = delete;             // Do not pass a temporary std::string to trim_view!
std::string_view strip_matching_quotes(std::string&&) = delete; // Do not pass a temporary std::string to strip_matching_quotes!

} // namespace ava::core
