#pragma once

#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "ava/core/result.h"

namespace ava::config::detail {

[[nodiscard]] std::string base64_url_encode(std::span<std::uint8_t const> bytes);
[[nodiscard]] std::optional<std::vector<std::uint8_t>> base64_url_decode(std::string_view value);
[[nodiscard]] std::string code_challenge(std::string_view verifier);
[[nodiscard]] std::string url_encode(std::string_view value);
[[nodiscard]] ava::core::Result<std::string> random_token(std::size_t bytes);

}  // namespace ava::config::detail
