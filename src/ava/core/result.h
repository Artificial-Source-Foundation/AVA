#pragma once

#include <expected>

#include "ava/core/error.h"

namespace ava::core {

template <typename T>
using Result = std::expected<T, Error>;

using VoidResult = std::expected<void, Error>;

}  // namespace ava::core
