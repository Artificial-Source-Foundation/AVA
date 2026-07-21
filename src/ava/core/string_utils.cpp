#include "sys.h"
#include "string_utils.h"
#include <cctype>

namespace ava::core {

std::string_view trim_view(std::string_view value)
{
  while (!value.empty() && std::isspace(static_cast<unsigned char>(value.front())) != 0) value.remove_prefix(1);
  while (!value.empty() && std::isspace(static_cast<unsigned char>(value.back())) != 0) value.remove_suffix(1);
  return value;
}

std::string trim(std::string_view value)
{
  auto const trimmed = trim_view(value);
  return std::string(trimmed);
}

std::string_view strip_matching_quotes(std::string_view value)
{
  if (value.size() >= 2 &&
      ((value.front() == '"' && value.back() == '"') ||
       (value.front() == '\'' && value.back() == '\'')))
    value.remove_prefix(1), value.remove_suffix(1);

  return value;
}

} // namespace ava::core
