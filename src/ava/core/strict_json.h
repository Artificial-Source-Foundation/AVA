#pragma once

#include <cstddef>
#include <string_view>

namespace ava::core {

enum class StrictJsonStatus
{
  Valid,
  Invalid,
  NestingTooDeep,
  DuplicateObjectKey,
};

// Validates one complete JSON value without constructing a DOM. Object member
// names are compared after JSON escape decoding, so aliases such as "id" and
// "\u0069d" are duplicates.
[[nodiscard]] StrictJsonStatus validate_strict_json(std::string_view value, std::size_t max_nesting_depth);

}  // namespace ava::core
