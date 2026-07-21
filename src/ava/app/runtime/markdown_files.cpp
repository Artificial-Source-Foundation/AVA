#include "sys.h"
#include "markdown_files.h"
#include "ava/core/string_utils.h"
#include <algorithm>
#include <system_error>

namespace ava::app::runtime {

ParsedMarkdown parse_markdown(std::string_view content)
{
  ParsedMarkdown parsed;
  if (!(content.starts_with("---\n") || content.starts_with("---\r\n")))
  {
    parsed.body = std::string(content);
    return parsed;
  }

  auto const body_start = content.starts_with("---\r\n") ? 5 : 4;
  auto const delimiter = content.find("\n---", body_start);
  if (delimiter == std::string_view::npos)
  {
    parsed.body = std::string(content);
    return parsed;
  }

  auto const frontmatter = content.substr(body_start, delimiter - body_start);
  std::size_t line_start = 0;
  while (line_start <= frontmatter.size())
  {
    auto const line_end = frontmatter.find('\n', line_start);
    auto line = frontmatter.substr(line_start, line_end == std::string_view::npos ? std::string_view::npos : line_end - line_start);
    if (!line.empty() && line.back() == '\r')
      line.remove_suffix(1);
    if (auto const colon = line.find(':'); colon != std::string_view::npos)
    {
      auto key = core::trim(line.substr(0, colon));
      auto value = core::strip_matching_quotes(core::trim_view(line.substr(colon + 1)));
      if (!key.empty())
        parsed.frontmatter[std::move(key)] = value;
    }
    if (line_end == std::string_view::npos)
      break;
    line_start = line_end + 1;
  }

  auto after = delimiter + 4;
  if (after < content.size() && content[after] == '\r')
    ++after;
  if (after < content.size() && content[after] == '\n')
    ++after;
  parsed.body = std::string(content.substr(after));
  return parsed;
}

std::string markdown_field(ParsedMarkdown const& markdown, std::string_view name)
{
  auto const it = markdown.frontmatter.find(std::string(name));
  if (it == markdown.frontmatter.end())
    return {};
  return it->second;
}

std::vector<std::filesystem::path> markdown_files(std::filesystem::path const& root, std::vector<CommandRegistryDiagnostic>& diagnostics, UnifiedCommandSource source)
{
  std::vector<std::filesystem::path> files;
  if (root.empty())
    return files;
  std::error_code exists_error;
  if (!std::filesystem::exists(root, exists_error))
    return files;
  if (exists_error)
  {
    diagnostics.push_back(CommandRegistryDiagnostic{.source = to_string(source), .path = root, .message = "failed to inspect command directory"});
    return files;
  }
  std::error_code directory_error;
  if (!std::filesystem::is_directory(root, directory_error) || directory_error)
  {
    diagnostics.push_back(CommandRegistryDiagnostic{.source = to_string(source), .path = root, .message = "command path is not a directory"});
    return files;
  }

  std::error_code iter_error;
  for (std::filesystem::recursive_directory_iterator it(root, std::filesystem::directory_options::skip_permission_denied, iter_error), end;
       !iter_error && it != end; it.increment(iter_error))
  {
    std::error_code entry_error;
    if (it->is_symlink(entry_error) || entry_error)
      continue;
    if (!it->is_regular_file(entry_error) || entry_error)
      continue;
    if (it->path().extension() != ".md")
      continue;
    files.push_back(it->path());
  }
  if (iter_error)
  {
    diagnostics.push_back(CommandRegistryDiagnostic{.source = to_string(source), .path = root, .message = "failed to iterate command directory"});
  }
  std::ranges::sort(files);
  return files;
}

} // namespace ava::app::runtime
