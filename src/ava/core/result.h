#pragma once

#include "ava/core/error.h"

#include <expected>

namespace ava::core {

template <typename T>
using Result = std::expected<T, Error>;

using VoidResult = std::expected<void, Error>;

}  // namespace ava::core
