#pragma once

#include <cstddef>
#include <string>

namespace ava::tools {

[[nodiscard]] std::string apply_output_fallback(
    const std::string& tool_name,
    const std::string& content,
    std::size_t max_bytes
);

}  // namespace ava::tools
