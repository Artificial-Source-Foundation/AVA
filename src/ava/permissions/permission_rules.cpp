#include "sys.h"
#include "ava/command/private_group.h"
#include "ava/permissions/permission_rules.h"
#include "ava/core/ids.h"
#include "ava/core/json.h"
#include "ava/core/path.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cerrno>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <ctime>
#include <iomanip>
#include <iterator>
#include <mutex>
#include <sstream>
#include <string>
#include <utility>
#include <fcntl.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <unistd.h>

namespace ava::permissions {
namespace {

constexpr std::size_t kMaxPermissionRulesFileBytes = 1024 * 1024;
constexpr std::size_t kMaxPermissionRuleReasonBytes = 1024;
constexpr std::size_t kMaxPermissionRuleCommandBytes = 8192;
constexpr std::size_t kMaxPermissionRuleIdentifierBytes = 256;
constexpr std::size_t kMaxPermissionRecipeKeyBytes = 128;
constexpr std::size_t kMaxPermissionRecipeDisplayBytes = 1024;

class ScopedFd
{
 public:
  ScopedFd() = default;
  explicit ScopedFd(int fd) : fd_(fd) { }
  ScopedFd(ScopedFd const&) = delete;
  ScopedFd& operator=(ScopedFd const&) = delete;
  ScopedFd(ScopedFd&& other) noexcept : fd_(std::exchange(other.fd_, -1)) { }
  ScopedFd& operator=(ScopedFd&& other) noexcept
  {
    if (this != &other)
    {
      close_if_open();
      fd_ = std::exchange(other.fd_, -1);
    }
    return *this;
  }
  ~ScopedFd() { close_if_open(); }

  [[nodiscard]] int get() const noexcept { return fd_; }

 private:
  void close_if_open() noexcept
  {
    if (fd_ >= 0)
      static_cast<void>(::close(fd_));
  }

  int fd_ = -1;
};

class TempPathCleanup
{
 public:
  TempPathCleanup(int parent_fd, std::string name) : parent_fd_(parent_fd), name_(std::move(name)) { }
  TempPathCleanup(TempPathCleanup const&) = delete;
  TempPathCleanup& operator=(TempPathCleanup const&) = delete;
  TempPathCleanup(TempPathCleanup&& other) noexcept : parent_fd_(other.parent_fd_), name_(std::move(other.name_)), active_(std::exchange(other.active_, false))
  {
  }
  TempPathCleanup& operator=(TempPathCleanup&& other) noexcept
  {
    if (this != &other)
    {
      cleanup();
      parent_fd_ = other.parent_fd_;
      name_ = std::move(other.name_);
      active_ = std::exchange(other.active_, false);
    }
    return *this;
  }
  ~TempPathCleanup() { cleanup(); }

  void dismiss() noexcept { active_ = false; }

 private:
  void cleanup() noexcept
  {
    if (active_ && parent_fd_ >= 0 && !name_.empty())
      static_cast<void>(::unlinkat(parent_fd_, name_.c_str(), 0));
    active_ = false;
  }

  int parent_fd_ = -1;
  std::string name_;
  bool active_ = true;
};

struct RuleDirectory
{
  ScopedFd fd;
  std::filesystem::path path;
  std::string file_name;
};

struct RuleLock
{
  RuleDirectory directory;
  ScopedFd fd;
};

std::string errno_message()
{
  return std::strerror(errno);
}

ava::core::Error rule_file_error(ava::core::ErrorCategory category, std::string message, std::filesystem::path const& path)
{
  auto error = ava::core::Error(category, std::move(message));
  error.with_context("path", path.string());
  return error;
}

ava::core::Error rule_parse_error(std::string message, std::filesystem::path const& path, std::string_view field = {},
                                  std::optional<std::size_t> index = std::nullopt)
{
  auto error = rule_file_error(ava::core::ErrorCategory::InvalidArgument, std::move(message), path);
  if (!field.empty())
    error.with_context("field", std::string(field));
  if (index)
    error.with_context("rule_index", std::to_string(*index));
  return error;
}

std::filesystem::path normalized_path(std::filesystem::path const& path)
{
  return ava::core::normalized_absolute_path(path);
}

// Compare resolved inode ancestry plus the still-missing lexical suffix. This
// detects existing directory aliases without canonicalizing or replacing either
// caller-visible path identity, and also protects not-yet-created rule files.
bool paths_refer_to_same_file(std::filesystem::path const& a, std::filesystem::path const& b)
{
  struct Identity
  {
    dev_t device = 0;
    ino_t inode = 0;
    std::vector<std::string> missing_suffix;
  };
  auto const identity = [](std::filesystem::path path) -> std::optional<Identity> {
    if (path.empty())
      return std::nullopt;
    path = normalized_path(path);
    std::vector<std::string> suffix;
    for (std::size_t depth = 0; depth < 256; ++depth)
    {
      struct stat status{};
      if (::stat(path.c_str(), &status) == 0)
        return Identity{.device = status.st_dev, .inode = status.st_ino, .missing_suffix = std::move(suffix)};
      if (errno != ENOENT && errno != ENOTDIR)
        return std::nullopt;
      auto const parent = path.parent_path();
      if (parent.empty() || parent == path)
        return std::nullopt;
      suffix.push_back(path.filename().string());
      path = parent;
    }
    return std::nullopt;
  };

  auto const left = identity(a);
  auto const right = identity(b);
  return left && right && left->device == right->device && left->inode == right->inode && left->missing_suffix == right->missing_suffix;
}

bool contains_parent_reference(std::filesystem::path const& path)
{
  for (auto const& part : path.lexically_normal())
  {
    if (part == "..")
      return true;
  }
  return false;
}

bool has_control_byte(std::string_view value)
{
  return std::ranges::any_of(value, [](char ch) {
    auto const byte = static_cast<unsigned char>(ch);
    return byte < 0x20 || byte == 0x7F;
  });
}

bool valid_utf8(std::string_view value)
{
  std::size_t index = 0;
  while (index < value.size())
  {
    auto const first = static_cast<unsigned char>(value[index++]);
    if (first < 0x80)
      continue;
    std::size_t continuation = 0;
    std::uint32_t codepoint = 0;
    if ((first & 0xe0U) == 0xc0U)
    {
      continuation = 1;
      codepoint = first & 0x1fU;
    }
    else if ((first & 0xf0U) == 0xe0U)
    {
      continuation = 2;
      codepoint = first & 0x0fU;
    }
    else if ((first & 0xf8U) == 0xf0U)
    {
      continuation = 3;
      codepoint = first & 0x07U;
    }
    else
      return false;
    if (index + continuation > value.size())
      return false;
    for (std::size_t count = 0; count < continuation; ++count)
    {
      auto const next = static_cast<unsigned char>(value[index++]);
      if ((next & 0xc0U) != 0x80U)
        return false;
      codepoint = (codepoint << 6U) | (next & 0x3fU);
    }
    if ((continuation == 1 && codepoint < 0x80U) || (continuation == 2 && codepoint < 0x800U) ||
        (continuation == 3 && (codepoint < 0x10000U || codepoint > 0x10ffffU || (codepoint >= 0xd800U && codepoint <= 0xdfffU))))
      return false;
  }
  return true;
}

std::string_view recipe_key_prefix(PermissionRuleScope scope)
{
  return scope == PermissionRuleScope::Global ? std::string_view("sha256:ava-command-recipe-v1:") : std::string_view("sha256:ava-command-workspace-recipe-v1:");
}

bool valid_recipe_key(std::string_view value, PermissionRuleScope scope)
{
  auto const prefix = recipe_key_prefix(scope);
  if (!value.starts_with(prefix) || value.size() != prefix.size() + 64 || value.size() > kMaxPermissionRecipeKeyBytes)
    return false;
  return std::ranges::all_of(value.substr(prefix.size()), [](unsigned char ch) { return std::isdigit(ch) != 0 || (ch >= 'a' && ch <= 'f'); });
}

bool valid_identifier(std::string_view value)
{
  if (value.empty() || value.size() > kMaxPermissionRuleIdentifierBytes)
    return false;
  return std::ranges::all_of(value, [](char ch) {
    auto const byte = static_cast<unsigned char>(ch);
    return std::isalnum(byte) != 0 || ch == '_' || ch == '-' || ch == '.';
  });
}

bool is_path_operation(Operation operation)
{
  return operation == Operation::ReadFile || operation == Operation::SearchFiles || operation == Operation::EditFile || operation == Operation::LspQuery;
}

bool is_command_operation(Operation operation)
{
  return operation == Operation::RunCommand || operation == Operation::NetworkFetch || operation == Operation::NetworkSearch ||
         operation == Operation::LspServerLaunch || operation == Operation::McpResourceRead;
}

std::string workspace_rules_key(std::filesystem::path const& workspace_dir)
{
  auto const normalized = normalized_path(workspace_dir).string();
  std::uint64_t hash = 14695981039346656037ULL;
  for (auto const ch : normalized)
  {
    hash ^= static_cast<unsigned char>(ch);
    hash *= 1099511628211ULL;
  }
  std::ostringstream out;
  out << std::hex << hash;
  return out.str();
}

std::mutex& protected_rule_paths_mutex()
{
  static std::mutex mutex;
  return mutex;
}

std::vector<std::filesystem::path>& protected_rule_paths()
{
  static std::vector<std::filesystem::path> paths;
  return paths;
}

void append_protected_rule_path(std::vector<std::filesystem::path>& paths, std::filesystem::path path)
{
  if (path.empty())
    return;
  path = normalized_path(path);
  if (std::ranges::find(paths, path) == paths.end())
  {
    paths.push_back(std::move(path));
  }
}

std::filesystem::path rules_file_path(PermissionRuleStore const& store, PermissionRuleScope scope)
{
  return enforceable_permission_rules_file(store, scope);
}

ava::core::VoidResult validate_rule_file_path(std::filesystem::path const& path)
{
  if (path.empty() || !path.is_absolute() || path.filename().empty() || path.filename() == "." || path.filename() == "..")
  {
    return std::unexpected(
        rule_file_error(ava::core::ErrorCategory::InvalidArgument, "permission rules file path must be an absolute path with a regular filename", path));
  }
  for (auto const& component : path)
  {
    if (component == "." || component == "..")
    {
      return std::unexpected(
          rule_file_error(ava::core::ErrorCategory::InvalidArgument, "permission rules file path must not contain traversal components", path));
    }
  }
  return {};
}

ava::core::VoidResult validate_rule_ancestor(int fd, std::filesystem::path const& path)
{
  struct stat st{};
  if (::fstat(fd, &st) != 0)
  {
    auto error = rule_file_error(ava::core::ErrorCategory::Io, "failed to inspect permission rules path component", path);
    error.with_context("cause", errno_message());
    return std::unexpected(std::move(error));
  }
  if (!S_ISDIR(st.st_mode))
    return std::unexpected(rule_file_error(ava::core::ErrorCategory::PermissionDenied, "permission rules path component is not a directory", path));
  if (st.st_uid != 0 && st.st_uid != ::geteuid())
    return std::unexpected(
        rule_file_error(ava::core::ErrorCategory::PermissionDenied, "permission rules path component is not owned by root or the current user", path));
  bool const shared_sticky_root_namespace = st.st_uid == 0 && (st.st_mode & S_ISVTX) != 0;
  bool const private_primary_group_directory = ava::command::detail::is_current_user_private_primary_group_directory(st);
  if ((st.st_mode & (S_IWGRP | S_IWOTH)) != 0 && !shared_sticky_root_namespace && !private_primary_group_directory)
  {
    return std::unexpected(rule_file_error(ava::core::ErrorCategory::PermissionDenied, "permission rules path component is group- or world-writable", path));
  }
  return {};
}

ava::core::VoidResult validate_final_rule_directory(int fd, std::filesystem::path const& path)
{
  if (auto safe = validate_rule_ancestor(fd, path); !safe)
    return safe;
  struct stat st{};
  if (::fstat(fd, &st) != 0)
  {
    auto error = rule_file_error(ava::core::ErrorCategory::Io, "failed to inspect permission rules directory", path);
    error.with_context("cause", errno_message());
    return std::unexpected(std::move(error));
  }
  if (st.st_uid != ::geteuid() || (st.st_mode & 07777) != S_IRWXU)
  {
    auto error =
        rule_file_error(ava::core::ErrorCategory::PermissionDenied, "permission rules directory must be owned by the current user with exact mode 0700", path);
    error.with_context("expected_permissions", "0700");
    return std::unexpected(std::move(error));
  }
  return {};
}

ava::core::Error rule_directory_open_error(std::filesystem::path const& path, int error_number)
{
  auto const category = error_number == ELOOP || error_number == ENOTDIR ? ava::core::ErrorCategory::PermissionDenied : ava::core::ErrorCategory::Io;
  auto error = rule_file_error(category, "failed to securely open permission rules path component", path);
  error.with_context("cause", std::strerror(error_number));
  return error;
}

struct RuleWalkStart
{
  ScopedFd fd;
  std::filesystem::path component_path;
  std::filesystem::path relative_parent;
};

ava::core::Result<RuleWalkStart> begin_rule_directory_walk(std::filesystem::path const& file_path,
                                                           std::shared_ptr<ava::core::AnchorSet> const& anchors)
{
  if (anchors)
  {
    auto selected = anchors->find_anchor(file_path);
    if (selected)
    {
      int const duplicate = ::openat(selected->anchor().fd, ".", O_RDONLY | O_DIRECTORY | O_NONBLOCK | O_CLOEXEC);
      if (duplicate < 0)
      {
        auto error = rule_file_error(ava::core::ErrorCategory::Io, "failed to duplicate permission rules storage anchor", file_path);
        error.with_context("cause", errno_message());
        return std::unexpected(std::move(error));
      }
      ScopedFd fd(duplicate);
      if (auto safe = validate_rule_ancestor(fd.get(), selected->anchor().root); !safe)
        return std::unexpected(std::move(safe.error()));
      return RuleWalkStart{.fd = std::move(fd),
                           .component_path = selected->anchor().root,
                           .relative_parent = selected->relative().parent_path()};
    }
  }

  ScopedFd fd(::open("/", O_RDONLY | O_DIRECTORY | O_NONBLOCK | O_NOFOLLOW | O_CLOEXEC));
  if (fd.get() < 0)
  {
    auto error = rule_file_error(ava::core::ErrorCategory::Io, "failed to open permission rules filesystem anchor", file_path);
    error.with_context("cause", errno_message());
    return std::unexpected(std::move(error));
  }
  if (auto safe = validate_rule_ancestor(fd.get(), "/"); !safe)
    return std::unexpected(std::move(safe.error()));
  return RuleWalkStart{.fd = std::move(fd), .component_path = "/", .relative_parent = file_path.parent_path().relative_path()};
}

ava::core::Result<std::optional<RuleDirectory>> open_rule_directory_for_read(
    std::filesystem::path const& file_path, std::shared_ptr<ava::core::AnchorSet> const& anchors)
{
  if (auto valid = validate_rule_file_path(file_path); !valid)
    return std::unexpected(std::move(valid.error()));

  auto walk = begin_rule_directory_walk(file_path, anchors);
  if (!walk)
    return std::unexpected(std::move(walk.error()));
  ScopedFd current = std::move(walk->fd);
  auto component_path = std::move(walk->component_path);

  auto const parent = file_path.parent_path();
  for (auto const& component : walk->relative_parent)
  {
    auto const name = component.string();
    component_path /= name;
    int const next = ::openat(current.get(), name.c_str(), O_RDONLY | O_DIRECTORY | O_NONBLOCK | O_NOFOLLOW | O_CLOEXEC);
    if (next < 0)
    {
      auto const error_number = errno;
      if (error_number == ENOENT)
        return std::optional<RuleDirectory>{};
      return std::unexpected(rule_directory_open_error(component_path, error_number));
    }
    ScopedFd opened(next);
    if (auto safe = validate_rule_ancestor(opened.get(), component_path); !safe)
      return std::unexpected(std::move(safe.error()));
    current = std::move(opened);
  }
  if (auto safe = validate_final_rule_directory(current.get(), parent); !safe)
    return std::unexpected(std::move(safe.error()));
  return std::optional<RuleDirectory>{RuleDirectory{.fd = std::move(current), .path = parent, .file_name = file_path.filename().string()}};
}

ava::core::Result<RuleDirectory> open_rule_directory_for_write(std::filesystem::path const& file_path,
                                                                 std::shared_ptr<ava::core::AnchorSet> const& anchors)
{
  if (auto valid = validate_rule_file_path(file_path); !valid)
    return std::unexpected(std::move(valid.error()));

  auto walk = begin_rule_directory_walk(file_path, anchors);
  if (!walk)
    return std::unexpected(std::move(walk.error()));
  ScopedFd current = std::move(walk->fd);
  auto component_path = std::move(walk->component_path);

  auto const parent = file_path.parent_path();
  for (auto const& component : walk->relative_parent)
  {
    auto const name = component.string();
    component_path /= name;
    bool created = false;
    int next = ::openat(current.get(), name.c_str(), O_RDONLY | O_DIRECTORY | O_NONBLOCK | O_NOFOLLOW | O_CLOEXEC);
    if (next < 0 && errno == ENOENT)
    {
      if (::mkdirat(current.get(), name.c_str(), S_IRWXU) == 0)
      {
        created = true;
      }
      else if (errno != EEXIST)
      {
        auto error = rule_file_error(ava::core::ErrorCategory::Io, "failed to create permission rules path component", component_path);
        error.with_context("cause", errno_message());
        return std::unexpected(std::move(error));
      }
      next = ::openat(current.get(), name.c_str(), O_RDONLY | O_DIRECTORY | O_NONBLOCK | O_NOFOLLOW | O_CLOEXEC);
    }
    if (next < 0)
      return std::unexpected(rule_directory_open_error(component_path, errno));
    ScopedFd opened(next);
    if (auto safe = validate_rule_ancestor(opened.get(), component_path); !safe)
      return std::unexpected(std::move(safe.error()));
    if (created && ::fchmod(opened.get(), S_IRWXU) != 0)
    {
      auto error = rule_file_error(ava::core::ErrorCategory::Io, "failed to secure newly created permission rules directory", component_path);
      error.with_context("cause", errno_message());
      return std::unexpected(std::move(error));
    }
    current = std::move(opened);
  }
  if (auto safe = validate_final_rule_directory(current.get(), parent); !safe)
    return std::unexpected(std::move(safe.error()));
  return RuleDirectory{.fd = std::move(current), .path = parent, .file_name = file_path.filename().string()};
}

ava::core::VoidResult validate_unsafe_replace_target(RuleDirectory const& directory)
{
  struct stat st{};
  if (::fstatat(directory.fd.get(), directory.file_name.c_str(), &st, AT_SYMLINK_NOFOLLOW) != 0)
  {
    if (errno == ENOENT)
      return {};
    auto error = rule_file_error(ava::core::ErrorCategory::Io, "failed to inspect permission rules file", directory.path / directory.file_name);
    error.with_context("cause", errno_message());
    return std::unexpected(std::move(error));
  }
  if (S_ISLNK(st.st_mode) || !S_ISREG(st.st_mode) || st.st_uid != ::geteuid() || (st.st_mode & 07777) != (S_IRUSR | S_IWUSR))
  {
    return std::unexpected(rule_file_error(ava::core::ErrorCategory::PermissionDenied,
                                           "permission rules file must be a current-user-owned regular file with exact mode 0600",
                                           directory.path / directory.file_name));
  }
  return {};
}

ava::core::Result<RuleLock> acquire_rule_lock(std::filesystem::path const& file_path,
                                               std::shared_ptr<ava::core::AnchorSet> const& anchors)
{
  auto directory = open_rule_directory_for_write(file_path, anchors);
  if (!directory)
    return std::unexpected(std::move(directory.error()));

  auto const lock_name = directory->file_name + ".lock";
  ScopedFd fd(::openat(directory->fd.get(), lock_name.c_str(), O_RDWR | O_CREAT | O_NONBLOCK | O_CLOEXEC | O_NOFOLLOW, S_IRUSR | S_IWUSR));
  if (fd.get() < 0)
  {
    auto error = rule_file_error(ava::core::ErrorCategory::Io, "failed to open permission rules lock file", directory->path / lock_name);
    error.with_context("cause", errno_message());
    return std::unexpected(std::move(error));
  }

  struct stat opened_st{};
  if (::fstat(fd.get(), &opened_st) != 0)
  {
    auto error = rule_file_error(ava::core::ErrorCategory::Io, "failed to inspect permission rules lock file", directory->path / lock_name);
    error.with_context("cause", errno_message());
    return std::unexpected(std::move(error));
  }
  if (!S_ISREG(opened_st.st_mode) || opened_st.st_uid != ::geteuid() || (opened_st.st_mode & (S_IRWXG | S_IRWXO)) != 0)
  {
    return std::unexpected(rule_file_error(ava::core::ErrorCategory::PermissionDenied, "permission rules lock file is not a private current-user regular file",
                                           directory->path / lock_name));
  }
  if (::fchmod(fd.get(), S_IRUSR | S_IWUSR) != 0)
  {
    auto error = rule_file_error(ava::core::ErrorCategory::Io, "failed to set permission rules lock permissions", directory->path / lock_name);
    error.with_context("cause", errno_message());
    return std::unexpected(std::move(error));
  }
  while (::flock(fd.get(), LOCK_EX) != 0)
  {
    if (errno == EINTR)
      continue;
    auto error = rule_file_error(ava::core::ErrorCategory::Io, "failed to lock permission rules file", directory->path / lock_name);
    error.with_context("cause", errno_message());
    return std::unexpected(std::move(error));
  }
  return RuleLock{.directory = std::move(*directory), .fd = std::move(fd)};
}

ava::core::VoidResult write_all_to_fd(int fd, std::string_view body, std::filesystem::path const& path)
{
  std::size_t offset = 0;
  while (offset < body.size())
  {
    auto const written = ::write(fd, body.data() + offset, body.size() - offset);
    if (written < 0)
    {
      auto error = rule_file_error(ava::core::ErrorCategory::Io, "failed to write permission rules file", path);
      error.with_context("cause", errno_message());
      return std::unexpected(std::move(error));
    }
    if (written == 0)
    {
      return std::unexpected(rule_file_error(ava::core::ErrorCategory::Io, "permission rules file write made no progress", path));
    }
    offset += static_cast<std::size_t>(written);
  }
  return {};
}

ava::core::VoidResult fsync_fd(int fd, std::filesystem::path const& path, std::string_view message)
{
  if (::fsync(fd) == 0)
    return {};
  auto error = rule_file_error(ava::core::ErrorCategory::Io, std::string(message), path);
  error.with_context("cause", errno_message());
  return std::unexpected(std::move(error));
}

ava::core::VoidResult write_rules_file_atomic(RuleDirectory const& directory, std::string_view body)
{
  if (auto checked = validate_unsafe_replace_target(directory); !checked)
    return checked;

  auto const path = directory.path / directory.file_name;
  for (int attempt = 0; attempt < 100; ++attempt)
  {
    auto const temp_name = directory.file_name + ".tmp." + std::to_string(::getpid()) + "." + std::to_string(attempt);
    ScopedFd const fd(::openat(directory.fd.get(), temp_name.c_str(), O_WRONLY | O_CREAT | O_EXCL | O_NONBLOCK | O_CLOEXEC | O_NOFOLLOW, S_IRUSR | S_IWUSR));
    if (fd.get() < 0)
    {
      if (errno == EEXIST)
        continue;
      auto error = rule_file_error(ava::core::ErrorCategory::Io, "failed to create temporary permission rules file", directory.path / temp_name);
      error.with_context("cause", errno_message());
      return std::unexpected(std::move(error));
    }
    TempPathCleanup cleanup(directory.fd.get(), temp_name);
    struct stat opened_st{};
    if (::fstat(fd.get(), &opened_st) != 0)
    {
      auto error = rule_file_error(ava::core::ErrorCategory::Io, "failed to inspect temporary permission rules file", directory.path / temp_name);
      error.with_context("cause", errno_message());
      return std::unexpected(std::move(error));
    }
    if (!S_ISREG(opened_st.st_mode) || opened_st.st_uid != ::geteuid() || opened_st.st_nlink != 1)
    {
      return std::unexpected(rule_file_error(ava::core::ErrorCategory::PermissionDenied,
                                             "temporary permission rules file is not a private current-user regular file", directory.path / temp_name));
    }
    if (::fchmod(fd.get(), S_IRUSR | S_IWUSR) != 0)
    {
      auto error = rule_file_error(ava::core::ErrorCategory::Io, "failed to set temporary permission rules permissions", directory.path / temp_name);
      error.with_context("cause", errno_message());
      return std::unexpected(std::move(error));
    }
    if (auto written = write_all_to_fd(fd.get(), body, directory.path / temp_name); !written)
      return written;
    if (auto synced = fsync_fd(fd.get(), directory.path / temp_name, "failed to sync temporary permission rules file"); !synced)
      return synced;
    if (auto checked = validate_unsafe_replace_target(directory); !checked)
      return checked;
    if (::renameat(directory.fd.get(), temp_name.c_str(), directory.fd.get(), directory.file_name.c_str()) != 0)
    {
      auto error = rule_file_error(ava::core::ErrorCategory::Io, "failed to replace permission rules file", path);
      error.with_context("cause", errno_message());
      return std::unexpected(std::move(error));
    }
    cleanup.dismiss();
    return fsync_fd(directory.fd.get(), directory.path, "failed to sync permission rules directory");
  }
  return std::unexpected(rule_file_error(ava::core::ErrorCategory::Io, "failed to create unique temporary permission rules file", path));
}

ava::core::Result<std::optional<std::string>> read_rules_text_if_exists(RuleDirectory const& directory)
{
  auto const path = directory.path / directory.file_name;
  ScopedFd const fd(::openat(directory.fd.get(), directory.file_name.c_str(), O_RDONLY | O_NONBLOCK | O_CLOEXEC | O_NOFOLLOW));
  if (fd.get() < 0)
  {
    if (errno == ENOENT)
      return std::nullopt;
    auto error = rule_file_error(ava::core::ErrorCategory::Io, "failed to open permission rules file", path);
    error.with_context("cause", errno_message());
    return std::unexpected(std::move(error));
  }

  struct stat st{};
  if (::fstat(fd.get(), &st) != 0)
  {
    auto error = rule_file_error(ava::core::ErrorCategory::Io, "failed to inspect opened permission rules file", path);
    error.with_context("cause", errno_message());
    return std::unexpected(std::move(error));
  }
  if (!S_ISREG(st.st_mode) || st.st_uid != ::geteuid() || (st.st_mode & 07777) != (S_IRUSR | S_IWUSR))
  {
    auto error = rule_file_error(ava::core::ErrorCategory::PermissionDenied,
                                 "permission rules file must be a current-user-owned regular file with exact mode 0600", path);
    error.with_context("expected_permissions", "0600");
    return std::unexpected(std::move(error));
  }
  if (st.st_size < 0 || static_cast<std::uintmax_t>(st.st_size) > kMaxPermissionRulesFileBytes)
  {
    auto error = rule_file_error(ava::core::ErrorCategory::Io, "permission rules file is too large", path);
    error.with_context("max_bytes", std::to_string(kMaxPermissionRulesFileBytes));
    return std::unexpected(std::move(error));
  }

  std::string content;
  std::array<char, 4096> buffer{};
  while (true)
  {
    auto const bytes_read = ::read(fd.get(), buffer.data(), buffer.size());
    if (bytes_read == 0)
      break;
    if (bytes_read < 0)
    {
      auto error = rule_file_error(ava::core::ErrorCategory::Io, "failed while reading permission rules file", path);
      error.with_context("cause", errno_message());
      return std::unexpected(std::move(error));
    }
    content.append(buffer.data(), static_cast<std::size_t>(bytes_read));
    if (content.size() > kMaxPermissionRulesFileBytes)
    {
      auto error = rule_file_error(ava::core::ErrorCategory::Io, "permission rules file is too large", path);
      error.with_context("max_bytes", std::to_string(kMaxPermissionRulesFileBytes));
      return std::unexpected(std::move(error));
    }
  }
  return content;
}

void skip_ws(std::string_view text, std::size_t& index)
{
  while (index < text.size() && std::isspace(static_cast<unsigned char>(text[index])) != 0) ++index;
}

std::optional<std::string> balanced_json(std::string_view text, std::size_t start, char open, char close)
{
  if (start >= text.size() || text[start] != open)
    return std::nullopt;
  bool in_string = false;
  bool escaped = false;
  int depth = 0;
  for (std::size_t index = start; index < text.size(); ++index)
  {
    char const ch = text[index];
    if (escaped)
    {
      escaped = false;
      continue;
    }
    if (ch == '\\' && in_string)
    {
      escaped = true;
      continue;
    }
    if (ch == '"')
    {
      in_string = !in_string;
      continue;
    }
    if (in_string)
      continue;
    if (ch == open)
      ++depth;
    if (ch == close)
    {
      --depth;
      if (depth == 0)
        return std::string(text.substr(start, index - start + 1));
      if (depth < 0)
        return std::nullopt;
    }
  }
  return std::nullopt;
}

ava::core::Result<std::vector<std::string>> v2_rule_member_names(std::string_view object, std::filesystem::path const& path, std::size_t rule_index)
{
  std::vector<std::string> names;
  std::size_t index = 1;
  while (index + 1 < object.size())
  {
    skip_ws(object, index);
    if (index + 1 >= object.size() || object[index] == '}')
      break;
    if (object[index] != '"')
      return std::unexpected(rule_parse_error("schema-v2 permission rule has malformed member name", path, "rules", rule_index));
    ++index;
    std::string name;
    while (index < object.size() && object[index] != '"')
    {
      if (object[index] == '\\')
        return std::unexpected(rule_parse_error("schema-v2 permission rule member names must be unescaped ASCII", path, "rules", rule_index));
      name.push_back(object[index++]);
    }
    if (index >= object.size())
      return std::unexpected(rule_parse_error("schema-v2 permission rule has malformed member name", path, "rules", rule_index));
    ++index;
    skip_ws(object, index);
    if (index >= object.size() || object[index] != ':')
      return std::unexpected(rule_parse_error("schema-v2 permission rule has malformed member separator", path, "rules", rule_index));
    ++index;
    skip_ws(object, index);
    bool in_string = false;
    bool escaped = false;
    int depth = 0;
    for (; index < object.size(); ++index)
    {
      char const ch = object[index];
      if (escaped)
      {
        escaped = false;
        continue;
      }
      if (in_string && ch == '\\')
      {
        escaped = true;
        continue;
      }
      if (ch == '"')
      {
        in_string = !in_string;
        continue;
      }
      if (in_string)
        continue;
      if (ch == '{' || ch == '[')
        ++depth;
      else if (ch == '}' || ch == ']')
      {
        if (depth == 0)
          break;
        --depth;
      }
      else if (ch == ',' && depth == 0)
        break;
    }
    if (index >= object.size())
      return std::unexpected(rule_parse_error("schema-v2 permission rule has malformed value", path, "rules", rule_index));
    if (std::ranges::find(names, name) != names.end())
      return std::unexpected(rule_parse_error("schema-v2 permission rule has duplicate member", path, name, rule_index));
    names.push_back(std::move(name));
    if (object[index] == ',')
      ++index;
  }
  return names;
}

ava::core::VoidResult validate_v2_rule_members(std::string_view object, std::filesystem::path const& path, std::size_t rule_index)
{
  static constexpr std::array<std::string_view, 15> kAllowed{"rule_id",        "scope",
                                                             "workspace_dir",  "action",
                                                             "operation",      "mode",
                                                             "tool_name",      "target_path",
                                                             "command",        "command_recipe_key",
                                                             "recipe_display", "critical_acknowledged",
                                                             "reason",         "actor",
                                                             "created_at"};
  auto names = v2_rule_member_names(object, path, rule_index);
  if (!names)
    return std::unexpected(std::move(names.error()));
  for (auto const& name : *names)
  {
    if (std::ranges::find(kAllowed, std::string_view(name)) == kAllowed.end())
      return std::unexpected(rule_parse_error("schema-v2 permission rule has unsupported member", path, name, rule_index));
  }
  return {};
}

ava::core::Result<std::vector<std::string>> rule_objects_array(std::string_view document, std::filesystem::path const& path)
{
  auto const start = ava::core::json::field_value_start(document, "rules");
  if (!start)
    return std::unexpected(rule_parse_error("permission rules file is missing rules array", path, "rules"));
  if (*start >= document.size() || document[*start] != '[')
  {
    return std::unexpected(rule_parse_error("permission rules field must be an array", path, "rules"));
  }
  auto const array = balanced_json(document, *start, '[', ']');
  if (!array)
    return std::unexpected(rule_parse_error("permission rules array is malformed", path, "rules"));

  std::vector<std::string> objects;
  std::size_t index = 1;
  while (index + 1 < array->size())
  {
    skip_ws(*array, index);
    if (index + 1 >= array->size() || (*array)[index] == ']')
      break;
    if ((*array)[index] != '{')
    {
      return std::unexpected(rule_parse_error("permission rules array entries must be objects", path, "rules", objects.size()));
    }
    auto object = balanced_json(*array, index, '{', '}');
    if (!object)
      return std::unexpected(rule_parse_error("permission rule object is malformed", path, "rules", objects.size()));
    index += object->size();
    objects.push_back(std::move(*object));
    skip_ws(*array, index);
    if (index + 1 >= array->size() || (*array)[index] == ']')
      break;
    if ((*array)[index] != ',')
    {
      return std::unexpected(rule_parse_error("permission rules array is malformed", path, "rules", objects.size()));
    }
    ++index;
  }
  return objects;
}

ava::core::Result<std::string> required_string_field(std::string_view object, std::string_view key, std::filesystem::path const& path, std::size_t rule_index,
                                                     bool allow_empty = false)
{
  if (!ava::core::json::field_value_start(object, key))
  {
    return std::unexpected(rule_parse_error("permission rule is missing required field", path, key, rule_index));
  }
  auto value = ava::core::json::string_field(object, key);
  if (!value || (!allow_empty && value->empty()))
  {
    return std::unexpected(rule_parse_error("permission rule field must be a non-empty string", path, key, rule_index));
  }
  return *value;
}

ava::core::Result<std::string> optional_string_field(std::string_view object, std::string_view key, std::filesystem::path const& path, std::size_t rule_index)
{
  if (!ava::core::json::field_value_start(object, key))
    return std::string{};
  auto value = ava::core::json::string_field(object, key);
  if (!value)
  {
    return std::unexpected(rule_parse_error("permission rule field must be a string", path, key, rule_index));
  }
  return *value;
}

ava::core::Result<bool> optional_bool_field(std::string_view object, std::string_view key, std::filesystem::path const& path, std::size_t rule_index)
{
  auto const start = ava::core::json::field_value_start(object, key);
  if (!start)
    return false;
  auto const tail = object.substr(*start);
  auto token_ends = [&](std::size_t end) {
    while (end < tail.size() && std::isspace(static_cast<unsigned char>(tail[end])) != 0) ++end;
    return end == tail.size() || tail[end] == ',' || tail[end] == '}';
  };
  if (tail.starts_with("true") && token_ends(4))
    return true;
  if (tail.starts_with("false") && token_ends(5))
    return false;
  return std::unexpected(rule_parse_error("permission rule field must be a boolean", path, key, rule_index));
}

ava::core::Result<long long> exact_integer_field(std::string_view object, std::string_view key, std::filesystem::path const& path)
{
  auto const start = ava::core::json::field_value_start(object, key);
  if (!start)
    return std::unexpected(rule_parse_error("permission rules file is missing integer field", path, key));
  std::size_t end = *start;
  if (end < object.size() && object[end] == '-')
    ++end;
  auto const digits_start = end;
  while (end < object.size() && std::isdigit(static_cast<unsigned char>(object[end])) != 0) ++end;
  if (end == digits_start)
  {
    return std::unexpected(rule_parse_error("permission rules integer field is malformed", path, key));
  }
  while (end < object.size() && std::isspace(static_cast<unsigned char>(object[end])) != 0) ++end;
  if (end < object.size() && object[end] != ',' && object[end] != '}')
  {
    return std::unexpected(rule_parse_error("permission rules integer field is malformed", path, key));
  }
  try
  {
    return std::stoll(std::string(object.substr(*start, end - *start)));
  }
  catch (...)
  {
    return std::unexpected(rule_parse_error("permission rules integer field is out of range", path, key));
  }
}

ava::core::Result<std::filesystem::path> normalize_rule_target_path(PermissionRuleStore const& store, PermissionRuleScope scope,
                                                                    std::filesystem::path const& target_path)
{
  if (target_path.empty())
    return std::filesystem::path{};
  auto candidate = target_path;
  if (candidate.is_relative())
  {
    if (scope == PermissionRuleScope::Global)
    {
      auto error = ava::core::Error(ava::core::ErrorCategory::InvalidArgument, "global permission path rules require an absolute target_path");
      error.with_context("target_path", target_path.string());
      return std::unexpected(std::move(error));
    }
    if (contains_parent_reference(candidate))
    {
      auto error = ava::core::Error(ava::core::ErrorCategory::InvalidArgument, "relative permission rule target_path must stay within the workspace");
      error.with_context("target_path", target_path.string());
      return std::unexpected(std::move(error));
    }
    candidate = store.workspace_dir / candidate;
  }
  return normalized_path(candidate);
}

ava::core::VoidResult validate_rule(PersistentPermissionRule const& rule, std::filesystem::path const& path,
                                    std::optional<std::size_t> rule_index = std::nullopt)
{
  if (!valid_identifier(rule.rule_id))
  {
    auto error = rule_parse_error("permission rule id is invalid", path, "rule_id", rule_index);
    error.with_context("rule_id", rule.rule_id);
    return std::unexpected(std::move(error));
  }
  if (rule.action == PermissionAction::Ask)
  {
    return std::unexpected(rule_parse_error("persistent permission rules only support allow or deny", path, "action", rule_index));
  }
  if (rule.schema_version != kLegacyPermissionRulesSchemaVersion && rule.schema_version != kCurrentPermissionRulesSchemaVersion)
  {
    return std::unexpected(rule_parse_error("permission rule schema_version is invalid", path, "schema_version", rule_index));
  }
  if (rule.critical_acknowledged && (rule.operation != Operation::RunCommand || rule.action != PermissionAction::Allow))
  {
    return std::unexpected(rule_parse_error("critical_acknowledged is valid only for exact RunCommand allows", path, "critical_acknowledged", rule_index));
  }
  if (rule.critical_acknowledged && is_repository_controlled_build_or_test_command(rule.command))
  {
    return std::unexpected(rule_parse_error("critical_acknowledged cannot authorize repository-controlled cmake build or ctest commands", path,
                                            "critical_acknowledged", rule_index));
  }
  if (!rule.actor.empty() && (!valid_identifier(rule.actor) || has_control_byte(rule.actor)))
  {
    return std::unexpected(rule_parse_error("permission rule actor is invalid", path, "actor", rule_index));
  }
  if (rule.reason.empty() || rule.reason.size() > kMaxPermissionRuleReasonBytes || has_control_byte(rule.reason) || !valid_utf8(rule.reason))
  {
    return std::unexpected(rule_parse_error("permission rule reason is invalid", path, "reason", rule_index));
  }
  if (!rule.tool_name.empty() && (!valid_identifier(rule.tool_name) || has_control_byte(rule.tool_name)))
  {
    return std::unexpected(rule_parse_error("permission rule tool_name is invalid", path, "tool_name", rule_index));
  }
  if (rule.command.size() > kMaxPermissionRuleCommandBytes || has_control_byte(rule.command) || !valid_utf8(rule.command))
  {
    return std::unexpected(rule_parse_error("permission rule command is invalid", path, "command", rule_index));
  }
  if (rule.schema_version == kLegacyPermissionRulesSchemaVersion &&
      (!rule.command_recipe_key.empty() || !rule.recipe_display.empty() || rule.critical_acknowledged))
  {
    return std::unexpected(rule_parse_error("schema-v1 rules cannot contain stable command recipe fields", path, "command_recipe_key", rule_index));
  }
  if (rule.command_recipe_key.size() > kMaxPermissionRecipeKeyBytes || has_control_byte(rule.command_recipe_key) ||
      (!rule.command_recipe_key.empty() && !valid_recipe_key(rule.command_recipe_key, rule.scope)))
  {
    return std::unexpected(rule_parse_error("permission rule command_recipe_key is invalid for its scope", path, "command_recipe_key", rule_index));
  }
  if (rule.recipe_display.size() > kMaxPermissionRecipeDisplayBytes || has_control_byte(rule.recipe_display) || !valid_utf8(rule.recipe_display))
  {
    return std::unexpected(rule_parse_error("permission rule recipe_display is invalid", path, "recipe_display", rule_index));
  }
  if (rule.scope == PermissionRuleScope::Workspace && rule.workspace_dir.empty())
  {
    return std::unexpected(rule_parse_error("workspace permission rule is missing workspace_dir", path, "workspace_dir", rule_index));
  }
  if (is_path_operation(rule.operation) && rule.target_path.empty())
  {
    return std::unexpected(rule_parse_error("path permission rule is missing target_path", path, "target_path", rule_index));
  }
  if (rule.operation == Operation::RunCommand)
  {
    if (rule.action == PermissionAction::Allow && rule.schema_version == kCurrentPermissionRulesSchemaVersion)
    {
      bool const recipe_allow = !rule.command_recipe_key.empty() && !rule.critical_acknowledged;
      bool const exact_critical_allow = rule.command_recipe_key.empty() && rule.critical_acknowledged && !rule.command.empty();
      if (!recipe_allow && !exact_critical_allow)
      {
        return std::unexpected(rule_parse_error("schema-v2 command allow requires an exact scope recipe key, or critical_acknowledged=true with exact command",
                                                path, "command_recipe_key", rule_index));
      }
    }
    if (rule.action == PermissionAction::Deny && rule.command.empty() && rule.command_recipe_key.empty())
    {
      return std::unexpected(rule_parse_error("command deny rule requires exact command or recipe key", path, "command", rule_index));
    }
    if (rule.schema_version == kLegacyPermissionRulesSchemaVersion && rule.action == PermissionAction::Allow && rule.command.empty())
    {
      return std::unexpected(rule_parse_error("legacy command allow is missing exact command", path, "command", rule_index));
    }
  }
  else if (is_command_operation(rule.operation) && rule.command.empty())
  {
    return std::unexpected(rule_parse_error("command permission rule is missing command", path, "command", rule_index));
  }
  if (!is_path_operation(rule.operation) && !is_command_operation(rule.operation) && rule.tool_name.empty())
  {
    return std::unexpected(rule_parse_error("permission rule is missing tool_name", path, "tool_name", rule_index));
  }
  return {};
}

ava::core::Result<PersistentPermissionRule> parse_rule_object(std::string_view object, std::filesystem::path const& path, PermissionRuleScope file_scope,
                                                              long long schema_version, std::size_t rule_index)
{
  if (!ava::core::json::is_valid_object(object))
  {
    return std::unexpected(rule_parse_error("permission rule object is not valid JSON", path, "rules", rule_index));
  }
  if (schema_version == kCurrentPermissionRulesSchemaVersion)
  {
    if (auto members = validate_v2_rule_members(object, path, rule_index); !members)
      return std::unexpected(std::move(members.error()));
  }
  auto id = required_string_field(object, "rule_id", path, rule_index);
  if (!id)
    return std::unexpected(std::move(id.error()));
  auto scope_text = required_string_field(object, "scope", path, rule_index);
  if (!scope_text)
    return std::unexpected(std::move(scope_text.error()));
  auto scope = parse_permission_rule_scope(*scope_text);
  if (!scope)
    return std::unexpected(rule_parse_error("permission rule has unsupported scope", path, "scope", rule_index));
  if (*scope != file_scope)
  {
    return std::unexpected(rule_parse_error("permission rule scope does not match its storage file", path, "scope", rule_index));
  }
  auto action_text = required_string_field(object, "action", path, rule_index);
  if (!action_text)
    return std::unexpected(std::move(action_text.error()));
  auto action = parse_permission_action(*action_text);
  if (!action || *action == PermissionAction::Ask)
  {
    return std::unexpected(rule_parse_error("permission rule has unsupported action", path, "action", rule_index));
  }
  auto operation_text = required_string_field(object, "operation", path, rule_index);
  if (!operation_text)
    return std::unexpected(std::move(operation_text.error()));
  auto operation = parse_operation(*operation_text);
  if (!operation)
  {
    return std::unexpected(rule_parse_error("permission rule has unsupported operation", path, "operation", rule_index));
  }
  auto mode_text = required_string_field(object, "mode", path, rule_index);
  if (!mode_text)
    return std::unexpected(std::move(mode_text.error()));
  auto mode = parse_permission_rule_mode(*mode_text);
  if (!mode)
    return std::unexpected(rule_parse_error("permission rule has unsupported mode", path, "mode", rule_index));
  auto reason = required_string_field(object, "reason", path, rule_index);
  if (!reason)
    return std::unexpected(std::move(reason.error()));
  auto actor = required_string_field(object, "actor", path, rule_index);
  if (!actor)
    return std::unexpected(std::move(actor.error()));
  auto created_at = required_string_field(object, "created_at", path, rule_index);
  if (!created_at)
    return std::unexpected(std::move(created_at.error()));
  auto workspace_dir = optional_string_field(object, "workspace_dir", path, rule_index);
  if (!workspace_dir)
    return std::unexpected(std::move(workspace_dir.error()));
  auto target_path = optional_string_field(object, "target_path", path, rule_index);
  if (!target_path)
    return std::unexpected(std::move(target_path.error()));
  auto command = optional_string_field(object, "command", path, rule_index);
  if (!command)
    return std::unexpected(std::move(command.error()));
  auto tool_name = optional_string_field(object, "tool_name", path, rule_index);
  if (!tool_name)
    return std::unexpected(std::move(tool_name.error()));
  auto command_recipe_key = optional_string_field(object, "command_recipe_key", path, rule_index);
  if (!command_recipe_key)
    return std::unexpected(std::move(command_recipe_key.error()));
  auto recipe_display = optional_string_field(object, "recipe_display", path, rule_index);
  if (!recipe_display)
    return std::unexpected(std::move(recipe_display.error()));
  auto critical_acknowledged = optional_bool_field(object, "critical_acknowledged", path, rule_index);
  if (!critical_acknowledged)
    return std::unexpected(std::move(critical_acknowledged.error()));
  if (!workspace_dir->empty() && std::filesystem::path(*workspace_dir).is_relative())
  {
    return std::unexpected(rule_parse_error("permission rule workspace_dir must be absolute", path, "workspace_dir", rule_index));
  }
  if (!target_path->empty() && std::filesystem::path(*target_path).is_relative())
  {
    return std::unexpected(rule_parse_error("permission rule target_path must be absolute", path, "target_path", rule_index));
  }

  PersistentPermissionRule rule{.rule_id = std::move(*id),
                                .scope = *scope,
                                .workspace_dir = workspace_dir->empty() ? std::filesystem::path{} : normalized_path(*workspace_dir),
                                .action = *action,
                                .operation = *operation,
                                .mode = *mode,
                                .tool_name = std::move(*tool_name),
                                .target_path = target_path->empty() ? std::filesystem::path{} : normalized_path(*target_path),
                                .command = std::move(*command),
                                .command_recipe_key = std::move(*command_recipe_key),
                                .recipe_display = std::move(*recipe_display),
                                .critical_acknowledged = *critical_acknowledged,
                                .schema_version = schema_version,
                                .reason = std::move(*reason),
                                .actor = std::move(*actor),
                                .created_at = std::move(*created_at)};
  if (auto valid = validate_rule(rule, path, rule_index); !valid)
    return std::unexpected(std::move(valid.error()));
  return rule;
}

ava::core::Result<std::vector<PersistentPermissionRule>> parse_rules_file(std::string_view content, std::filesystem::path const& path,
                                                                          PermissionRuleScope file_scope)
{
  if (!ava::core::json::is_valid_object(content))
  {
    return std::unexpected(rule_parse_error("permission rules file is not a valid JSON object", path));
  }
  auto const version = exact_integer_field(content, "schema_version", path);
  if (!version)
  {
    return std::unexpected(std::move(version.error()));
  }
  if (*version != kLegacyPermissionRulesSchemaVersion && *version != kCurrentPermissionRulesSchemaVersion)
  {
    auto error = rule_parse_error("unsupported permission rules schema_version", path, "schema_version");
    error.with_context("schema_version", std::to_string(*version));
    error.with_context("supported_schema_version", std::to_string(kCurrentPermissionRulesSchemaVersion));
    return std::unexpected(std::move(error));
  }
  if (*version == kCurrentPermissionRulesSchemaVersion)
  {
    static constexpr std::array<std::string_view, 2> kAllowed{"schema_version", "rules"};
    auto names = v2_rule_member_names(content, path, 0);
    if (!names)
      return std::unexpected(std::move(names.error()));
    for (auto const& name : *names)
    {
      if (std::ranges::find(kAllowed, std::string_view(name)) == kAllowed.end())
        return std::unexpected(rule_parse_error("schema-v2 permission rules file has unsupported member", path, name));
    }
  }

  auto objects = rule_objects_array(content, path);
  if (!objects)
    return std::unexpected(std::move(objects.error()));
  std::vector<PersistentPermissionRule> rules;
  rules.reserve(objects->size());
  for (std::size_t index = 0; index < objects->size(); ++index)
  {
    auto rule = parse_rule_object((*objects)[index], path, file_scope, *version, index);
    if (!rule)
      return std::unexpected(std::move(rule.error()));
    rules.push_back(std::move(*rule));
  }
  return rules;
}

ava::core::Result<std::vector<PersistentPermissionRule>> load_scope_rules(PermissionRuleStore const& store, PermissionRuleScope scope,
                                                                          RuleDirectory const* locked_directory = nullptr)
{
  auto const path = rules_file_path(store, scope);
  if (locked_directory)
  {
    auto content = read_rules_text_if_exists(*locked_directory);
    if (!content)
      return std::unexpected(std::move(content.error()));
    if (!*content)
      return std::vector<PersistentPermissionRule>{};
    return parse_rules_file(**content, path, scope);
  }

  auto directory = open_rule_directory_for_read(path, store.anchor_set);
  if (!directory)
    return std::unexpected(std::move(directory.error()));
  if (!*directory)
    return std::vector<PersistentPermissionRule>{};
  auto content = read_rules_text_if_exists(**directory);
  if (!content)
    return std::unexpected(std::move(content.error()));
  if (!*content)
    return std::vector<PersistentPermissionRule>{};
  return parse_rules_file(**content, path, scope);
}

bool contains_legacy_command_allow(std::vector<PersistentPermissionRule> const& rules);

std::string rules_file_json(std::vector<PersistentPermissionRule> const& rules)
{
  // A v1 command Allow cannot be represented in schema v2. Preserve a valid
  // v1 document until the final such Allow is explicitly removed; the next
  // write then deliberately migrates the remaining rules to v2.
  auto const schema_version = contains_legacy_command_allow(rules) ? kLegacyPermissionRulesSchemaVersion : kCurrentPermissionRulesSchemaVersion;
  std::string json = "{\"schema_version\":";
  json += std::to_string(schema_version);
  json += ",\"rules\":[";
  for (std::size_t index = 0; index < rules.size(); ++index)
  {
    if (index > 0)
      json += ',';
    auto serialized_rule = rules[index];
    serialized_rule.schema_version = schema_version;
    json += permission_rule_json(serialized_rule);
  }
  json += "]}\n";
  return json;
}

PermissionRuleDraft normalize_draft_defaults(PermissionRuleDraft draft)
{
  if (draft.actor.empty())
    draft.actor = "rpc";
  return draft;
}

bool is_repository_build_or_test_allow(PersistentPermissionRule const& rule);
std::string now_timestamp();

ava::core::Result<PersistentPermissionRule> rule_from_draft(PermissionRuleStore const& store, PermissionRuleDraft draft)
{
  draft = normalize_draft_defaults(std::move(draft));
  auto normalized_target = normalize_rule_target_path(store, draft.scope, draft.target_path);
  if (!normalized_target)
    return std::unexpected(std::move(normalized_target.error()));

  PersistentPermissionRule rule{.rule_id = ava::core::make_id("permrule"),
                                .scope = draft.scope,
                                .workspace_dir = draft.scope == PermissionRuleScope::Workspace ? normalized_path(store.workspace_dir) : std::filesystem::path{},
                                .action = draft.action,
                                .operation = draft.operation,
                                .mode = draft.mode,
                                .tool_name = std::move(draft.tool_name),
                                .target_path = std::move(*normalized_target),
                                .command = std::move(draft.command),
                                .command_recipe_key = std::move(draft.command_recipe_key),
                                .recipe_display = std::move(draft.recipe_display),
                                .critical_acknowledged = draft.critical_acknowledged,
                                .schema_version = kCurrentPermissionRulesSchemaVersion,
                                .reason = std::move(draft.reason),
                                .actor = std::move(draft.actor),
                                .created_at = now_timestamp()};
  auto const path = rules_file_path(store, rule.scope);
  if (auto valid = validate_rule(rule, path); !valid)
    return std::unexpected(std::move(valid.error()));
  if (rule.operation == Operation::RunCommand && rule.action == PermissionAction::Allow && rule.critical_acknowledged)
  {
    return std::unexpected(rule_parse_error("Critical command approvals are one-shot and cannot be persisted", path, "critical_acknowledged"));
  }
  if (is_repository_build_or_test_allow(rule))
  {
    return std::unexpected(rule_parse_error(
        "persistent allow rules require a stable command recipe key; raw repository build/test command text is not authorizing", path, "command_recipe_key"));
  }
  return rule;
}

bool mode_matches(PermissionRuleMode rule_mode, ava::agent::Mode prompt_mode)
{
  switch (rule_mode)
  {
    case PermissionRuleMode::Any:
      return true;
    case PermissionRuleMode::Build:
      return prompt_mode == ava::agent::Mode::Build;
    case PermissionRuleMode::Plan:
      return prompt_mode == ava::agent::Mode::Plan;
  }
  return false;
}

bool scope_matches(PermissionRuleStore const& store, PersistentPermissionRule const& rule, PermissionPrompt const& prompt)
{
  if (rule.scope == PermissionRuleScope::Global)
    return true;
  auto const store_workspace = normalized_path(store.workspace_dir);
  auto const prompt_workspace = normalized_path(prompt.workspace_dir);
  return rule.workspace_dir == store_workspace && prompt_workspace == store_workspace;
}

bool path_matches(PermissionRuleStore const& store, PersistentPermissionRule const& rule, PermissionPrompt const& prompt)
{
  if (rule.target_path.empty())
    return true;
  if (prompt.target_path.empty())
    return false;
  auto const target = normalize_rule_target_path(store, rule.scope, prompt.target_path);
  return target && *target == rule.target_path;
}

bool command_recipe_matches(PersistentPermissionRule const& rule, PermissionPrompt const& prompt)
{
  if (rule.command_recipe_key.empty())
    return true;
  if (!prompt.command_metadata)
    return false;
  auto const& key = rule.scope == PermissionRuleScope::Global ? prompt.command_metadata->global_recipe_key : prompt.command_metadata->workspace_recipe_key;
  return !key.empty() && key == rule.command_recipe_key;
}

bool rule_matches(PermissionRuleStore const& store, PersistentPermissionRule const& rule, PermissionPrompt const& prompt)
{
  if (rule.operation != prompt.operation)
    return false;
  if (!mode_matches(rule.mode, prompt.mode))
    return false;
  if (!scope_matches(store, rule, prompt))
    return false;
  if (!rule.tool_name.empty() && rule.tool_name != prompt.tool_name)
    return false;
  if (!command_recipe_matches(rule, prompt))
    return false;
  bool const typed_command_recipe = rule.operation == Operation::RunCommand && !rule.command_recipe_key.empty();
  if (!typed_command_recipe && !rule.command.empty() && rule.command != prompt.command)
    return false;
  return path_matches(store, rule, prompt);
}

bool command_allow_is_authoritative(PersistentPermissionRule const& rule, PermissionPrompt const& prompt)
{
  if (rule.operation != Operation::RunCommand || rule.action != PermissionAction::Allow || rule.schema_version != kCurrentPermissionRulesSchemaVersion ||
      !prompt.command_metadata)
    return false;
  if (!rule.command_recipe_key.empty())
    return command_prompt_allows_persistent_allow(prompt) && command_recipe_matches(rule, prompt);
  // Schema-v2 exact Critical acknowledgements remain parseable so users can
  // remove old rules, but backend Once is an absolute maximum: no command text
  // can recover persistent execution authority.
  return false;
}

bool is_repository_build_or_test_allow(PersistentPermissionRule const& rule)
{
  return rule.action == PermissionAction::Allow && rule.operation == Operation::RunCommand && rule.command_recipe_key.empty() && !rule.critical_acknowledged &&
         is_repository_controlled_build_or_test_command(rule.command);
}

bool contains_legacy_command_allow(std::vector<PersistentPermissionRule> const& rules)
{
  return std::ranges::any_of(rules, [](PersistentPermissionRule const& rule) {
    return rule.schema_version == kLegacyPermissionRulesSchemaVersion && rule.operation == Operation::RunCommand && rule.action == PermissionAction::Allow;
  });
}

bool workspace_rule(PersistentPermissionRule const& rule)
{
  return rule.scope == PermissionRuleScope::Workspace;
}

int rule_specificity(PersistentPermissionRule const& rule)
{
  int specificity = 0;
  if (!rule.target_path.empty())
    ++specificity;
  if (!rule.command.empty())
    ++specificity;
  if (!rule.command_recipe_key.empty())
    ++specificity;
  if (!rule.tool_name.empty())
    ++specificity;
  if (rule.mode != PermissionRuleMode::Any)
    ++specificity;
  return specificity;
}

std::string now_timestamp()
{
  auto const now = std::chrono::system_clock::now();
  auto const time = std::chrono::system_clock::to_time_t(now);
  std::tm tm{};
  gmtime_r(&time, &tm);
  std::ostringstream out;
  out << std::put_time(&tm, "%Y-%m-%dT%H:%M:%SZ");
  return out.str();
}

PersistentPermissionRule const& prefer_more_specific(PersistentPermissionRule const& current, PersistentPermissionRule const& candidate)
{
  if (!workspace_rule(current) && workspace_rule(candidate))
    return candidate;
  if (workspace_rule(current) == workspace_rule(candidate) && rule_specificity(candidate) > rule_specificity(current))
    return candidate;
  return current;
}

}  // namespace

std::optional<PermissionRuleScope> parse_permission_rule_scope(std::string_view value)
{
  if (value == "global")
    return PermissionRuleScope::Global;
  if (value == "workspace")
    return PermissionRuleScope::Workspace;
  return std::nullopt;
}

std::optional<PermissionRuleMode> parse_permission_rule_mode(std::string_view value)
{
  if (value == "any")
    return PermissionRuleMode::Any;
  if (value == "build")
    return PermissionRuleMode::Build;
  if (value == "plan")
    return PermissionRuleMode::Plan;
  return std::nullopt;
}

std::string to_string(PermissionRuleScope scope)
{
  switch (scope)
  {
    case PermissionRuleScope::Global:
      return "global";
    case PermissionRuleScope::Workspace:
      return "workspace";
  }
  return "workspace";
}

std::string to_string(PermissionRuleMode mode)
{
  switch (mode)
  {
    case PermissionRuleMode::Any:
      return "any";
    case PermissionRuleMode::Build:
      return "build";
    case PermissionRuleMode::Plan:
      return "plan";
  }
  return "any";
}

std::filesystem::path enforceable_permission_rules_file(PermissionRuleStore const& store, PermissionRuleScope scope)
{
  if (scope == PermissionRuleScope::Global)
    return store.global_rules_file;

  auto base_dir = store.global_rules_file.parent_path();
  if (base_dir.empty())
    base_dir = store.workspace_rules_file.parent_path();
  return base_dir / "workspace-permission-rules" / workspace_rules_key(store.workspace_dir) / "permission-rules.json";
}

bool is_enforceable_permission_rules_file(PermissionRuleStore const& store, std::filesystem::path const& path)
{
  if (path.empty())
    return false;
  if (!store.global_rules_file.empty() &&
      paths_refer_to_same_file(path, enforceable_permission_rules_file(store, PermissionRuleScope::Global)))
    return true;
  return !store.workspace_dir.empty() &&
         paths_refer_to_same_file(path, enforceable_permission_rules_file(store, PermissionRuleScope::Workspace));
}

void register_enforceable_permission_rule_files(PermissionRuleStore const& store)
{
  std::lock_guard lock(protected_rule_paths_mutex());
  auto& paths = protected_rule_paths();
  if (!store.global_rules_file.empty())
  {
    append_protected_rule_path(paths, enforceable_permission_rules_file(store, PermissionRuleScope::Global));
  }
  if (!store.workspace_dir.empty())
  {
    append_protected_rule_path(paths, enforceable_permission_rules_file(store, PermissionRuleScope::Workspace));
  }
}

bool is_registered_enforceable_permission_rules_file(std::filesystem::path const& path)
{
  if (path.empty())
    return false;
  std::lock_guard lock(protected_rule_paths_mutex());
  auto const& paths = protected_rule_paths();
  return std::ranges::any_of(paths, [&](std::filesystem::path const& registered) { return paths_refer_to_same_file(path, registered); });
}

ava::core::Result<std::vector<PersistentPermissionRule>> load_persistent_permission_rules(PermissionRuleStore const& store)
{
  register_enforceable_permission_rule_files(store);
  auto global = load_scope_rules(store, PermissionRuleScope::Global);
  if (!global)
    return std::unexpected(std::move(global.error()));
  auto workspace = load_scope_rules(store, PermissionRuleScope::Workspace);
  if (!workspace)
    return std::unexpected(std::move(workspace.error()));
  global->insert(global->end(), std::make_move_iterator(workspace->begin()), std::make_move_iterator(workspace->end()));
  return *global;
}

ava::core::Result<PersistentPermissionRule> add_persistent_permission_rule(PermissionRuleStore const& store, PermissionRuleDraft draft)
{
  register_enforceable_permission_rule_files(store);
  auto rule = rule_from_draft(store, std::move(draft));
  if (!rule)
    return std::unexpected(std::move(rule.error()));

  auto const scope = rule->scope;
  auto const path = rules_file_path(store, scope);
  auto lock = acquire_rule_lock(path, store.anchor_set);
  if (!lock)
    return std::unexpected(std::move(lock.error()));
  auto rules = load_scope_rules(store, scope, &lock->directory);
  if (!rules)
    return std::unexpected(std::move(rules.error()));
  if (contains_legacy_command_allow(*rules))
  {
    return std::unexpected(
        rule_parse_error("refusing to rewrite schema-v1 command Allows; remove them explicitly before adding schema-v2 rules", path, "schema_version"));
  }
  rules->push_back(*rule);
  if (auto written = write_rules_file_atomic(lock->directory, rules_file_json(*rules)); !written)
  {
    return std::unexpected(std::move(written.error()));
  }
  return *rule;
}

ava::core::Result<PersistentPermissionRule> remove_persistent_permission_rule(PermissionRuleStore const& store, std::string_view rule_id)
{
  register_enforceable_permission_rule_files(store);
  if (!valid_identifier(rule_id))
  {
    auto error = ava::core::Error(ava::core::ErrorCategory::InvalidArgument, "permission_rule_remove requires valid rule_id");
    error.with_context("rule_id", std::string(rule_id));
    return std::unexpected(std::move(error));
  }

  for (auto const scope : {PermissionRuleScope::Workspace, PermissionRuleScope::Global})
  {
    auto const path = rules_file_path(store, scope);
    auto lock = acquire_rule_lock(path, store.anchor_set);
    if (!lock)
      return std::unexpected(std::move(lock.error()));
    auto rules = load_scope_rules(store, scope, &lock->directory);
    if (!rules)
      return std::unexpected(std::move(rules.error()));
    auto found = rules->end();
    for (auto it = rules->begin(); it != rules->end(); ++it)
    {
      if (it->rule_id == rule_id)
      {
        found = it;
        break;
      }
    }
    if (found == rules->end())
      continue;
    auto removed = *found;
    rules->erase(found);
    if (auto written = write_rules_file_atomic(lock->directory, rules_file_json(*rules)); !written)
    {
      return std::unexpected(std::move(written.error()));
    }
    return removed;
  }

  auto error = ava::core::Error(ava::core::ErrorCategory::NotFound, "permission_rule_remove has no matching rule_id");
  error.with_context("rule_id", std::string(rule_id));
  return std::unexpected(std::move(error));
}

ava::core::Result<std::optional<PersistentPermissionRule>> match_persistent_permission_rule(PermissionRuleStore const& store, PermissionPrompt const& prompt)
{
  auto rules = load_persistent_permission_rules(store);
  if (!rules)
    return std::unexpected(std::move(rules.error()));

  std::optional<PersistentPermissionRule> matched_deny;
  std::optional<PersistentPermissionRule> matched_allow;
  for (auto const& rule : *rules)
  {
    if (!rule_matches(store, rule, prompt))
      continue;
    if (rule.action == PermissionAction::Deny)
    {
      matched_deny = matched_deny ? prefer_more_specific(*matched_deny, rule) : rule;
      continue;
    }
    if (rule.operation == Operation::RunCommand && !command_allow_is_authoritative(rule, prompt))
      continue;
    matched_allow = matched_allow ? prefer_more_specific(*matched_allow, rule) : rule;
  }
  if (matched_deny)
    return matched_deny;
  if (matched_allow)
    return matched_allow;
  return std::optional<PersistentPermissionRule>{};
}

PermissionResolver build_persistent_permission_rule_resolver(PermissionRuleStore store, PermissionResolver fallback)
{
  register_enforceable_permission_rule_files(store);
  return [store = std::move(store), fallback = std::move(fallback)](PermissionPrompt const& prompt) -> ava::core::Result<PermissionResolutionDecision> {
    auto matched = match_persistent_permission_rule(store, prompt);
    if (!matched)
    {
      PermissionResolutionDecision decision{PermissionResolution::Deny, matched.error().format()};
      decision.resolution_source = "persistent_rule_error";
      decision.authoritative = true;
      return decision;
    }
    if (*matched)
    {
      auto const resolution = (*matched)->action == PermissionAction::Allow ? PermissionResolution::Allow : PermissionResolution::Deny;
      PermissionResolutionDecision decision{resolution, (*matched)->reason};
      decision.resolution_source = "persistent_rule";
      decision.rule_id = (*matched)->rule_id;
      decision.authoritative = true;
      return decision;
    }
    if (fallback)
      return fallback(prompt);
    return PermissionResolution::Deny;
  };
}

PermissionResolver build_persistent_permission_deny_preflight(PermissionRuleStore store)
{
  register_enforceable_permission_rule_files(store);
  return [store = std::move(store)](PermissionPrompt const& prompt) -> ava::core::Result<PermissionResolutionDecision> {
    auto matched = match_persistent_permission_rule(store, prompt);
    if (!matched)
    {
      PermissionResolutionDecision decision{PermissionResolution::Deny, matched.error().format()};
      decision.resolution_source = "persistent_rule_error";
      decision.authoritative = true;
      return decision;
    }
    if (*matched && (*matched)->action == PermissionAction::Deny)
    {
      PermissionResolutionDecision decision{PermissionResolution::Deny, (*matched)->reason};
      decision.resolution_source = "persistent_rule";
      decision.rule_id = (*matched)->rule_id;
      decision.authoritative = true;
      return decision;
    }
    PermissionResolutionDecision decision{PermissionResolution::Allow, "no persistent deny matched"};
    decision.resolution_source = "persistent_deny_preflight";
    decision.authoritative = true;
    return decision;
  };
}

std::string permission_rule_json(PersistentPermissionRule const& rule)
{
  std::string json = "{";
  json += "\"rule_id\":\"" + ava::core::json::escape(rule.rule_id) + "\"";
  json += ",\"scope\":\"" + ava::core::json::escape(to_string(rule.scope)) + "\"";
  json += ",\"workspace_dir\":\"" + ava::core::json::escape(rule.workspace_dir.string()) + "\"";
  json += ",\"action\":\"" + ava::core::json::escape(to_string(rule.action)) + "\"";
  json += ",\"operation\":\"" + ava::core::json::escape(to_string(rule.operation)) + "\"";
  json += ",\"mode\":\"" + ava::core::json::escape(to_string(rule.mode)) + "\"";
  json += ",\"tool_name\":\"" + ava::core::json::escape(rule.tool_name) + "\"";
  json += ",\"target_path\":\"" + ava::core::json::escape(rule.target_path.string()) + "\"";
  json += ",\"command\":\"" + ava::core::json::escape(rule.command) + "\"";
  if (rule.schema_version != kLegacyPermissionRulesSchemaVersion)
  {
    json += ",\"command_recipe_key\":\"" + ava::core::json::escape(rule.command_recipe_key) + "\"";
    json += ",\"recipe_display\":\"" + ava::core::json::escape(rule.recipe_display) + "\"";
    json += ",\"critical_acknowledged\":" + std::string(rule.critical_acknowledged ? "true" : "false");
  }
  json += ",\"reason\":\"" + ava::core::json::escape(rule.reason) + "\"";
  json += ",\"actor\":\"" + ava::core::json::escape(rule.actor) + "\"";
  json += ",\"created_at\":\"" + ava::core::json::escape(rule.created_at) + "\"";
  json += '}';
  return json;
}

std::string permission_rules_result_json(PermissionRuleStore const& store, std::vector<PersistentPermissionRule> const& rules)
{
  std::string json = "{";
  json += "\"schema_version\":" + std::to_string(kCurrentPermissionRulesSchemaVersion);
  json += ",\"global_rules_file\":\"" + ava::core::json::escape(store.global_rules_file.string()) + "\"";
  json += ",\"workspace_rules_file\":\"" + ava::core::json::escape(enforceable_permission_rules_file(store, PermissionRuleScope::Workspace).string()) + "\"";
  json += ",\"rules\":[";
  for (std::size_t index = 0; index < rules.size(); ++index)
  {
    if (index > 0)
      json += ',';
    json += permission_rule_json(rules[index]);
  }
  json += "]}";
  return json;
}

}  // namespace ava::permissions
