#include "sys.h"
#include "command_names.h"

#include <array>
#include <cctype>
#include <fstream>
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

ava::core::Result<std::string> read_bounded_file(std::filesystem::path const& path, std::size_t max_bytes)
{
  std::error_code status_error;
  auto const status = std::filesystem::symlink_status(path, status_error);
  if (status_error || std::filesystem::is_symlink(status) || !std::filesystem::is_regular_file(status))
  {
    auto error = ava::core::Error(ava::core::ErrorCategory::Io, "command file is not a regular file");
    error.with_context("path", path.string());
    if (status_error)
      error.with_context("cause", status_error.message());
    return std::unexpected(std::move(error));
  }

  std::error_code size_error;
  auto const size = std::filesystem::file_size(path, size_error);
  if (size_error || size > max_bytes)
  {
    auto error = ava::core::Error(ava::core::ErrorCategory::Io, "command file is too large");
    error.with_context("path", path.string());
    error.with_context("max_bytes", std::to_string(max_bytes));
    if (size_error)
      error.with_context("cause", size_error.message());
    return std::unexpected(std::move(error));
  }

  std::ifstream file(path, std::ios::binary);
  if (!file)
  {
    return std::unexpected(ava::core::Error(ava::core::ErrorCategory::Io, "failed to open command file").with_context("path", path.string()));
  }

  std::string content;
  std::array<char, 4096> buffer{};
  while (file)
  {
    file.read(buffer.data(), static_cast<std::streamsize>(buffer.size()));
    if (file.gcount() > 0)
      content.append(buffer.data(), static_cast<std::size_t>(file.gcount()));
    if (content.size() > max_bytes)
    {
      auto error = ava::core::Error(ava::core::ErrorCategory::Io, "command file is too large");
      error.with_context("path", path.string());
      error.with_context("max_bytes", std::to_string(max_bytes));
      return std::unexpected(std::move(error));
    }
  }
  if (!file.eof() && file.fail())
  {
    return std::unexpected(ava::core::Error(ava::core::ErrorCategory::Io, "failed while reading command file").with_context("path", path.string()));
  }
  return content;
}

} // namespace ava::app::runtime
