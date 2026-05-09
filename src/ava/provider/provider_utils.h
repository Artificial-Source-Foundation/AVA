#pragma once

#include <initializer_list>
#include <string>
#include <string_view>

namespace ava::provider {

[[nodiscard]] bool is_json_object_shape(std::string_view value);
[[nodiscard]] bool is_valid_json_object(std::string_view value);
[[nodiscard]] std::string base64_encode(std::string_view bytes);
[[nodiscard]] bool is_valid_base64(std::string_view value);
[[nodiscard]] std::string sanitized_body_snippet(std::string_view body,
                                                  std::initializer_list<std::string_view> secret_keys);

}  // namespace ava::provider
