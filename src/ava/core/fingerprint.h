#pragma once

#include <cstdint>
#include <string_view>

namespace ava::core {

[[nodiscard]] inline std::uint64_t content_fingerprint(std::string_view text)
{
  constexpr std::uint64_t offset_basis = 14695981039346656037ULL;
  constexpr std::uint64_t prime = 1099511628211ULL;
  std::uint64_t fingerprint = offset_basis;
  for (unsigned char const character : text) {
    fingerprint ^= character;
    fingerprint *= prime;
  }
  return fingerprint;
}

}  // namespace ava::core
