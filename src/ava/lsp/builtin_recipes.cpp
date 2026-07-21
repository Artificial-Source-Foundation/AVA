#include "sys.h"
#include "ava/lsp/bounded_file_reader.h"
#include "ava/lsp/builtin_recipes.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdlib>
#include <fstream>
#include <optional>
#include <string_view>
#include <utility>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

namespace ava::lsp {
namespace {

constexpr std::size_t kMaxRootDepth = 64;
constexpr std::uintmax_t kMaxCargoManifestBytes = 64 * 1024;

bool path_is_within(std::filesystem::path const& candidate, std::filesystem::path const& root)
{
  if (root.empty())
    return false;
  auto const child = std::filesystem::absolute(candidate).lexically_normal().generic_string();
  auto const parent = std::filesystem::absolute(root).lexically_normal().generic_string();
  return child == parent || child.starts_with(parent + '/');
}

bool directory_chain_is_safe(std::filesystem::path const& directory, bool user_owned)
{
  auto const absolute = std::filesystem::absolute(directory).lexically_normal();
  if (!absolute.is_absolute())
    return false;
  std::filesystem::path current("/");
  for (auto const& component : absolute.relative_path())
  {
    current /= component;
    struct stat metadata{};
    if (::lstat(current.c_str(), &metadata) != 0 || !S_ISDIR(metadata.st_mode) || S_ISLNK(metadata.st_mode) ||
        (metadata.st_uid != 0 && metadata.st_uid != ::geteuid()))
    {
      return false;
    }
    bool const root_sticky_namespace = metadata.st_uid == 0 && (metadata.st_mode & S_ISVTX) != 0;
    if ((metadata.st_mode & (S_IWGRP | S_IWOTH)) != 0 && !root_sticky_namespace)
      return false;
  }
  struct stat final_metadata{};
  return ::lstat(absolute.c_str(), &final_metadata) == 0 && (!user_owned || final_metadata.st_uid == ::geteuid());
}

bool has_elf_identity(int fd)
{
  std::array<unsigned char, 4> magic{};
  ssize_t bytes = -1;
  do
  {
    bytes = ::pread(fd, magic.data(), magic.size(), 0);
  } while (bytes < 0 && errno == EINTR);
  return bytes == static_cast<ssize_t>(magic.size()) && magic[0] == 0x7f && magic[1] == 'E' && magic[2] == 'L' && magic[3] == 'F';
}

bool is_rustup_proxy(std::filesystem::path const& candidate, struct stat const& candidate_metadata)
{
  for (auto const* sibling_name : {"rustup", "cargo"})
  {
    struct stat sibling{};
    auto const sibling_path = candidate.parent_path() / sibling_name;
    if (::lstat(sibling_path.c_str(), &sibling) == 0 && !S_ISLNK(sibling.st_mode) && sibling.st_dev == candidate_metadata.st_dev &&
        sibling.st_ino == candidate_metadata.st_ino)
    {
      return true;
    }
  }
  return false;
}

std::optional<ExecutableIdentity> inspect_executable(std::filesystem::path const& candidate, bool user_owned, std::string_view server_id,
                                                     std::filesystem::path const& workspace_root)
{
  auto const absolute = std::filesystem::absolute(candidate).lexically_normal();
  if (path_is_within(absolute, workspace_root) || !directory_chain_is_safe(absolute.parent_path(), user_owned))
    return std::nullopt;

  struct stat path_metadata{};
  if (::lstat(absolute.c_str(), &path_metadata) != 0 || S_ISLNK(path_metadata.st_mode) || !S_ISREG(path_metadata.st_mode) ||
      (user_owned ? path_metadata.st_uid != ::geteuid() : path_metadata.st_uid != 0) || (path_metadata.st_mode & (S_IWGRP | S_IWOTH)) != 0 ||
      (path_metadata.st_mode & (S_IXUSR | S_IXGRP | S_IXOTH)) == 0)
  {
    return std::nullopt;
  }
  if (server_id == "rust-analyzer" && is_rustup_proxy(absolute, path_metadata))
    return std::nullopt;

  int const fd = ::open(absolute.c_str(), O_RDONLY | O_NOFOLLOW | O_NONBLOCK | O_CLOEXEC);
  if (fd < 0)
    return std::nullopt;
  struct stat opened_metadata{};
  bool const valid = ::fstat(fd, &opened_metadata) == 0 && opened_metadata.st_dev == path_metadata.st_dev && opened_metadata.st_ino == path_metadata.st_ino &&
                     has_elf_identity(fd);
  ::close(fd);
  if (!valid)
    return std::nullopt;

  return ExecutableIdentity{
      .canonical_path = absolute,
      .device = static_cast<std::uintmax_t>(opened_metadata.st_dev),
      .inode = static_cast<std::uintmax_t>(opened_metadata.st_ino),
      .size = static_cast<std::uintmax_t>(opened_metadata.st_size),
      .changed_seconds = static_cast<std::int64_t>(opened_metadata.st_ctim.tv_sec),
      .changed_nanoseconds = static_cast<std::int64_t>(opened_metadata.st_ctim.tv_nsec),
  };
}

std::vector<std::filesystem::path> default_system_directories()
{
  return {"/usr/local/bin", "/usr/bin", "/bin"};
}

std::vector<std::filesystem::path> default_user_directories()
{
  auto const* home_value = std::getenv("HOME");
  if (home_value == nullptr || *home_value == '\0')
    return {};
  auto const home = std::filesystem::path(home_value);
  if (!home.is_absolute())
    return {};
  return {home / ".local" / "bin", home / "go" / "bin", home / ".cargo" / "bin"};
}

bool marker_exists(std::filesystem::path const& marker, std::filesystem::path const& workspace_root)
{
  auto content = read_bounded_lsp_file(BoundedFileReadOptions{
      .path = marker,
      .workspace_root = workspace_root,
      .max_bytes = 0,
      .scope = BoundedFileReadScope::Workspace,
      .missing_ok = true,
      .metadata_only = true,
  });
  return content && content->has_value();
}

std::optional<std::string> marker_content(std::filesystem::path const& marker, std::filesystem::path const& workspace_root)
{
  auto content = read_bounded_lsp_file(BoundedFileReadOptions{
      .path = marker,
      .workspace_root = workspace_root,
      .max_bytes = kMaxCargoManifestBytes,
      .scope = BoundedFileReadScope::Workspace,
      .missing_ok = true,
  });
  if (!content || !*content)
    return std::nullopt;
  return std::move(**content);
}

bool cargo_workspace_manifest(std::string_view content)
{
  std::size_t offset = 0;
  while (offset <= content.size())
  {
    auto const end = content.find('\n', offset);
    auto line = content.substr(offset, end == std::string_view::npos ? content.size() - offset : end - offset);
    while (!line.empty() && std::isspace(static_cast<unsigned char>(line.front()))) line.remove_prefix(1);
    while (!line.empty() && std::isspace(static_cast<unsigned char>(line.back()))) line.remove_suffix(1);
    if (line == "[workspace]")
      return true;
    if (end == std::string_view::npos)
      break;
    offset = end + 1;
  }
  return false;
}

std::vector<std::filesystem::path> ancestors_for_document(std::filesystem::path const& document_path, std::filesystem::path const& workspace_root)
{
  auto const workspace = std::filesystem::absolute(workspace_root).lexically_normal();
  auto current = std::filesystem::absolute(document_path).lexically_normal().parent_path();
  if (!path_is_within(current, workspace))
    return {workspace};
  std::vector<std::filesystem::path> ancestors;
  for (std::size_t depth = 0; depth < kMaxRootDepth; ++depth)
  {
    ancestors.push_back(current);
    if (current == workspace)
      break;
    auto const parent = current.parent_path();
    if (parent == current || !path_is_within(parent, workspace))
      break;
    current = parent;
  }
  if (ancestors.empty() || ancestors.back() != workspace)
    return {workspace};
  return ancestors;
}

}  // namespace

std::vector<BuiltinServerRecipe> builtin_server_recipes()
{
  return {
      {.id = "clangd",
       .arguments = {"--background-index"},
       .file_extensions = {".c", ".cc", ".cpp", ".cxx", ".h", ".hh", ".hpp", ".hxx", ".m", ".mm"},
       .language_id = "cpp"},
      {.id = "gopls", .arguments = {}, .file_extensions = {".go"}, .language_id = "go"},
      {.id = "rust-analyzer", .arguments = {}, .file_extensions = {".rs"}, .language_id = "rust"},
  };
}

std::vector<BuiltinServerInspection> inspect_builtin_servers(std::vector<std::string> const& enabled_ids, std::filesystem::path const& workspace_root,
                                                             BuiltinDiscoveryOptions const& options)
{
  auto system_directories = options.system_directories;
  auto user_directories = options.user_directories;
  if (options.use_default_search_directories)
  {
    auto defaults = default_system_directories();
    system_directories.insert(system_directories.end(), defaults.begin(), defaults.end());
    auto user_defaults = default_user_directories();
    user_directories.insert(user_directories.end(), user_defaults.begin(), user_defaults.end());
  }

  std::vector<BuiltinServerInspection> result;
  for (auto const& recipe : builtin_server_recipes())
  {
    if (std::ranges::find(enabled_ids, recipe.id) == enabled_ids.end())
    {
      result.push_back({.id = recipe.id, .status = BuiltinServerStatus::Disabled, .reason = "not_enabled"});
      continue;
    }

    bool unsafe_candidate = false;
    std::optional<ExecutableIdentity> executable;
    auto inspect_directories = [&](std::vector<std::filesystem::path> const& directories, bool user_owned) {
      for (auto const& directory : directories)
      {
        auto const candidate = directory / recipe.id;
        struct stat metadata{};
        if (::lstat(candidate.c_str(), &metadata) != 0)
          continue;
        auto inspected = inspect_executable(candidate, user_owned, recipe.id, workspace_root);
        if (inspected)
        {
          executable = std::move(*inspected);
          return;
        }
        unsafe_candidate = true;
      }
    };
    inspect_directories(system_directories, false);
    if (!executable)
      inspect_directories(user_directories, true);

    if (executable)
    {
      result.push_back({.id = recipe.id, .status = BuiltinServerStatus::Available, .reason = "installed", .executable = std::move(executable)});
    }
    else if (unsafe_candidate)
    {
      result.push_back({.id = recipe.id, .status = BuiltinServerStatus::Unsafe, .reason = "unsafe_install"});
    }
    else
    {
      result.push_back({.id = recipe.id, .status = BuiltinServerStatus::NotFound, .reason = "not_found"});
    }
  }
  return result;
}

std::filesystem::path select_builtin_server_root(std::string_view server_id, std::filesystem::path const& document_path,
                                                 std::filesystem::path const& workspace_root)
{
  auto const workspace = std::filesystem::absolute(workspace_root).lexically_normal();
  auto const ancestors = ancestors_for_document(document_path, workspace);
  if (server_id == "clangd")
  {
    for (auto const& ancestor : ancestors)
    {
      for (auto const* marker : {"compile_commands.json", "compile_flags.txt", ".clangd"})
      {
        if (marker_exists(ancestor / marker, workspace))
          return ancestor;
      }
    }
  }
  else if (server_id == "gopls")
  {
    for (auto const& ancestor : ancestors)
    {
      if (marker_exists(ancestor / "go.work", workspace))
        return ancestor;
    }
    for (auto const& ancestor : ancestors)
    {
      if (marker_exists(ancestor / "go.mod", workspace) || marker_exists(ancestor / "go.sum", workspace))
        return ancestor;
    }
  }
  else if (server_id == "rust-analyzer")
  {
    std::optional<std::filesystem::path> nearest_cargo_root;
    for (auto const& ancestor : ancestors)
    {
      auto const manifest_path = ancestor / "Cargo.toml";
      if (auto content = marker_content(manifest_path, workspace))
      {
        if (!nearest_cargo_root)
          nearest_cargo_root = ancestor;
        if (cargo_workspace_manifest(*content))
          return ancestor;
      }
      else if (!nearest_cargo_root && marker_exists(ancestor / "Cargo.lock", workspace))
      {
        nearest_cargo_root = ancestor;
      }
    }
    if (nearest_cargo_root)
      return *nearest_cargo_root;
  }
  return workspace;
}

std::string_view to_string(BuiltinServerStatus status) noexcept
{
  switch (status)
  {
    case BuiltinServerStatus::Disabled:
      return "disabled";
    case BuiltinServerStatus::Available:
      return "available";
    case BuiltinServerStatus::NotFound:
      return "not-found";
    case BuiltinServerStatus::Unsafe:
      return "unsafe";
  }
  return "unsafe";
}

}  // namespace ava::lsp
