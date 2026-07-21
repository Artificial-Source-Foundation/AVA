#pragma once

#include <string>
#include <string_view>

namespace ava::provider {

[[nodiscard]] bool is_json_object_shape(std::string_view value);
[[nodiscard]] bool is_valid_json_object(std::string_view value);
[[nodiscard]] std::string base64_encode(std::string_view bytes);
[[nodiscard]] bool is_valid_base64(std::string_view value);

}  // namespace ava::provider
