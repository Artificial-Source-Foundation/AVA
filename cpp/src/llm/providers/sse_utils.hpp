#pragma once

#include <optional>
#include <string>
#include <string_view>

namespace ava::llm::providers {

[[nodiscard]] std::string normalize_sse_newlines(std::string_view chunk, bool& pending_carriage_return);
[[nodiscard]] std::optional<std::string> extract_sse_data_line(std::string_view line);

}  // namespace ava::llm::providers
