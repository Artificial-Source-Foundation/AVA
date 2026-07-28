#include "sys.h"
#include "markdown_files.h"

#include <algorithm>
#include <system_error>

namespace ava::app::runtime {

std::vector<std::filesystem::path> markdown_files(std::filesystem::path const& root, std::vector<CommandRegistryDiagnostic>& diagnostics,
                                                  UnifiedCommandSource source)
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

}  // namespace ava::app::runtime
