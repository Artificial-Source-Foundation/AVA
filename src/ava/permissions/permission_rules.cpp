#include "sys.h"
#include "ava/permissions/permission_rules.h"
#include "ava/core/ids.h"
#include "ava/core/json.h"

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
  explicit TempPathCleanup(std::filesystem::path path) : path_(std::move(path)) { }
  TempPathCleanup(TempPathCleanup const&) = delete;
  TempPathCleanup& operator=(TempPathCleanup const&) = delete;
  TempPathCleanup(TempPathCleanup&& other) noexcept : path_(std::move(other.path_)), active_(std::exchange(other.active_, false)) { }
  TempPathCleanup& operator=(TempPathCleanup&& other) noexcept
  {
    if (this != &other)
    {
      cleanup();
      path_ = std::move(other.path_);
      active_ = std::exchange(other.active_, false);
    }
    return *this;
  }
  ~TempPathCleanup() { cleanup(); }

  void dismiss() noexcept { active_ = false; }

 private:
  void cleanup() noexcept
  {
    if (!active_)
      return;
    std::error_code remove_error;
    std::filesystem::remove(path_, remove_error);
    active_ = false;
  }

  std::filesystem::path path_;
  bool active_ = true;
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
  std::error_code error;
  auto const canonical = std::filesystem::weakly_canonical(path, error);
  if (!error)
    return canonical;
  return std::filesystem::absolute(path).lexically_normal();
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

ava::core::VoidResult ensure_rule_directory(std::filesystem::path const& file_path)
{
  auto const parent = file_path.parent_path();
  std::error_code mkdir_error;
  std::filesystem::create_directories(parent, mkdir_error);
  if (mkdir_error)
  {
    auto error = rule_file_error(ava::core::ErrorCategory::Io, "failed to create permission rules directory", parent);
    error.with_context("cause", mkdir_error.message());
    return std::unexpected(std::move(error));
  }
  std::error_code status_error;
  auto const status = std::filesystem::symlink_status(parent, status_error);
  if (status_error)
  {
    auto error = rule_file_error(ava::core::ErrorCategory::Io, "failed to inspect permission rules directory", parent);
    error.with_context("cause", status_error.message());
    return std::unexpected(std::move(error));
  }
  if (std::filesystem::is_symlink(status) || !std::filesystem::is_directory(status))
  {
    return std::unexpected(rule_file_error(ava::core::ErrorCategory::PermissionDenied, "permission rules directory is not a regular directory", parent));
  }
  if (::chmod(parent.c_str(), S_IRWXU) != 0)
  {
    auto error = rule_file_error(ava::core::ErrorCategory::Io, "failed to set permission rules directory permissions", parent);
    error.with_context("cause", errno_message());
    return std::unexpected(std::move(error));
  }
  return {};
}

ava::core::VoidResult reject_unsafe_replace_target(std::filesystem::path const& path)
{
  std::error_code status_error;
  auto const status = std::filesystem::symlink_status(path, status_error);
  if (status_error)
  {
    if (status_error.default_error_condition() == std::errc::no_such_file_or_directory)
      return {};
    auto error = rule_file_error(ava::core::ErrorCategory::Io, "failed to inspect permission rules file", path);
    error.with_context("cause", status_error.message());
    return std::unexpected(std::move(error));
  }
  if (!std::filesystem::exists(status))
    return {};
  if (std::filesystem::is_symlink(status))
  {
    return std::unexpected(rule_file_error(ava::core::ErrorCategory::PermissionDenied, "permission rules file is a symbolic link", path));
  }
  if (!std::filesystem::is_regular_file(status))
  {
    return std::unexpected(rule_file_error(ava::core::ErrorCategory::PermissionDenied, "permission rules file is not regular", path));
  }
  return {};
}

ava::core::Result<ScopedFd> acquire_rule_lock(std::filesystem::path const& file_path)
{
  if (auto ensured = ensure_rule_directory(file_path); !ensured)
    return std::unexpected(std::move(ensured.error()));

  auto const lock_path = file_path.parent_path() / (file_path.filename().string() + ".lock");
  int flags = O_RDWR | O_CREAT | O_CLOEXEC;
#ifdef O_NOFOLLOW
  flags |= O_NOFOLLOW;
#endif
  ScopedFd fd(::open(lock_path.c_str(), flags, S_IRUSR | S_IWUSR));
  if (fd.get() < 0)
  {
    auto error = rule_file_error(ava::core::ErrorCategory::Io, "failed to open permission rules lock file", lock_path);
    error.with_context("cause", errno_message());
    return std::unexpected(std::move(error));
  }

  struct stat opened_st{};
  if (::fstat(fd.get(), &opened_st) != 0)
  {
    auto error = rule_file_error(ava::core::ErrorCategory::Io, "failed to inspect permission rules lock file", lock_path);
    error.with_context("cause", errno_message());
    return std::unexpected(std::move(error));
  }
  if (!S_ISREG(opened_st.st_mode))
  {
    return std::unexpected(rule_file_error(ava::core::ErrorCategory::PermissionDenied, "permission rules lock file is not regular", lock_path));
  }
  if (::fchmod(fd.get(), S_IRUSR | S_IWUSR) != 0)
  {
    auto error = rule_file_error(ava::core::ErrorCategory::Io, "failed to set permission rules lock permissions", lock_path);
    error.with_context("cause", errno_message());
    return std::unexpected(std::move(error));
  }
  while (::flock(fd.get(), LOCK_EX) != 0)
  {
    if (errno == EINTR)
      continue;
    auto error = rule_file_error(ava::core::ErrorCategory::Io, "failed to lock permission rules file", lock_path);
    error.with_context("cause", errno_message());
    return std::unexpected(std::move(error));
  }
  return fd;
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

void fsync_parent_best_effort(std::filesystem::path const& path)
{
  ScopedFd const dir_fd(::open(path.parent_path().c_str(), O_RDONLY | O_CLOEXEC));
  if (dir_fd.get() < 0)
    return;
  static_cast<void>(::fsync(dir_fd.get()));
}

ava::core::VoidResult write_rules_file_atomic(std::filesystem::path const& path, std::string_view body)
{
  if (auto checked = reject_unsafe_replace_target(path); !checked)
    return checked;

  auto const parent = path.parent_path();
  auto const basename = path.filename().string();
  for (int attempt = 0; attempt < 100; ++attempt)
  {
    auto const temp_path = parent / (basename + ".tmp." + std::to_string(::getpid()) + "." + std::to_string(attempt));
    ScopedFd const fd(::open(temp_path.c_str(), O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC, S_IRUSR | S_IWUSR));
    if (fd.get() < 0)
    {
      if (errno == EEXIST)
        continue;
      auto error = rule_file_error(ava::core::ErrorCategory::Io, "failed to create temporary permission rules file", temp_path);
      error.with_context("cause", errno_message());
      return std::unexpected(std::move(error));
    }
    TempPathCleanup cleanup(temp_path);
    struct stat opened_st{};
    if (::fstat(fd.get(), &opened_st) != 0)
    {
      auto error = rule_file_error(ava::core::ErrorCategory::Io, "failed to inspect temporary permission rules file", temp_path);
      error.with_context("cause", errno_message());
      return std::unexpected(std::move(error));
    }
    if (!S_ISREG(opened_st.st_mode))
    {
      return std::unexpected(rule_file_error(ava::core::ErrorCategory::PermissionDenied, "temporary permission rules file is not regular", temp_path));
    }
    if (::fchmod(fd.get(), S_IRUSR | S_IWUSR) != 0)
    {
      auto error = rule_file_error(ava::core::ErrorCategory::Io, "failed to set temporary permission rules permissions", temp_path);
      error.with_context("cause", errno_message());
      return std::unexpected(std::move(error));
    }
    if (auto written = write_all_to_fd(fd.get(), body, temp_path); !written)
      return written;
    if (auto synced = fsync_fd(fd.get(), temp_path, "failed to sync permission rules file"); !synced)
      return synced;
    if (::rename(temp_path.c_str(), path.c_str()) != 0)
    {
      auto error = rule_file_error(ava::core::ErrorCategory::Io, "failed to replace permission rules file", path);
      error.with_context("cause", errno_message());
      return std::unexpected(std::move(error));
    }
    cleanup.dismiss();
    fsync_parent_best_effort(path);
    return {};
  }
  return std::unexpected(rule_file_error(ava::core::ErrorCategory::Io, "failed to create unique temporary permission rules file", path));
}

ava::core::Result<std::optional<std::string>> read_rules_text_if_exists(std::filesystem::path const& path)
{
  std::error_code status_error;
  auto const status = std::filesystem::symlink_status(path, status_error);
  if (status_error)
  {
    if (status_error.default_error_condition() == std::errc::no_such_file_or_directory)
      return std::nullopt;
    auto error = rule_file_error(ava::core::ErrorCategory::Io, "failed to inspect permission rules file", path);
    error.with_context("cause", status_error.message());
    return std::unexpected(std::move(error));
  }
  if (!std::filesystem::exists(status))
    return std::nullopt;
  if (std::filesystem::is_symlink(status))
  {
    return std::unexpected(rule_file_error(ava::core::ErrorCategory::PermissionDenied, "permission rules file is a symbolic link", path));
  }
  if (!std::filesystem::is_regular_file(status))
  {
    return std::unexpected(rule_file_error(ava::core::ErrorCategory::PermissionDenied, "permission rules file is not regular", path));
  }

  int flags = O_RDONLY | O_CLOEXEC;
#ifdef O_NOFOLLOW
  flags |= O_NOFOLLOW;
#endif
  ScopedFd const fd(::open(path.c_str(), flags));
  if (fd.get() < 0)
  {
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
  if (!S_ISREG(st.st_mode))
  {
    return std::unexpected(rule_file_error(ava::core::ErrorCategory::PermissionDenied, "opened permission rules file is not regular", path));
  }
  if (st.st_uid != ::geteuid())
  {
    return std::unexpected(rule_file_error(ava::core::ErrorCategory::PermissionDenied, "permission rules file is not owned by the current user", path));
  }
  if ((st.st_mode & (S_IRWXG | S_IRWXO)) != 0)
  {
    auto error =
        rule_file_error(ava::core::ErrorCategory::PermissionDenied, "permission rules file permissions are too broad; run `chmod 600` on the file", path);
    error.with_context("expected_permissions", "0600");
    return std::unexpected(std::move(error));
  }
  if (st.st_size < 0 || static_cast<std::uintmax_t>(st.st_size) > kMaxPermissionRulesFileBytes)
  {
    auto error = rule_file_error(ava::core::ErrorCategory::Io, "permission rules file is too large", path);
    error.with_context("max_bytes", std::to_string(kMaxPermissionRulesFileBytes));
    return std::unexpected(std::move(error));
  }

#ifndef O_NOFOLLOW
  struct stat path_st{};
  if (::lstat(path.c_str(), &path_st) != 0)
  {
    auto error = rule_file_error(ava::core::ErrorCategory::Io, "failed to inspect permission rules file", path);
    error.with_context("cause", errno_message());
    return std::unexpected(std::move(error));
  }
  if (S_ISLNK(path_st.st_mode) || path_st.st_dev != st.st_dev || path_st.st_ino != st.st_ino)
  {
    return std::unexpected(rule_file_error(ava::core::ErrorCategory::PermissionDenied, "permission rules file changed during open", path));
  }
#endif

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

ava::core::Result<std::vector<PersistentPermissionRule>> load_scope_rules(PermissionRuleStore const& store, PermissionRuleScope scope)
{
  auto const path = rules_file_path(store, scope);
  auto content = read_rules_text_if_exists(path);
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
  if (!rule.command.empty() && rule.command != prompt.command)
    return false;
  return path_matches(store, rule, prompt);
}

bool command_allow_is_authoritative(PersistentPermissionRule const& rule, PermissionPrompt const& prompt)
{
  if (rule.operation != Operation::RunCommand || rule.action != PermissionAction::Allow || rule.schema_version != kCurrentPermissionRulesSchemaVersion ||
      !prompt.command_metadata)
    return false;
  auto const& metadata = *prompt.command_metadata;
  if (!rule.command_recipe_key.empty())
    return command_prompt_allows_persistent_allow(prompt) && command_recipe_matches(rule, prompt);
  // Never recover authority for repository-controlled build/test text from an
  // old or manually forged acknowledgement, regardless of current plan
  // classification.  An exact Critical acknowledgement is authoritative only
  // when the command is verified and contained: Unavailable or
  // UnverifiedDelegatedExecutor containment means the command will run
  // uncontained, so uncontained network-Sensitive commands escalated to
  // Critical must remain one-shot regardless of executes_mutable_project_code.
  return rule.critical_acknowledged && !is_repository_controlled_build_or_test_command(rule.command) &&
         metadata.level == ava::command::CommandLevel::Critical && metadata.executor_identity_verified &&
         metadata.containment_status != CommandContainmentStatus::Unavailable &&
         metadata.containment_status != CommandContainmentStatus::UnverifiedDelegatedExecutor && !rule.command.empty() &&
         rule.command == prompt.command;
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
  auto const normalized = normalized_path(path);
  if (!store.global_rules_file.empty() && normalized == normalized_path(enforceable_permission_rules_file(store, PermissionRuleScope::Global)))
    return true;
  return !store.workspace_dir.empty() && normalized == normalized_path(enforceable_permission_rules_file(store, PermissionRuleScope::Workspace));
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
  auto const normalized = normalized_path(path);
  std::lock_guard lock(protected_rule_paths_mutex());
  auto const& paths = protected_rule_paths();
  return std::ranges::find(paths, normalized) != paths.end();
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
  auto lock = acquire_rule_lock(path);
  if (!lock)
    return std::unexpected(std::move(lock.error()));
  auto rules = load_scope_rules(store, scope);
  if (!rules)
    return std::unexpected(std::move(rules.error()));
  if (contains_legacy_command_allow(*rules))
  {
    return std::unexpected(
        rule_parse_error("refusing to rewrite schema-v1 command Allows; remove them explicitly before adding schema-v2 rules", path, "schema_version"));
  }
  rules->push_back(*rule);
  if (auto written = write_rules_file_atomic(path, rules_file_json(*rules)); !written)
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
    auto lock = acquire_rule_lock(path);
    if (!lock)
      return std::unexpected(std::move(lock.error()));
    auto rules = load_scope_rules(store, scope);
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
    if (auto written = write_rules_file_atomic(path, rules_file_json(*rules)); !written)
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
