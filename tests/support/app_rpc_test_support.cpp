#include "sys.h"
#include "tests/support/app_rpc_test_support.h"

#include <optional>
#include <string>
#include <string_view>

namespace ava::tests {

std::optional<std::string> rpc_string_field_from_output(std::string_view jsonl, std::string_view field)
{
  auto const key = "\"" + std::string(field) + "\":\"";
  auto const start = jsonl.find(key);
  if (start == std::string_view::npos)
    return std::nullopt;
  auto value_start = start + key.size();
  auto const end = jsonl.find('"', value_start);
  if (end == std::string_view::npos)
    return std::nullopt;
  return std::string(jsonl.substr(value_start, end - value_start));
}

}  // namespace ava::tests
