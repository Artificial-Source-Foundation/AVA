#include "sys.h"
#include "ava/command/discovery.h"
#include "ava/command/intent_internal.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cerrno>
#include <cstring>
#include <optional>
#include <set>
#include <string>
#include <utility>
#include <vector>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

namespace ava::command::detail {
namespace {

#ifndef O_PATH
#define O_PATH O_RDONLY
#endif

class UniqueFd final
{
 public:
  explicit UniqueFd(int fd = -1) noexcept : fd_(fd) { }
  ~UniqueFd()
  {
    if (fd_ >= 0)
      ::close(fd_);
  }
  UniqueFd(UniqueFd const&) = delete;
  UniqueFd& operator=(UniqueFd const&) = delete;
  UniqueFd(UniqueFd&& other) noexcept : fd_(std::exchange(other.fd_, -1)) { }
  UniqueFd& operator=(UniqueFd&& other) noexcept
  {
    if (this != &other)
    {
      if (fd_ >= 0)
        ::close(fd_);
      fd_ = std::exchange(other.fd_, -1);
    }
    return *this;
  }

  [[nodiscard]] int get() const noexcept { return fd_; }

  AVA_DEBUG_PRINT_MEMBERS_ON

 private:
  int fd_ = -1;
};

enum class ExpectedNode
{
  Any,
  Directory,
  Regular,
};

bool is_within(std::filesystem::path const& child, std::filesystem::path const& parent)
{
  std::error_code error;
  auto const relative = std::filesystem::relative(child, parent, error);
  if (error)
    return false;
  if (relative.empty() || relative == ".")
    return true;
  auto const begin = relative.begin();
  return begin != relative.end() && *begin != "..";
}

bool mode_is_safe(struct stat const& status)
{
  return (status.st_mode & (S_IWGRP | S_IWOTH)) == 0;
}

bool owner_is_safe(struct stat const& status, bool require_current_user)
{
  if (require_current_user)
    return status.st_uid == geteuid();
  return status.st_uid == geteuid() || status.st_uid == 0;
}

bool is_sticky_directory(struct stat const& status)
{
  return S_ISDIR(status.st_mode) && (status.st_mode & S_ISVTX) != 0;
}

ava::core::Result<std::filesystem::path> normalized_absolute(std::filesystem::path const& input, std::string_view label)
{
  if (input.empty() || has_forbidden_path_byte(input))
  {
    return std::unexpected(
        command_error(ava::core::ErrorCategory::InvalidArgument, std::string(label) + " must not be empty or contain control bytes", "path", input.string()));
  }
  std::error_code error;
  auto absolute = std::filesystem::absolute(input, error);
  if (error)
  {
    auto result = command_error(ava::core::ErrorCategory::Io, "failed to make " + std::string(label) + " absolute", "path", input.string());
    result.with_context("cause", error.message());
    return std::unexpected(std::move(result));
  }
  return absolute.lexically_normal();
}

ava::core::Result<struct stat> metadata_stat(std::filesystem::path const& path, std::string_view label)
{
  // O_PATH|O_NOFOLLOW obtains only metadata and cannot block on a FIFO. It is
  // deliberately used for every untrusted pathname inspection in this module.
  UniqueFd fd(::open(path.c_str(), O_PATH | O_NOFOLLOW | O_NONBLOCK | O_CLOEXEC));
  if (fd.get() < 0)
  {
    auto result = command_error(errno == ENOENT ? ava::core::ErrorCategory::NotFound : ava::core::ErrorCategory::Io, "failed to inspect " + std::string(label),
                                "path", path.string());
    result.with_context("cause", std::strerror(errno));
    return std::unexpected(std::move(result));
  }
  struct stat status{};
  if (::fstat(fd.get(), &status) != 0)
  {
    auto result = command_error(ava::core::ErrorCategory::Io, "failed to read metadata for " + std::string(label), "path", path.string());
    result.with_context("cause", std::strerror(errno));
    return std::unexpected(std::move(result));
  }
  return status;
}

PathAncestorMetadata ancestor_metadata_from(std::filesystem::path path, struct stat const& status)
{
  return PathAncestorMetadata{.path = std::move(path),
                              .device = static_cast<std::uintmax_t>(status.st_dev),
                              .inode = static_cast<std::uintmax_t>(status.st_ino),
                              .mode = static_cast<std::uintmax_t>(status.st_mode),
                              .owner = static_cast<std::uintmax_t>(status.st_uid),
                              .link_count = static_cast<std::uintmax_t>(status.st_nlink),
                              .changed_seconds = static_cast<std::int64_t>(status.st_ctim.tv_sec),
                              .changed_nanoseconds = static_cast<std::int64_t>(status.st_ctim.tv_nsec),
                              .is_symlink = S_ISLNK(status.st_mode),
                              // Root-owned sticky namespaces such as /tmp are
                              // shared by unrelated processes, so bind their
                              // safety properties but not volatile identity
                              // metadata. Other sticky directories stay bound.
                              .identity_bound = !(is_sticky_directory(status) && status.st_uid == 0)};
}

ava::core::Result<std::vector<PathAncestorMetadata>> capture_ancestor_metadata(std::filesystem::path const& requested, std::filesystem::path const& canonical,
                                                                               std::string_view label)
{
  std::vector<PathAncestorMetadata> result;
  std::set<std::filesystem::path> seen;
  auto const capture_parents = [&result, &seen, label](std::filesystem::path const& path) -> ava::core::VoidResult {
    std::filesystem::path current("/");
    for (auto const& component : path.parent_path().relative_path())
    {
      current /= component;
      if (!seen.insert(current).second)
        continue;
      auto status = metadata_stat(current, label);
      if (!status)
        return std::unexpected(std::move(status.error()));
      result.push_back(ancestor_metadata_from(current, *status));
    }
    return {};
  };
  if (auto captured = capture_parents(requested); !captured)
    return std::unexpected(std::move(captured.error()));
  if (auto captured = capture_parents(canonical); !captured)
    return std::unexpected(std::move(captured.error()));
  return result;
}

PathMetadata metadata_from(std::filesystem::path requested, std::filesystem::path canonical, struct stat const& requested_status,
                           struct stat const& canonical_status, std::vector<PathAncestorMetadata> ancestors)
{
  return PathMetadata{.requested_path = std::move(requested),
                      .canonical_path = std::move(canonical),
                      .device = static_cast<std::uintmax_t>(canonical_status.st_dev),
                      .inode = static_cast<std::uintmax_t>(canonical_status.st_ino),
                      .mode = static_cast<std::uintmax_t>(canonical_status.st_mode),
                      .size = static_cast<std::uintmax_t>(canonical_status.st_size),
                      .owner = static_cast<std::uintmax_t>(canonical_status.st_uid),
                      .link_count = static_cast<std::uintmax_t>(canonical_status.st_nlink),
                      .changed_seconds = static_cast<std::int64_t>(canonical_status.st_ctim.tv_sec),
                      .changed_nanoseconds = static_cast<std::int64_t>(canonical_status.st_ctim.tv_nsec),
                      .requested_path_is_symlink = S_ISLNK(requested_status.st_mode),
                      .requested_device = static_cast<std::uintmax_t>(requested_status.st_dev),
                      .requested_inode = static_cast<std::uintmax_t>(requested_status.st_ino),
                      .requested_mode = static_cast<std::uintmax_t>(requested_status.st_mode),
                      .requested_owner = static_cast<std::uintmax_t>(requested_status.st_uid),
                      .requested_link_count = static_cast<std::uintmax_t>(requested_status.st_nlink),
                      .requested_changed_seconds = static_cast<std::int64_t>(requested_status.st_ctim.tv_sec),
                      .requested_changed_nanoseconds = static_cast<std::int64_t>(requested_status.st_ctim.tv_nsec),
                      .ancestor_metadata = std::move(ancestors)};
}

ava::core::Result<PathMetadata> inspect_path_metadata(std::filesystem::path const& input, ExpectedNode expected, bool reject_final_symlink,
                                                      std::string_view label)
{
  auto requested = normalized_absolute(input, label);
  if (!requested)
    return std::unexpected(std::move(requested.error()));
  auto requested_status = metadata_stat(*requested, label);
  if (!requested_status)
    return std::unexpected(std::move(requested_status.error()));
  if (reject_final_symlink && S_ISLNK(requested_status->st_mode))
  {
    return std::unexpected(
        command_error(ava::core::ErrorCategory::PermissionDenied, std::string(label) + " must not be a final-component symlink", "path", requested->string()));
  }

  std::error_code canonical_error;
  auto canonical = std::filesystem::canonical(*requested, canonical_error);
  if (canonical_error)
  {
    auto result = command_error(ava::core::ErrorCategory::Io, "failed to canonicalize " + std::string(label), "path", requested->string());
    result.with_context("cause", canonical_error.message());
    return std::unexpected(std::move(result));
  }
  auto canonical_status = metadata_stat(canonical, label);
  if (!canonical_status)
    return std::unexpected(std::move(canonical_status.error()));
  if ((expected == ExpectedNode::Directory && !S_ISDIR(canonical_status->st_mode)) ||
      (expected == ExpectedNode::Regular && !S_ISREG(canonical_status->st_mode)))
  {
    return std::unexpected(
        command_error(ava::core::ErrorCategory::InvalidArgument, std::string(label) + " has an unexpected file type", "path", canonical.string()));
  }
  auto ancestors = capture_ancestor_metadata(*requested, canonical, "path ancestor");
  if (!ancestors)
    return std::unexpected(std::move(ancestors.error()));
  return metadata_from(std::move(*requested), std::move(canonical), *requested_status, *canonical_status, std::move(*ancestors));
}

ava::core::VoidResult validate_safe_ancestors(std::vector<PathAncestorMetadata> const& ancestors)
{
  for (auto const& ancestor : ancestors)
  {
    struct stat status{};
    status.st_mode = static_cast<mode_t>(ancestor.mode);
    status.st_uid = static_cast<uid_t>(ancestor.owner);
    status.st_nlink = static_cast<nlink_t>(ancestor.link_count);
    if (ancestor.is_symlink)
    {
      if (!S_ISLNK(status.st_mode) || !owner_is_safe(status, false) || status.st_nlink != 1)
      {
        return std::unexpected(command_error(ava::core::ErrorCategory::PermissionDenied, "command path symlink ancestor has an unsafe owner or link count",
                                             "path", ancestor.path.string()));
      }
      continue;
    }
    if (!S_ISDIR(status.st_mode))
      return std::unexpected(
          command_error(ava::core::ErrorCategory::PermissionDenied, "command path ancestor is not a directory", "path", ancestor.path.string()));
    if (!owner_is_safe(status, false))
      return std::unexpected(
          command_error(ava::core::ErrorCategory::PermissionDenied, "command path ancestor has an unsafe owner", "path", ancestor.path.string()));
    // A sticky shared directory (for example /tmp) is an accepted namespace
    // boundary. Every descendant used as an execution root remains checked.
    if (!mode_is_safe(status) && !is_sticky_directory(status))
      return std::unexpected(
          command_error(ava::core::ErrorCategory::PermissionDenied, "command path ancestor is group/world writable", "path", ancestor.path.string()));
  }
  return {};
}

ava::core::VoidResult validate_safe_final_symlink_identity(std::filesystem::path const& path, bool is_symlink, std::uintmax_t mode, std::uintmax_t owner,
                                                           std::uintmax_t link_count, std::string_view label)
{
  if (!is_symlink)
    return {};
  struct stat status{};
  status.st_mode = static_cast<mode_t>(mode);
  status.st_uid = static_cast<uid_t>(owner);
  status.st_nlink = static_cast<nlink_t>(link_count);
  if (!S_ISLNK(status.st_mode) || !owner_is_safe(status, false) || status.st_nlink != 1)
  {
    return std::unexpected(
        command_error(ava::core::ErrorCategory::PermissionDenied, std::string(label) + " symlink has an unsafe owner or link count", "path", path.string()));
  }
  return {};
}

ava::core::VoidResult validate_safe_final_symlink(PathMetadata const& metadata, std::string_view label)
{
  return validate_safe_final_symlink_identity(metadata.requested_path, metadata.requested_path_is_symlink, metadata.requested_mode, metadata.requested_owner,
                                              metadata.requested_link_count, label);
}

ava::core::VoidResult validate_safe_directory(PathMetadata const& metadata, bool require_current_user, std::string_view label)
{
  struct stat status{};
  status.st_mode = static_cast<mode_t>(metadata.mode);
  status.st_uid = static_cast<uid_t>(metadata.owner);
  if (!S_ISDIR(status.st_mode) || !mode_is_safe(status) || !owner_is_safe(status, require_current_user))
  {
    return std::unexpected(command_error(ava::core::ErrorCategory::PermissionDenied,
                                         std::string(label) + " has unsafe ownership or group/world-writable permissions", "path",
                                         metadata.canonical_path.string()));
  }
  if (auto valid = validate_safe_final_symlink(metadata, label); !valid)
    return std::unexpected(std::move(valid.error()));
  return validate_safe_ancestors(metadata.ancestor_metadata);
}

ava::core::VoidResult validate_synthetic_environment_directory(PathMetadata const& metadata, std::string_view label)
{
  struct stat status{};
  status.st_mode = static_cast<mode_t>(metadata.mode);
  status.st_uid = static_cast<uid_t>(metadata.owner);
  if (!S_ISDIR(status.st_mode) || !owner_is_safe(status, true) || (status.st_mode & (S_IRWXG | S_IRWXO)) != 0)
  {
    return std::unexpected(command_error(ava::core::ErrorCategory::PermissionDenied, std::string(label) + " must be an owner-owned 0700-style directory",
                                         "path", metadata.canonical_path.string()));
  }
  return validate_safe_ancestors(metadata.ancestor_metadata);
}

ava::core::VoidResult validate_safe_recipe_path(PathMetadata const& metadata)
{
  struct stat status{};
  status.st_mode = static_cast<mode_t>(metadata.mode);
  status.st_uid = static_cast<uid_t>(metadata.owner);
  status.st_nlink = static_cast<nlink_t>(metadata.link_count);
  if (metadata.requested_path_is_symlink || (!S_ISDIR(status.st_mode) && !S_ISREG(status.st_mode)) || !mode_is_safe(status) || !owner_is_safe(status, false) ||
      (S_ISREG(status.st_mode) && status.st_nlink != 1))
  {
    return std::unexpected(command_error(ava::core::ErrorCategory::PermissionDenied,
                                         "recipe path argument has unsafe ownership, mode, link count, or symlink provenance", "path",
                                         metadata.canonical_path.string()));
  }
  return validate_safe_ancestors(metadata.ancestor_metadata);
}

ava::core::VoidResult validate_safe_executable(ExecutableMetadata const& metadata)
{
  struct stat status{};
  status.st_mode = static_cast<mode_t>(metadata.mode);
  status.st_uid = static_cast<uid_t>(metadata.owner);
  if (!S_ISREG(status.st_mode) || (status.st_mode & (S_IXUSR | S_IXGRP | S_IXOTH)) == 0)
  {
    return std::unexpected(command_error(ava::core::ErrorCategory::InvalidArgument, "resolved command is not a regular executable file", "path",
                                         metadata.canonical_path.string()));
  }
  if (!mode_is_safe(status) || !owner_is_safe(status, false) || metadata.link_count != 1)
  {
    return std::unexpected(command_error(ava::core::ErrorCategory::PermissionDenied, "resolved command has unsafe owner, writable mode, or link count", "path",
                                         metadata.canonical_path.string()));
  }
  if (auto valid = validate_safe_final_symlink_identity(metadata.requested_path, metadata.requested_path_is_symlink, metadata.requested_mode,
                                                        metadata.requested_owner, metadata.requested_link_count, "executable");
      !valid)
  {
    return std::unexpected(std::move(valid.error()));
  }
  return validate_safe_ancestors(metadata.ancestor_metadata);
}

ExecutableMetadata executable_from(PathMetadata const& metadata)
{
  return ExecutableMetadata{.requested_path = metadata.requested_path,
                            .canonical_path = metadata.canonical_path,
                            .device = metadata.device,
                            .inode = metadata.inode,
                            .mode = metadata.mode,
                            .size = metadata.size,
                            .owner = metadata.owner,
                            .link_count = metadata.link_count,
                            .changed_seconds = metadata.changed_seconds,
                            .changed_nanoseconds = metadata.changed_nanoseconds,
                            .requested_path_is_symlink = metadata.requested_path_is_symlink,
                            .requested_device = metadata.requested_device,
                            .requested_inode = metadata.requested_inode,
                            .requested_mode = metadata.requested_mode,
                            .requested_owner = metadata.requested_owner,
                            .requested_link_count = metadata.requested_link_count,
                            .requested_changed_seconds = metadata.requested_changed_seconds,
                            .requested_changed_nanoseconds = metadata.requested_changed_nanoseconds,
                            .ancestor_metadata = metadata.ancestor_metadata};
}

ava::core::Result<ExecutableMetadata> executable_metadata(std::filesystem::path const& requested)
{
  auto metadata = inspect_path_metadata(requested, ExpectedNode::Regular, false, "executable");
  if (!metadata)
    return std::unexpected(std::move(metadata.error()));
  auto executable = executable_from(*metadata);
  if (auto valid = validate_safe_executable(executable); !valid)
    return std::unexpected(std::move(valid.error()));
  return executable;
}

std::vector<std::string_view> split_path(std::string_view path)
{
  std::vector<std::string_view> entries;
  std::size_t start = 0;
  while (true)
  {
    auto const separator = path.find(':', start);
    entries.push_back(path.substr(start, separator == std::string_view::npos ? std::string_view::npos : separator - start));
    if (separator == std::string_view::npos)
      break;
    start = separator + 1;
  }
  return entries;
}

ava::core::Result<CommandPathEntry> safe_path_directory(std::filesystem::path const& requested, PathProvenance provenance)
{
  if (requested.empty() || !requested.is_absolute())
  {
    return std::unexpected(
        command_error(ava::core::ErrorCategory::InvalidArgument, "PATH entry must be an absolute non-empty directory", "path", requested.string()));
  }
  if (has_forbidden_path_byte(requested) || requested.string().find(':') != std::string::npos)
  {
    return std::unexpected(command_error(ava::core::ErrorCategory::InvalidArgument, "PATH entry contains unsafe bytes", "path", requested.string()));
  }
  bool const startup = provenance == PathProvenance::StartupPath;
  auto metadata = inspect_path_metadata(requested, ExpectedNode::Directory, !startup, "PATH entry");
  if (!metadata)
    return std::unexpected(std::move(metadata.error()));
  bool const require_current_user = !startup;
  if (auto valid = validate_safe_directory(*metadata, require_current_user, "PATH directory"); !valid)
    return std::unexpected(std::move(valid.error()));
  return CommandPathEntry{.directory = metadata->canonical_path, .provenance = provenance, .metadata = std::move(*metadata)};
}

void append_unique_path(std::vector<CommandPathEntry>& entries, CommandPathEntry entry)
{
  auto const found = std::ranges::find_if(entries, [&entry](CommandPathEntry const& existing) { return existing.directory == entry.directory; });
  if (found == entries.end())
    entries.push_back(std::move(entry));
}

ava::core::Result<std::vector<CommandPathEntry>> discover_path(CommandBuildOptions const& options, std::filesystem::path const& workspace,
                                                               std::filesystem::path const& trusted_home)
{
  std::optional<std::string> startup_path = options.startup_path;
  if (!startup_path)
  {
    if (char const* current = std::getenv("PATH"))
      startup_path = current;
  }
  if (startup_path && startup_path->size() > options.limits.max_path_bytes)
    return std::unexpected(command_error(ava::core::ErrorCategory::InvalidArgument, "startup PATH exceeds the bounded input size"));
  if (startup_path && has_forbidden_byte(*startup_path))
    return std::unexpected(command_error(ava::core::ErrorCategory::InvalidArgument, "startup PATH contains a forbidden control byte"));

  std::vector<CommandPathEntry> entries;
  if (startup_path)
  {
    auto const startup_entries = split_path(*startup_path);
    if (startup_entries.size() > options.limits.max_path_entries)
      return std::unexpected(command_error(ava::core::ErrorCategory::InvalidArgument, "startup PATH has too many entries"));
    for (auto const entry : startup_entries)
    {
      if (entry.empty())
        return std::unexpected(command_error(ava::core::ErrorCategory::InvalidArgument, "startup PATH contains an empty entry"));
      auto safe = safe_path_directory(std::filesystem::path(entry), PathProvenance::StartupPath);
      // A startup PATH commonly contains optional toolchain locations. Keep
      // only directories that pass the sealed-path checks. Missing, symlinked,
      // or writable absolute entries add no executable authority and are
      // omitted; empty or relative entries are ambiguous and fail closed.
      if (!safe)
      {
        if (safe.error().category() == ava::core::ErrorCategory::InvalidArgument)
          return std::unexpected(std::move(safe.error()));
        continue;
      }
      append_unique_path(entries, std::move(*safe));
    }
  }

  auto const add_optional_candidate = [&entries](std::filesystem::path const& candidate, PathProvenance provenance) {
    auto safe = safe_path_directory(candidate, provenance);
    if (safe)
      append_unique_path(entries, std::move(*safe));
  };
  // Host discovery uses only this trusted root; these paths never flow into the
  // child HOME/XDG/TMP environment.
  add_optional_candidate(trusted_home / ".local" / "bin", PathProvenance::UserLocal);
  add_optional_candidate(trusted_home / ".cargo" / "bin", PathProvenance::UserCargo);
  add_optional_candidate(workspace / ".venv" / "bin", PathProvenance::WorkspaceVenv);
  add_optional_candidate(workspace / "node_modules" / ".bin", PathProvenance::WorkspaceNodeModules);

  if (entries.size() > options.limits.max_path_entries)
    return std::unexpected(command_error(ava::core::ErrorCategory::InvalidArgument, "derived PATH has too many safe entries"));
  std::size_t total_bytes = 0;
  for (auto const& entry : entries)
  {
    auto const bytes = entry.directory.string().size();
    if (total_bytes > options.limits.max_path_bytes || bytes > options.limits.max_path_bytes - total_bytes)
      return std::unexpected(command_error(ava::core::ErrorCategory::InvalidArgument, "derived PATH exceeds the bounded size"));
    total_bytes += bytes;
  }
  return entries;
}

ExecutableOrigin executable_origin(ExecutableMetadata const& executable, std::filesystem::path const& workspace, std::filesystem::path const& trusted_home)
{
  if (is_within(executable.canonical_path, workspace))
    return ExecutableOrigin::Workspace;
  if (is_within(executable.canonical_path, trusted_home))
    return ExecutableOrigin::User;
  return ExecutableOrigin::System;
}

struct ShebangParse
{
  std::filesystem::path interpreter_path;
  std::string argument;
  bool is_env = false;
};

ava::core::Result<std::optional<ShebangParse>> shebang_interpreter_path(ExecutableMetadata const& executable, CommandLimits const& limits)
{
  // A normal read follows neither a final symlink nor a special file, and the
  // fstat identity check closes the metadata-to-read race as far as this
  // pathname-based planning design can. Descriptor-anchored execution is a
  // future execution-layer change.
  UniqueFd input(::open(executable.canonical_path.c_str(), O_RDONLY | O_NONBLOCK | O_NOFOLLOW | O_CLOEXEC));
  if (input.get() < 0)
  {
    auto result = command_error(ava::core::ErrorCategory::Io, "failed to read executable for shebang inspection", "path", executable.canonical_path.string());
    result.with_context("cause", std::strerror(errno));
    return std::unexpected(std::move(result));
  }
  struct stat status{};
  if (::fstat(input.get(), &status) != 0 || !S_ISREG(status.st_mode) || static_cast<std::uintmax_t>(status.st_dev) != executable.device ||
      static_cast<std::uintmax_t>(status.st_ino) != executable.inode || static_cast<std::int64_t>(status.st_ctim.tv_sec) != executable.changed_seconds ||
      static_cast<std::int64_t>(status.st_ctim.tv_nsec) != executable.changed_nanoseconds)
  {
    return std::unexpected(
        command_error(ava::core::ErrorCategory::Io, "executable changed during shebang inspection", "path", executable.canonical_path.string()));
  }

  std::string line(limits.max_shebang_bytes + 1, '\0');
  ssize_t const read_count = ::read(input.get(), line.data(), line.size());
  if (read_count < 0)
  {
    auto result = command_error(ava::core::ErrorCategory::Io, "failed to read executable shebang", "path", executable.canonical_path.string());
    result.with_context("cause", std::strerror(errno));
    return std::unexpected(std::move(result));
  }
  line.resize(static_cast<std::size_t>(read_count));
  if (!line.starts_with("#!"))
    return std::optional<ShebangParse>{};
  auto const newline = line.find('\n');
  if (newline == std::string::npos && line.size() > limits.max_shebang_bytes)
  {
    return std::unexpected(
        command_error(ava::core::ErrorCategory::InvalidArgument, "shebang line exceeds the bounded size", "path", executable.canonical_path.string()));
  }
  auto body = std::string_view(line).substr(2, newline == std::string_view::npos ? std::string_view::npos : newline - 2);
  if (!body.empty() && body.back() == '\r')
    body.remove_suffix(1);
  std::size_t first = 0;
  while (first < body.size() && std::isspace(static_cast<unsigned char>(body[first])) != 0) ++first;
  std::size_t end = first;
  while (end < body.size() && std::isspace(static_cast<unsigned char>(body[end])) == 0) ++end;
  auto const interpreter = body.substr(first, end - first);
  while (end < body.size() && std::isspace(static_cast<unsigned char>(body[end])) != 0) ++end;
  // Linux passes at most one argument from the shebang to the interpreter;
  // everything after the first whitespace-delimited interpreter token up to
  // the newline is that single argument.
  auto const argument = body.substr(end);
  if (interpreter.empty() || has_forbidden_byte(interpreter) || has_forbidden_byte(argument) || !std::filesystem::path(interpreter).is_absolute())
  {
    return std::unexpected(
        command_error(ava::core::ErrorCategory::InvalidArgument, "shebang must name an absolute interpreter path", "path", executable.canonical_path.string()));
  }
  ShebangParse parse{.interpreter_path = std::filesystem::path(interpreter), .argument = std::string(argument), .is_env = interpreter == "/usr/bin/env"};
  return parse;
}

std::optional<ExecutableMetadata> resolve_env_name(std::string_view name, std::vector<CommandPathEntry> const& path_entries)
{
  if (name.empty() || name.find('/') != std::string_view::npos || has_forbidden_byte(name))
    return std::nullopt;
  for (auto const& entry : path_entries)
  {
    auto const candidate = entry.directory / name;
    std::error_code exists_error;
    bool const exists = std::filesystem::exists(candidate, exists_error);
    if (!exists && !exists_error)
      continue;
    if (exists_error)
      continue;
    auto metadata = executable_metadata(candidate);
    if (metadata)
      return *metadata;
  }
  return std::nullopt;
}

ava::core::Result<std::pair<std::vector<ShebangInterpreter>, bool>> inspect_shebang_chain(ExecutableMetadata const& executable, CommandLimits const& limits,
                                                                                          std::vector<CommandPathEntry> const& path_entries)
{
  std::vector<ShebangInterpreter> interpreters;
  std::set<std::filesystem::path> seen;
  seen.insert(executable.canonical_path);
  auto current = executable;
  bool fully_resolved = true;
  while (true)
  {
    auto parsed = shebang_interpreter_path(current, limits);
    if (!parsed)
      return std::unexpected(std::move(parsed.error()));
    if (!*parsed)
      return std::make_pair(std::move(interpreters), fully_resolved);
    if (interpreters.size() >= limits.max_shebang_depth)
    {
      return std::unexpected(command_error(ava::core::ErrorCategory::InvalidArgument, "shebang interpreter chain exceeds the bounded depth", "path",
                                           executable.canonical_path.string()));
    }
    auto const& parse = **parsed;
    if (parse.is_env)
    {
      // #!/usr/bin/env <name> resolves the single name through the sealed
      // PATH. A missing or multi-token argument is safely representable as
      // a one-shot critical prompt but cannot be fully bound.
      auto env_meta = executable_metadata(parse.interpreter_path);
      if (!env_meta)
        return std::unexpected(std::move(env_meta.error()));
      if (!seen.insert(env_meta->canonical_path).second)
      {
        return std::unexpected(
            command_error(ava::core::ErrorCategory::InvalidArgument, "shebang interpreter chain contains a cycle", "path", env_meta->canonical_path.string()));
      }
      interpreters.push_back(ShebangInterpreter{.interpreter = *env_meta, .argument = parse.argument});
      // Determine whether the env argument is a single resolvable name.
      std::size_t token_start = 0;
      while (token_start < parse.argument.size() && std::isspace(static_cast<unsigned char>(parse.argument[token_start])) != 0) ++token_start;
      std::size_t token_end = token_start;
      while (token_end < parse.argument.size() && std::isspace(static_cast<unsigned char>(parse.argument[token_end])) == 0) ++token_end;
      std::size_t rest = token_end;
      while (rest < parse.argument.size() && std::isspace(static_cast<unsigned char>(parse.argument[rest])) != 0) ++rest;
      if (token_end == token_start || rest != parse.argument.size())
      {
        // Empty argument or trailing tokens: not a simple single-name env
        // invocation. The plan still succeeds with a one-shot critical prompt.
        fully_resolved = false;
        return std::make_pair(std::move(interpreters), fully_resolved);
      }
      auto const name = std::string_view(parse.argument).substr(token_start, token_end - token_start);
      auto resolved = resolve_env_name(name, path_entries);
      if (!resolved)
      {
        fully_resolved = false;
        return std::make_pair(std::move(interpreters), fully_resolved);
      }
      if (!seen.insert(resolved->canonical_path).second)
      {
        return std::unexpected(
            command_error(ava::core::ErrorCategory::InvalidArgument, "shebang interpreter chain contains a cycle", "path", resolved->canonical_path.string()));
      }
      interpreters.push_back(ShebangInterpreter{.interpreter = *resolved, .argument = {}});
      current = std::move(*resolved);
      continue;
    }
    auto interpreter = executable_metadata(parse.interpreter_path);
    if (!interpreter)
      return std::unexpected(std::move(interpreter.error()));
    if (!seen.insert(interpreter->canonical_path).second)
    {
      return std::unexpected(
          command_error(ava::core::ErrorCategory::InvalidArgument, "shebang interpreter chain contains a cycle", "path", interpreter->canonical_path.string()));
    }
    interpreters.push_back(ShebangInterpreter{.interpreter = *interpreter, .argument = parse.argument});
    current = std::move(*interpreter);
  }
}

}  // namespace

ava::core::Result<SealedCommandContext> discover_command_context(CommandIntent const& intent, CommandBuildOptions const& options)
{
  if (auto valid = validate_limits(options.limits); !valid)
    return std::unexpected(std::move(valid.error()));
  auto workspace_metadata = inspect_path_metadata(options.workspace, ExpectedNode::Directory, true, "command workspace");
  if (!workspace_metadata)
    return std::unexpected(std::move(workspace_metadata.error()));
  if (auto valid = validate_safe_directory(*workspace_metadata, false, "command workspace"); !valid)
    return std::unexpected(std::move(valid.error()));

  PathMetadata cwd_metadata = *workspace_metadata;
  if (intent.requested_cwd())
  {
    auto candidate = intent.requested_cwd()->is_absolute() ? *intent.requested_cwd() : workspace_metadata->canonical_path / *intent.requested_cwd();
    auto captured = inspect_path_metadata(candidate, ExpectedNode::Directory, true, "command cwd");
    if (!captured)
      return std::unexpected(std::move(captured.error()));
    if (!is_within(captured->canonical_path, workspace_metadata->canonical_path))
    {
      return std::unexpected(command_error(ava::core::ErrorCategory::PermissionDenied, "command cwd must remain inside the canonical workspace", "cwd",
                                           captured->canonical_path.string()));
    }
    if (auto valid = validate_safe_directory(*captured, false, "command cwd"); !valid)
      return std::unexpected(std::move(valid.error()));
    cwd_metadata = std::move(*captured);
  }

  auto trusted_home = inspect_path_metadata(options.trusted_home, ExpectedNode::Directory, true, "trusted command discovery home");
  if (!trusted_home)
    return std::unexpected(std::move(trusted_home.error()));
  if (auto valid = validate_safe_directory(*trusted_home, true, "trusted command discovery home"); !valid)
    return std::unexpected(std::move(valid.error()));
  if (options.ava_authority_roots.size() > options.limits.max_path_entries)
    return std::unexpected(command_error(ava::core::ErrorCategory::InvalidArgument, "AVA authority root list has too many entries"));
  std::vector<PathMetadata> ava_authority_roots;
  ava_authority_roots.reserve(options.ava_authority_roots.size());
  for (auto const& root : options.ava_authority_roots)
  {
    auto authority = inspect_path_metadata(root, ExpectedNode::Directory, true, "AVA authority root");
    if (!authority)
      return std::unexpected(std::move(authority.error()));
    if (auto valid = validate_safe_directory(*authority, true, "AVA authority root"); !valid)
      return std::unexpected(std::move(valid.error()));
    ava_authority_roots.push_back(std::move(*authority));
  }

  auto const overlaps_protected_root = [&workspace_metadata, &trusted_home, &ava_authority_roots](std::filesystem::path const& candidate) {
    auto const overlaps = [&candidate](PathMetadata const& protected_root) {
      return is_within(candidate, protected_root.canonical_path) || is_within(protected_root.canonical_path, candidate);
    };
    return overlaps(*workspace_metadata) || overlaps(*trusted_home) || std::ranges::any_of(ava_authority_roots, overlaps);
  };
  auto const capture_synthetic_root = [&options, &overlaps_protected_root](std::filesystem::path const& path,
                                                                           std::string_view name) -> ava::core::Result<PathMetadata> {
    auto synthetic = normalized_absolute(path, name);
    if (!synthetic)
      return std::unexpected(std::move(synthetic.error()));
    if (synthetic->string().size() > options.limits.max_path_bytes)
    {
      return std::unexpected(command_error(ava::core::ErrorCategory::InvalidArgument,
                                           std::string(name) + " exceeds the bounded synthetic environment path size", "path", synthetic->string()));
    }
    auto metadata = inspect_path_metadata(*synthetic, ExpectedNode::Directory, true, name);
    if (!metadata)
      return std::unexpected(std::move(metadata.error()));
    if (auto valid = validate_synthetic_environment_directory(*metadata, name); !valid)
      return std::unexpected(std::move(valid.error()));
    if (overlaps_protected_root(metadata->canonical_path))
    {
      return std::unexpected(command_error(ava::core::ErrorCategory::InvalidArgument,
                                           std::string(name) + " must be disjoint from workspace and trusted AVA host roots", "path",
                                           metadata->canonical_path.string()));
    }
    return metadata;
  };
  auto home = capture_synthetic_root(options.environment.home, "HOME");
  if (!home)
    return std::unexpected(std::move(home.error()));
  auto xdg_config_home = capture_synthetic_root(options.environment.xdg_config_home, "XDG_CONFIG_HOME");
  if (!xdg_config_home)
    return std::unexpected(std::move(xdg_config_home.error()));
  auto xdg_cache_home = capture_synthetic_root(options.environment.xdg_cache_home, "XDG_CACHE_HOME");
  if (!xdg_cache_home)
    return std::unexpected(std::move(xdg_cache_home.error()));
  auto xdg_data_home = capture_synthetic_root(options.environment.xdg_data_home, "XDG_DATA_HOME");
  if (!xdg_data_home)
    return std::unexpected(std::move(xdg_data_home.error()));
  auto xdg_state_home = capture_synthetic_root(options.environment.xdg_state_home, "XDG_STATE_HOME");
  if (!xdg_state_home)
    return std::unexpected(std::move(xdg_state_home.error()));
  auto tmpdir = capture_synthetic_root(options.environment.tmpdir, "TMPDIR");
  if (!tmpdir)
    return std::unexpected(std::move(tmpdir.error()));
  SyntheticEnvironmentRoots synthetic_environment_roots{.home = std::move(*home),
                                                        .xdg_config_home = std::move(*xdg_config_home),
                                                        .xdg_cache_home = std::move(*xdg_cache_home),
                                                        .xdg_data_home = std::move(*xdg_data_home),
                                                        .xdg_state_home = std::move(*xdg_state_home),
                                                        .tmpdir = std::move(*tmpdir)};

  auto entries = discover_path(options, workspace_metadata->canonical_path, trusted_home->canonical_path);
  if (!entries)
    return std::unexpected(std::move(entries.error()));
  return SealedCommandContext{.workspace = workspace_metadata->canonical_path,
                              .cwd = cwd_metadata.canonical_path,
                              .workspace_metadata = std::move(*workspace_metadata),
                              .cwd_metadata = std::move(cwd_metadata),
                              .trusted_home_metadata = std::move(*trusted_home),
                              .ava_authority_root_metadata = std::move(ava_authority_roots),
                              .synthetic_environment_roots = std::move(synthetic_environment_roots),
                              .path_entries = std::move(*entries)};
}

ava::core::Result<ResolvedExecutable> resolve_executable(std::vector<std::string> const& argv, std::vector<CommandPathEntry> const& path_entries,
                                                         std::filesystem::path const& cwd, std::filesystem::path const& workspace,
                                                         std::filesystem::path const& trusted_home, CommandLimits const& limits)
{
  if (argv.empty() || argv.front().empty())
    return std::unexpected(command_error(ava::core::ErrorCategory::InvalidArgument, "command argv has no executable identity"));

  std::filesystem::path requested(argv.front());
  ava::core::Result<ExecutableMetadata> metadata = std::unexpected(command_error(ava::core::ErrorCategory::NotFound, "executable was not found"));
  if (requested.is_absolute() || requested.string().find('/') != std::string::npos)
  {
    metadata = executable_metadata(requested.is_absolute() ? requested : cwd / requested);
  }
  else
  {
    for (auto const& entry : path_entries)
    {
      auto const candidate = entry.directory / requested;
      std::error_code exists_error;
      bool const exists = std::filesystem::exists(candidate, exists_error);
      if (!exists && !exists_error)
        continue;
      if (exists_error)
      {
        auto error = command_error(ava::core::ErrorCategory::Io, "failed to inspect executable candidate", "path", candidate.string());
        error.with_context("cause", exists_error.message());
        return std::unexpected(std::move(error));
      }
      metadata = executable_metadata(candidate);
      break;
    }
  }
  if (!metadata)
    return std::unexpected(std::move(metadata.error()));

  auto interpreters = inspect_shebang_chain(*metadata, limits, path_entries);
  if (!interpreters)
    return std::unexpected(std::move(interpreters.error()));
  return ResolvedExecutable{.executable = *metadata,
                            .origin = executable_origin(*metadata, workspace, trusted_home),
                            .shebang_interpreters = std::move(interpreters->first),
                            .shebang_fully_resolved = interpreters->second};
}

std::optional<PathMetadata> seal_recipe_path_argument(std::string_view value, std::filesystem::path const& cwd, std::filesystem::path const& workspace)
{
  if (value.empty() || has_forbidden_byte(value))
    return std::nullopt;
  std::filesystem::path raw(value);
  auto candidate = raw.is_absolute() ? raw : cwd / raw;
  auto normalized = normalized_absolute(candidate, "recipe path argument");
  if (!normalized || !is_within(*normalized, workspace))
    return std::nullopt;
  auto metadata = inspect_path_metadata(*normalized, ExpectedNode::Any, false, "recipe path argument");
  if (!metadata || !is_within(metadata->canonical_path, workspace))
    return std::nullopt;
  // A Standard recipe cannot use a symlinked path component. Canonical equality
  // also covers intermediate symlinks; replacement is caught by freshness.
  if (metadata->canonical_path != *normalized || metadata->requested_path_is_symlink)
    return std::nullopt;
  if (auto valid = validate_safe_recipe_path(*metadata); !valid)
    return std::nullopt;
  return std::move(*metadata);
}

bool ancestors_are_fresh(std::vector<PathAncestorMetadata> const& recorded, std::vector<PathAncestorMetadata> const& current)
{
  if (recorded.size() != current.size())
    return false;
  for (std::size_t index = 0; index < recorded.size(); ++index)
  {
    auto const& expected = recorded[index];
    auto const& observed = current[index];
    if (expected.path != observed.path || expected.is_symlink != observed.is_symlink || expected.identity_bound != observed.identity_bound)
      return false;
    if (expected.identity_bound)
    {
      if (expected != observed)
        return false;
      continue;
    }
    struct stat status{};
    status.st_mode = static_cast<mode_t>(observed.mode);
    status.st_uid = static_cast<uid_t>(observed.owner);
    if (!S_ISDIR(status.st_mode) || !owner_is_safe(status, false) || !is_sticky_directory(status))
      return false;
  }
  return true;
}

ava::core::Result<bool> path_metadata_is_fresh(PathMetadata const& recorded)
{
  auto current = inspect_path_metadata(recorded.requested_path, ExpectedNode::Any, false, "sealed path");
  if (!current)
    return std::unexpected(std::move(current.error()));
  if (!ancestors_are_fresh(recorded.ancestor_metadata, current->ancestor_metadata))
    return false;
  current->ancestor_metadata = recorded.ancestor_metadata;
  return *current == recorded;
}

ava::core::Result<bool> executable_metadata_is_fresh(ExecutableMetadata const& recorded)
{
  auto current = executable_metadata(recorded.requested_path);
  if (!current)
    return std::unexpected(std::move(current.error()));
  if (!ancestors_are_fresh(recorded.ancestor_metadata, current->ancestor_metadata))
    return false;
  current->ancestor_metadata = recorded.ancestor_metadata;
  return *current == recorded;
}

}  // namespace ava::command::detail
