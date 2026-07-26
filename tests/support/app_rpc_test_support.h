#pragma once

#include <optional>
#include <string>
#include <string_view>

namespace ava::tests {

std::optional<std::string> rpc_string_field_from_output(std::string_view jsonl, std::string_view field);

}  // namespace ava::tests
