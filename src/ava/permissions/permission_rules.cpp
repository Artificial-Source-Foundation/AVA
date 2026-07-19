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
  if (!rule.actor.empty() && (!valid_identifier(rule.actor) || has_control_byte(rule.actor)))
  {
    return std::unexpected(rule_parse_error("permission rule actor is invalid", path, "actor", rule_index));
  }
  if (rule.reason.empty() || rule.reason.size() > kMaxPermissionRuleReasonBytes || has_control_byte(rule.reason))
  {
    return std::unexpected(rule_parse_error("permission rule reason is invalid", path, "reason", rule_index));
  }
  if (!rule.tool_name.empty() && (!valid_identifier(rule.tool_name) || has_control_byte(rule.tool_name)))
  {
    return std::unexpected(rule_parse_error("permission rule tool_name is invalid", path, "tool_name", rule_index));
  }
  if (rule.command.size() > kMaxPermissionRuleCommandBytes || has_control_byte(rule.command))
  {
    return std::unexpected(rule_parse_error("permission rule command is invalid", path, "command", rule_index));
  }
  if (rule.scope == PermissionRuleScope::Workspace && rule.workspace_dir.empty())
  {
    return std::unexpected(rule_parse_error("workspace permission rule is missing workspace_dir", path, "workspace_dir", rule_index));
  }
  if (is_path_operation(rule.operation) && rule.target_path.empty())
  {
    return std::unexpected(rule_parse_error("path permission rule is missing target_path", path, "target_path", rule_index));
  }
  if (is_command_operation(rule.operation) && rule.command.empty())
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
                                                              std::size_t rule_index)
{
  if (!ava::core::json::is_valid_object(object))
  {
    return std::unexpected(rule_parse_error("permission rule object is not valid JSON", path, "rules", rule_index));
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
  if (*version != kCurrentPermissionRulesSchemaVersion)
  {
    auto error = rule_parse_error("unsupported permission rules schema_version", path, "schema_version");
    error.with_context("schema_version", std::to_string(*version));
    error.with_context("supported_schema_version", std::to_string(kCurrentPermissionRulesSchemaVersion));
    return std::unexpected(std::move(error));
  }

  auto objects = rule_objects_array(content, path);
  if (!objects)
    return std::unexpected(std::move(objects.error()));
  std::vector<PersistentPermissionRule> rules;
  rules.reserve(objects->size());
  for (std::size_t index = 0; index < objects->size(); ++index)
  {
    auto rule = parse_rule_object((*objects)[index], path, file_scope, index);
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

std::string rules_file_json(std::vector<PersistentPermissionRule> const& rules)
{
  std::string json = "{\"schema_version\":";
  json += std::to_string(kCurrentPermissionRulesSchemaVersion);
  json += ",\"rules\":[";
  for (std::size_t index = 0; index < rules.size(); ++index)
  {
    if (index > 0)
      json += ',';
    json += permission_rule_json(rules[index]);
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
                                .reason = std::move(draft.reason),
                                .actor = std::move(draft.actor),
                                .created_at = now_timestamp()};
  auto const path = rules_file_path(store, rule.scope);
  if (auto valid = validate_rule(rule, path); !valid)
    return std::unexpected(std::move(valid.error()));
  if (is_repository_build_or_test_allow(rule))
  {
    return std::unexpected(rule_parse_error("persistent allow rules cannot authorize repository build or test commands", path, "command"));
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
  if (!rule.command.empty() && rule.command != prompt.command)
    return false;
  return path_matches(store, rule, prompt);
}

bool is_repository_build_or_test_allow(PersistentPermissionRule const& rule)
{
  return rule.action == PermissionAction::Allow && rule.operation == Operation::RunCommand && is_repository_controlled_build_or_test_command(rule.command);
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
    // Until the separate stable recipe identity/store exists, every planned
    // command is one-shot only. v1 command rules predate sealed command
    // identity, so all legacy RunCommand Allows are ignored; only
    // authoritative Denies are retained.
    if (rule.operation == Operation::RunCommand)
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
  json += ",\"reason\":\"" + ava::core::json::escape(rule.reason) + "\"";
  json += ",\"actor\":\"" + ava::core::json::escape(rule.actor) + "\"";
  json += ",\"created_at\":\"" + ava::core::json::escape(rule.created_at) + "\"";
  json += '}';
  return json;
}

std::string permission_rules_result_json(PermissionRuleStore const& store, std::vector<PersistentPermissionRule> const& rules)
{
  std::string json = "{";
  json += "\"global_rules_file\":\"" + ava::core::json::escape(store.global_rules_file.string()) + "\"";
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
