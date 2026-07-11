#include "sys.h"
#include "ava/context/context_loader.h"
#include "ava/core/error.h"
#include "ava/core/json.h"

#include <algorithm>
#include <array>
#include <fstream>
#include <set>
#include <string_view>

namespace ava::context {
namespace {

constexpr std::array<std::string_view, 4> kContextFileNames{"AGENTS.md", "AGENTS.MD", "CLAUDE.md", "CLAUDE.MD"};

bool is_context_file_name(std::filesystem::path const& path)
{
  auto const filename = path.filename().string();
  return std::ranges::any_of(kContextFileNames, [&](std::string_view candidate) { return filename == candidate; });
}

std::filesystem::path normalized_absolute(std::filesystem::path const& path)
{
  std::error_code error;
  auto normalized = std::filesystem::weakly_canonical(path, error);
  if (!error)
    return normalized.lexically_normal();
  return std::filesystem::absolute(path, error).lexically_normal();
}

bool is_same_or_child(std::filesystem::path const& child, std::filesystem::path const& parent)
{
  auto const child_text = child.lexically_normal().string();
  auto const parent_text = parent.lexically_normal().string();
  if (child_text == parent_text)
    return true;
  if (parent_text == "/")
    return child_text.starts_with('/');
  return child_text.starts_with(parent_text + "/");
}

ava::core::Result<std::string> read_file_limited(std::filesystem::path const& path, std::size_t max_file_bytes)
{
  std::error_code status_error;
  auto const status = std::filesystem::symlink_status(path, status_error);
  if (status_error || std::filesystem::is_symlink(status) || !std::filesystem::is_regular_file(status))
  {
    auto error = ava::core::Error(ava::core::ErrorCategory::Io, "context file is not a regular file");
    error.with_context("path", path.string());
    if (status_error)
      error.with_context("cause", status_error.message());
    return std::unexpected(std::move(error));
  }

  std::error_code size_error;
  auto const size = std::filesystem::file_size(path, size_error);
  if (size_error || size > max_file_bytes)
  {
    auto error = ava::core::Error(ava::core::ErrorCategory::Io, "context file is too large");
    error.with_context("path", path.string());
    error.with_context("max_bytes", std::to_string(max_file_bytes));
    if (size_error)
      error.with_context("cause", size_error.message());
    return std::unexpected(std::move(error));
  }

  std::ifstream file(path, std::ios::binary);
  if (!file)
  {
    auto error = ava::core::Error(ava::core::ErrorCategory::Io, "failed to open context file");
    error.with_context("path", path.string());
    return std::unexpected(std::move(error));
  }

  std::string content;
  std::array<char, 4096> buffer{};
  while (file)
  {
    file.read(buffer.data(), static_cast<std::streamsize>(buffer.size()));
    if (file.gcount() > 0)
      content.append(buffer.data(), static_cast<std::size_t>(file.gcount()));
    if (content.size() > max_file_bytes)
    {
      auto error = ava::core::Error(ava::core::ErrorCategory::Io, "context file is too large");
      error.with_context("path", path.string());
      error.with_context("max_bytes", std::to_string(max_file_bytes));
      return std::unexpected(std::move(error));
    }
  }
  if (!file.eof() && file.fail())
  {
    auto error = ava::core::Error(ava::core::ErrorCategory::Io, "failed while reading context file");
    error.with_context("path", path.string());
    return std::unexpected(std::move(error));
  }
  return content;
}

ava::core::VoidResult append_if_present(std::vector<LoadedContextFile>& files, std::set<std::string>& seen_paths, std::filesystem::path const& path,
                                        ContextSourceType source_type, std::size_t max_file_bytes)
{
  std::error_code status_error;
  auto const status = std::filesystem::symlink_status(path, status_error);
  if (status_error)
    return {};
  if (std::filesystem::is_symlink(status))
  {
    auto error = ava::core::Error(ava::core::ErrorCategory::Io, "context file must not be a symlink");
    error.with_context("path", path.string());
    return std::unexpected(std::move(error));
  }
  if (!std::filesystem::is_regular_file(status))
    return {};
  auto const normalized = normalized_absolute(path);
  if (!seen_paths.insert(normalized.string()).second)
    return {};
  auto content = read_file_limited(path, max_file_bytes);
  if (!content)
    return std::unexpected(content.error());
  files.push_back(LoadedContextFile{.path = normalized, .source_type = source_type, .byte_count = content->size(), .content = std::move(*content)});
  return {};
}

ava::core::VoidResult append_first_context_file_from_dir(std::vector<LoadedContextFile>& files, std::set<std::string>& seen_paths,
                                                         std::filesystem::path const& dir, ContextSourceType source_type, std::size_t max_file_bytes)
{
  auto const initial_size = files.size();
  for (auto const name : kContextFileNames)
  {
    auto appended = append_if_present(files, seen_paths, dir / std::string(name), source_type, max_file_bytes);
    if (!appended)
      return appended;
    if (files.size() != initial_size)
      return {};
  }
  return {};
}

ava::core::VoidResult append_global_context_file(std::vector<LoadedContextFile>& files, std::set<std::string>& seen_paths,
                                                 std::filesystem::path const& global_agents_file, std::size_t max_file_bytes)
{
  auto const initial_size = files.size();
  auto appended = append_if_present(files, seen_paths, global_agents_file, ContextSourceType::Global, max_file_bytes);
  if (!appended || files.size() != initial_size || !is_context_file_name(global_agents_file))
    return appended;

  for (auto const name : kContextFileNames)
  {
    auto const candidate = global_agents_file.parent_path() / std::string(name);
    if (candidate == global_agents_file)
      continue;
    appended = append_if_present(files, seen_paths, candidate, ContextSourceType::Global, max_file_bytes);
    if (!appended)
      return appended;
    if (files.size() != initial_size)
      return {};
  }
  return {};
}

std::vector<std::filesystem::path> context_dirs_root_to_current(std::filesystem::path const& workspace_root, std::filesystem::path const& current_dir)
{
  std::vector<std::filesystem::path> dirs;
  auto const root = normalized_absolute(workspace_root);
  auto const current = normalized_absolute(current_dir.empty() ? workspace_root : current_dir);
  if (!is_same_or_child(current, root))
  {
    dirs.push_back(root);
    return dirs;
  }

  auto cursor = current;
  while (true)
  {
    dirs.push_back(cursor);
    if (cursor == root || cursor == cursor.root_path())
      break;
    cursor = cursor.parent_path();
  }
  std::reverse(dirs.begin(), dirs.end());
  return dirs;
}

}  // namespace

std::string to_string(ContextSourceType source_type)
{
  switch (source_type)
  {
    case ContextSourceType::Workspace:
      return "workspace";
    case ContextSourceType::Global:
      return "global";
  }
  return "unknown";
}

ava::core::Result<std::vector<LoadedContextFile>> load_context_files(ContextLoadOptions const& options)
{
  std::vector<LoadedContextFile> files;
  std::set<std::string> seen_paths;

  for (auto const& dir : context_dirs_root_to_current(options.workspace_root, options.current_dir))
  {
    auto appended = append_first_context_file_from_dir(files, seen_paths, dir, ContextSourceType::Workspace, options.max_file_bytes);
    if (!appended)
      return std::unexpected(appended.error());
  }

  if (!options.global_agents_file.empty())
  {
    auto appended = append_global_context_file(files, seen_paths, options.global_agents_file, options.max_file_bytes);
    if (!appended)
      return std::unexpected(appended.error());
  }

  return files;
}

std::string format_context_for_prompt(std::vector<LoadedContextFile> const& files)
{
  if (files.empty())
    return {};
  std::string output = "\n\n# Loaded Project Instructions\n";
  for (auto const& file : files)
  {
    output += "\n## " + to_string(file.source_type) + ": " + file.path.string() + "\n";
    output += file.content;
    if (!output.ends_with('\n'))
      output += '\n';
  }
  return output;
}

}  // namespace ava::context
