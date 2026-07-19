#include "sys.h"
#include "ava/core/process_args.h"
#include "ava/core/path.h"

#include <filesystem>
#include <string>

namespace ava::core {
namespace {

std::filesystem::path fallback_root()
{
  return std::filesystem::path{"/"};
}

bool is_within_path(std::filesystem::path const& parent, std::filesystem::path const& candidate)
{
  auto const parent_text = parent.lexically_normal().generic_string();
  auto const candidate_text = candidate.lexically_normal().generic_string();
  if (parent_text.empty())
    return false;
  if (candidate_text == parent_text)
    return true;
  auto const prefix = parent_text.ends_with('/') ? parent_text : parent_text + "/";
  return candidate_text.starts_with(prefix);
}

}  // namespace

bool is_workspace_relative_process_arg(std::string_view value)
{
  if (value.empty())
    return false;
  if (value.starts_with('-'))
  {
    auto const separator = value.find('=');
    if (separator == std::string_view::npos)
      return false;
    value.remove_prefix(separator + 1);
    if (value.empty())
      return false;
  }
  if (value == "." || value == ".." || value.starts_with("./") || value.starts_with("../"))
    return true;
  if (value.find('/') == std::string_view::npos)
    return false;
  return std::filesystem::path(std::string(value)).is_relative();
}

std::filesystem::path safe_global_process_cwd(std::filesystem::path const& config_source, std::filesystem::path const& workspace_dir)
{
  auto cwd = config_source.has_parent_path() ? config_source.parent_path() : fallback_root();
  if (cwd.empty() || cwd == ".")
    cwd = fallback_root();

  auto const cwd_absolute = normalized_absolute_path(cwd);
  if (!workspace_dir.empty())
  {
    auto const workspace_absolute = normalized_absolute_path(workspace_dir);
    if (is_within_path(workspace_absolute, cwd_absolute))
      return fallback_root();
  }
  return cwd_absolute.empty() ? fallback_root() : cwd_absolute;
}

}  // namespace ava::core
