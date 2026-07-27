#include "sys.h"
#include "command_names.h"

#include <cctype>
#include <system_error>

namespace ava::app::runtime {

bool valid_command_segment(std::string_view segment)
{
  if (segment.empty() || segment.size() > 128)
    return false;
  bool last_was_separator = false;
  for (char const ch : segment)
  {
    auto const byte = static_cast<unsigned char>(ch);
    bool const allowed = std::isalnum(byte) != 0 || ch == '.' || ch == '_' || ch == '-';
    if (!allowed)
      return false;
    bool const separator = ch == '.' || ch == '_' || ch == '-';
    if (separator && last_was_separator)
      return false;
    last_was_separator = separator;
  }
  return !last_was_separator;
}

bool valid_prompt_command_name(std::string_view name)
{
  if (name.empty() || name.size() > kMaxCommandTokenBytes - 1)
    return false;
  std::size_t start = 0;
  while (start <= name.size())
  {
    auto const slash = name.find('/', start);
    auto const end = slash == std::string_view::npos ? name.size() : slash;
    if (!valid_command_segment(name.substr(start, end - start)))
      return false;
    if (slash == std::string_view::npos)
      break;
    start = slash + 1;
  }
  return true;
}

std::optional<std::string> command_name_for_file(std::filesystem::path const& root, std::filesystem::path const& file)
{
  std::error_code relative_error;
  auto relative = std::filesystem::relative(file, root, relative_error);
  if (relative_error || relative.empty())
    return std::nullopt;
  relative.replace_extension();
  auto name = relative.generic_string();
  if (!valid_prompt_command_name(name))
    return std::nullopt;
  return name;
}

}  // namespace ava::app::runtime
