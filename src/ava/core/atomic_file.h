#pragma once

#include "ava/core/result.h"

#include <filesystem>
#include <string_view>

namespace ava::core {

[[nodiscard]] VoidResult write_text_file_atomic(std::filesystem::path const& path, std::string_view body, std::string_view description);

}  // namespace ava::core
