#include "sys.h"
#include "ava/session/record.h"
#include "ava/session/session_store.h"
#include "ava/session/session_store_internal.h"
#include "ava/session/validation.h"
#include "ava/core/ids.h"
#include "ava/core/json.h"
#include "ava/core/path.h"
#include "ava/core/strict_json.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cerrno>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <functional>
#include <iomanip>
#include <map>
#include <memory>
#include <mutex>
#include <new>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <system_error>
#include <type_traits>
#include <unordered_set>
#include <utility>
#include <vector>
#include <fcntl.h>
#include <linux/fs.h>
#include <sys/file.h>
#include <sys/random.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <unistd.h>
#include "debug.h"

namespace ava::session {

namespace detail {

ScopedFd::ScopedFd(int fd) noexcept : fd_(fd)
{
}

ScopedFd::ScopedFd(ScopedFd&& other) noexcept : fd_(std::exchange(other.fd_, -1))
{
}

ScopedFd& ScopedFd::operator=(ScopedFd&& other) noexcept
{
  if (this != &other)
  {
    reset();
    fd_ = std::exchange(other.fd_, -1);
  }
  return *this;
}

ScopedFd::~ScopedFd()
{
  reset();
}

int ScopedFd::get() const noexcept
{
  return fd_;
}

int ScopedFd::release() noexcept
{
  return std::exchange(fd_, -1);
}

int ScopedFd::close_checked() noexcept
{
  if (fd_ < 0)
    return 0;
  int const fd = std::exchange(fd_, -1);
  return ::close(fd) == 0 ? 0 : errno;
}

void ScopedFd::reset(int fd) noexcept
{
  if (fd_ >= 0)
    static_cast<void>(::close(fd_));
  fd_ = fd;
}

std::string project_key(std::filesystem::path const& workspace_dir)
{
  auto const normalized = std::filesystem::absolute(workspace_dir).lexically_normal().string();
  std::uint64_t hash = 14695981039346656037ULL;
  for (unsigned char const ch : normalized)
  {
    hash ^= ch;
    hash *= 1099511628211ULL;
  }
  std::ostringstream out;
  out << std::hex << hash;
  return out.str();
}

std::mutex& append_mutex_for_path(std::filesystem::path const& path)
{
  static std::mutex registry_mutex;
  static std::map<std::filesystem::path, std::shared_ptr<std::mutex>> append_mutexes;

  auto const key = std::filesystem::absolute(path).lexically_normal();
  std::lock_guard lock(registry_mutex);
  auto& mutex = append_mutexes[key];
  if (!mutex)
  {
    mutex = std::make_shared<std::mutex>();
  }
  return *mutex;
}

ava::core::Error path_io_error(std::string message, std::filesystem::path const& path, int error_number)
{
  auto error = ava::core::Error(ava::core::ErrorCategory::Io, std::move(message));
  error.with_context("path", path.string());
  if (error_number != 0)
    error.with_context("cause", std::strerror(error_number));
  return error;
}

ava::core::Error append_authority_error(std::string message, std::filesystem::path const& path)
{
  auto error = ava::core::Error(ava::core::ErrorCategory::InvalidArgument, std::move(message));
  error.with_context("path", path.string());
  return error;
}

ava::core::VoidResult validate_read_limits(SessionReadLimits const& limits)
{
  if (limits.max_file_bytes == 0 || limits.max_line_bytes == 0 || limits.max_entries == 0 || limits.max_line_bytes > limits.max_file_bytes ||
      limits.max_line_bytes > kMaxSessionLineBytes)
    return std::unexpected(ava::core::Error(ava::core::ErrorCategory::InvalidArgument, "session bounded read limits are invalid"));
  return {};
}

ava::core::Error strict_session_record_error(ava::core::StrictJsonStatus status, std::filesystem::path const& path, bool final_unterminated)
{
  std::string message;
  switch (status)
  {
    case ava::core::StrictJsonStatus::Invalid:
      message = final_unterminated ? "unterminated session suffix is invalid JSON" : "newline-terminated session record is invalid JSON";
      break;
    case ava::core::StrictJsonStatus::NestingTooDeep:
      message = "session record JSON nesting exceeds limit";
      break;
    case ava::core::StrictJsonStatus::DuplicateObjectKey:
      message = "session record JSON contains duplicate object member names";
      break;
    case ava::core::StrictJsonStatus::Valid:
      message = "session record JSON validation failed";
      break;
  }
  auto error = ava::core::Error(ava::core::ErrorCategory::Session, std::move(message));
  error.with_context("path", path.string());
  error.with_context("record_framing", final_unterminated ? "final_unterminated" : "newline_terminated");
  error.with_context("max_nesting_depth", std::to_string(ava::core::json::kMaxNestingDepth));
  return error;
}

ava::core::Result<SessionEntry> parse_strict_session_record(std::string_view line, std::filesystem::path const& path, bool final_unterminated)
{
  auto const strict_status = ava::core::validate_strict_json(line, ava::core::json::kMaxNestingDepth);
  if (strict_status != ava::core::StrictJsonStatus::Valid)
    return std::unexpected(strict_session_record_error(strict_status, path, final_unterminated));
  if (line.ends_with('\r'))
    line.remove_suffix(1);
  auto entry = parse_session_entry_line(line, path);
  if (!entry)
  {
    auto error = std::move(entry.error());
    error.with_context("record_framing", final_unterminated ? "final_unterminated" : "newline_terminated");
    return std::unexpected(std::move(error));
  }
  return entry;
}

bool same_file_identity(struct stat const& left, struct stat const& right)
{
  return S_ISREG(left.st_mode) && S_ISREG(right.st_mode) && left.st_dev == right.st_dev && left.st_ino == right.st_ino;
}

bool same_directory_identity(struct stat const& left, struct stat const& right)
{
  return S_ISDIR(left.st_mode) && S_ISDIR(right.st_mode) && left.st_dev == right.st_dev && left.st_ino == right.st_ino;
}

SessionTraceScope::SessionTraceScope(std::shared_ptr<ava::observability::RunObservation> observation,
                                     std::shared_ptr<ava::observability::TraceContext const> context, ava::observability::TraceEventType attempt,
                                     ava::observability::TraceEventType result, std::optional<EntryType> entry_type, bool ephemeral) noexcept
    : observation_(std::move(observation)), context_(std::move(context)), result_(result), entry_type_(entry_type), ephemeral_(ephemeral)
{
  observation_->emit(attempt, *context_, [this](auto& event) {
    event.phase = ava::observability::TracePhase::Session;
    if (entry_type_)
      event.fields = {{.key = "entry_type", .value = to_string(*entry_type_)}, {.key = "ephemeral", .value = ephemeral_ ? "true" : "false"}};
    else
      event.fields = {{.key = "ephemeral", .value = ephemeral_ ? "true" : "false"}};
  });
}

SessionTraceScope::~SessionTraceScope() noexcept
{
  observation_->emit(result_, *context_, [this](auto& event) {
    event.phase = ava::observability::TracePhase::Session;
    event.outcome = outcome_;
    if (result_ == ava::observability::TraceEventType::SessionLoadResult)
      event.fields = {{.key = "ephemeral", .value = ephemeral_ ? "true" : "false"}, {.key = "entry_count", .value = std::to_string(entry_count_)}};
    else
      event.fields = {{.key = "entry_type", .value = to_string(*entry_type_)}, {.key = "ephemeral", .value = ephemeral_ ? "true" : "false"}};
  });
}

void SessionTraceScope::succeed(std::size_t entry_count) noexcept
{
  outcome_ = ava::observability::TraceOutcome::Success;
  entry_count_ = entry_count;
}

}  // namespace detail

namespace {

using detail::append_authority_error;
using detail::append_mutex_for_path;
using detail::parse_strict_session_record;
using detail::path_io_error;
using detail::project_key;
using detail::same_directory_identity;
using detail::same_file_identity;
using detail::ScopedFd;
using detail::SessionTraceScope;
using detail::strict_session_record_error;
using detail::validate_read_limits;

bool write_all(int fd, std::string_view bytes, int& error_number)
{
  std::size_t written = 0;
  while (written < bytes.size())
  {
    auto const count = ::write(fd, bytes.data() + written, bytes.size() - written);
    if (count < 0)
    {
      if (errno == EINTR)
        continue;
      error_number = errno;
      return false;
    }
    if (count == 0)
    {
      error_number = EIO;
      return false;
    }
    written += static_cast<std::size_t>(count);
  }
  return true;
}

ava::core::VoidResult check_recovery_cancellation(SessionCancelCallback const& cancel_requested)
{
  if (cancel_requested && cancel_requested())
    return std::unexpected(ava::core::Error(ava::core::ErrorCategory::Unknown, "session recovery canceled"));
  return {};
}

using ava::core::normalized_absolute_path;

enum class AppendCommitState
{
  NotStarted,
  PartialOrUnknown,
  CommittedToLeasedInode,
};

std::string_view append_commit_state_text(AppendCommitState state)
{
  switch (state)
  {
    case AppendCommitState::NotStarted:
      return "not_started";
    case AppendCommitState::PartialOrUnknown:
      return "partial_or_unknown";
    case AppendCommitState::CommittedToLeasedInode:
      return "committed_to_leased_inode";
  }
  AI_NEVER_REACHED
}

bool has_append_commit_state(ava::core::Error const& error)
{
  return std::ranges::any_of(error.context(), [](ava::core::ErrorContext const& item) { return item.key == "append_commit_state"; });
}

bool append_committed_to_leased_inode(ava::core::Error const& error)
{
  return std::ranges::any_of(error.context(), [](ava::core::ErrorContext const& item) {
    return item.key == "append_commit_state" && item.value == append_commit_state_text(AppendCommitState::CommittedToLeasedInode);
  });
}

bool append_partial_or_unknown(ava::core::Error const& error)
{
  return std::ranges::any_of(error.context(), [](ava::core::ErrorContext const& item) {
    return item.key == "append_commit_state" && item.value == append_commit_state_text(AppendCommitState::PartialOrUnknown);
  });
}

ava::core::Error append_target_recovery_required_error(std::filesystem::path const& path)
{
  auto error = ava::core::Error(ava::core::ErrorCategory::Session, "session append target requires explicit recovery after a partially persisted batch");
  error.with_context("recovery", "call SessionAppendTarget::recover before any later append or append_batch").with_context("path", path.string());
  return error;
}

ava::core::Error with_append_commit_state(ava::core::Error error, AppendCommitState state, std::filesystem::path const& path)
{
  error.with_context("append_commit_state", std::string(append_commit_state_text(state)));
  if (state == AppendCommitState::PartialOrUnknown)
  {
    error.with_context("recovery",
                       "retain the same active SessionLease, call SessionStore::recover_torn_tail, then call "
                       "SessionStore::recover_incomplete_assistant_output_suffix before any further append");
  }
  error.with_context("path", path.string());
  return error;
}

ava::core::Error batch_partial_failure(ava::core::Error const& failure, std::filesystem::path const& path, std::size_t persisted_entries)
{
  ava::core::Error normalized(failure.category(), failure.message());
  for (auto const& context : failure.context())
  {
    if (context.key != "append_commit_state" && context.key != "recovery")
      normalized.with_context(context.key, context.value);
  }
  normalized = with_append_commit_state(std::move(normalized), AppendCommitState::PartialOrUnknown, path);
  normalized.with_context("batch_persisted_entries", std::to_string(persisted_entries));
  normalized.with_context(
      "staged_prefix_recovery",
      "a batch prefix may be durable; retain the append authority and call SessionAppendTarget::recover to repair torn bytes plus any valid incomplete "
      "assistant-output suffix before accepting another mutation");
  return normalized;
}

ava::core::Result<std::string> rollback_quarantine_name(std::string_view session_name)
{
  std::array<unsigned char, 16> random_bytes{};
  std::size_t offset = 0;
  while (offset < random_bytes.size())
  {
    auto const count = ::getrandom(random_bytes.data() + offset, random_bytes.size() - offset, 0);
    if (count < 0)
    {
      if (errno == EINTR)
        continue;
      return std::unexpected(path_io_error("failed to obtain a CSPRNG rollback quarantine name", std::filesystem::path(session_name), errno));
    }
    if (count == 0)
      return std::unexpected(path_io_error("failed to obtain a complete CSPRNG rollback quarantine name", std::filesystem::path(session_name), EIO));
    offset += static_cast<std::size_t>(count);
  }

  constexpr char hex[] = "0123456789abcdef";
  std::string suffix;
  suffix.reserve(random_bytes.size() * 2);
  for (auto const byte : random_bytes)
  {
    suffix.push_back(hex[byte >> 4]);
    suffix.push_back(hex[byte & 0x0F]);
  }
  return "." + std::string(session_name) + ".rollback." + suffix;
}

int renameat2_no_replace(int parent_fd, std::string const& source_name, std::string const& destination_name)
{
  return static_cast<int>(::syscall(SYS_renameat2, parent_fd, source_name.c_str(), parent_fd, destination_name.c_str(), RENAME_NOREPLACE));
}

bool renameat2_no_replace_unavailable(int error_number)
{
  return error_number == ENOSYS || error_number == EINVAL || error_number == EOPNOTSUPP;
}

bool unchanged_file_snapshot(struct stat const& current, struct stat const& initial)
{
  return same_file_identity(current, initial) && current.st_size == initial.st_size && current.st_mtim.tv_sec == initial.st_mtim.tv_sec &&
         current.st_mtim.tv_nsec == initial.st_mtim.tv_nsec && current.st_ctim.tv_sec == initial.st_ctim.tv_sec &&
         current.st_ctim.tv_nsec == initial.st_ctim.tv_nsec;
}

std::filesystem::path anchored_child_diagnostic_path(int parent_fd, std::string_view name, std::filesystem::path const& fallback_parent)
{
  std::error_code link_error;
  auto parent = std::filesystem::read_symlink("/proc/self/fd/" + std::to_string(parent_fd), link_error);
  if (link_error || parent.empty())
    parent = fallback_parent;
  return parent / std::string(name);
}

ava::core::VoidResult require_unchanged_repair_namespace(int repair_fd, int parent_fd, struct stat const& initial_file_status,
                                                         struct stat const& anchored_parent_status, std::filesystem::path const& parent_path,
                                                         std::string const& session_name, std::filesystem::path const& diagnostic_path)
{
  struct stat descriptor_status{};
  if (fstat(repair_fd, &descriptor_status) != 0)
    return std::unexpected(path_io_error("failed to revalidate session descriptor before torn tail repair", diagnostic_path, errno));
  if (!unchanged_file_snapshot(descriptor_status, initial_file_status))
  {
    auto error = ava::core::Error(ava::core::ErrorCategory::Session, "session changed while preparing torn tail repair");
    error.with_context("path", diagnostic_path.string());
    return std::unexpected(std::move(error));
  }

  struct stat named_status{};
  if (fstatat(parent_fd, session_name.c_str(), &named_status, AT_SYMLINK_NOFOLLOW) != 0)
    return std::unexpected(path_io_error("failed to revalidate the session name before torn tail repair", diagnostic_path, errno));
  if (!unchanged_file_snapshot(named_status, initial_file_status))
  {
    auto error = ava::core::Error(ava::core::ErrorCategory::Session, "session name no longer identifies the recovery target");
    error.with_context("path", diagnostic_path.string());
    return std::unexpected(std::move(error));
  }

  int namespace_fd = ::open(parent_path.c_str(), O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW);
  if (namespace_fd < 0)
    return std::unexpected(path_io_error("failed to re-open the session directory before torn tail repair", parent_path, errno));
  struct stat namespace_status{};
  int namespace_error = 0;
  if (fstat(namespace_fd, &namespace_status) != 0)
    namespace_error = errno;
  int close_error = 0;
  if (::close(namespace_fd) != 0)
    close_error = errno;
  if (namespace_error != 0)
    return std::unexpected(path_io_error("failed to inspect the re-opened session directory before torn tail repair", parent_path, namespace_error));
  if (close_error != 0)
    return std::unexpected(path_io_error("failed to close the re-opened session directory before torn tail repair", parent_path, close_error));
  if (!same_directory_identity(namespace_status, anchored_parent_status))
  {
    auto error = ava::core::Error(ava::core::ErrorCategory::Session, "session directory namespace changed while preparing torn tail repair");
    error.with_context("path", parent_path.string());
    return std::unexpected(std::move(error));
  }
  return {};
}

ava::core::Result<std::filesystem::path> quarantine_session_suffix(int parent_fd, std::filesystem::path const& parent_path, std::string const& session_name,
                                                                   std::string_view recovery_kind, std::string_view suffix,
                                                                   SessionCancelCallback const& cancel_requested,
                                                                   std::function<void(std::filesystem::path const&)> const* before_publication_for_test)
{
  for (int attempt = 0; attempt < 16; ++attempt)
  {
    auto const unique = ava::core::make_id("quarantine");
    auto const final_name = session_name + "." + std::string(recovery_kind) + "." + unique + ".bin";
    auto const temporary_name = "." + session_name + "." + std::string(recovery_kind) + ".tmp." + unique;
    auto const final_path = anchored_child_diagnostic_path(parent_fd, final_name, parent_path);
    auto const temporary_path = anchored_child_diagnostic_path(parent_fd, temporary_name, parent_path);

    ScopedFd quarantine_fd(::openat(parent_fd, temporary_name.c_str(), O_CREAT | O_EXCL | O_RDWR | O_CLOEXEC | O_NOFOLLOW, 0600));
    if (quarantine_fd.get() < 0)
    {
      if (errno == EEXIST)
        continue;
      return std::unexpected(path_io_error("failed to create temporary session recovery quarantine", temporary_path, errno));
    }

    struct stat initial_status{};
    if (fstat(quarantine_fd.get(), &initial_status) != 0)
    {
      int const error_number = errno;
      int const close_error = quarantine_fd.close_checked();
      auto error = path_io_error("failed to inspect newly created session recovery quarantine", temporary_path, error_number);
      error.with_context("quarantine_target", final_path.string());
      if (close_error != 0)
        error.with_context("quarantine_close_cause", std::strerror(close_error));
      return std::unexpected(std::move(error));
    }
    auto same_quarantine_metadata = [](struct stat const& left, struct stat const& right) {
      return same_file_identity(left, right) && left.st_uid == right.st_uid && left.st_gid == right.st_gid && left.st_mode == right.st_mode &&
             left.st_size == right.st_size;
    };
    auto cleanup_temporary_if_owned = [&]() -> int {
      struct stat named_status{};
      if (fstatat(parent_fd, temporary_name.c_str(), &named_status, AT_SYMLINK_NOFOLLOW) != 0)
        return errno == ENOENT ? 0 : errno;
      if (!same_file_identity(initial_status, named_status))
        return ESTALE;
      return ::unlinkat(parent_fd, temporary_name.c_str(), 0) == 0 ? 0 : errno;
    };
    auto abandon_prepublication = [&](ava::core::Error error) -> ava::core::Result<std::filesystem::path> {
      int const cleanup_error = cleanup_temporary_if_owned();
      int const close_error = quarantine_fd.close_checked();
      error.with_context("quarantine_target", final_path.string());
      if (cleanup_error != 0)
        error.with_context("temporary_cleanup", cleanup_error == ESTALE ? "temporary name changed; replacement preserved" : std::strerror(cleanup_error));
      if (close_error != 0)
        error.with_context("quarantine_close_cause", std::strerror(close_error));
      return std::unexpected(std::move(error));
    };
    auto prepublication_io_failure = [&](std::string message, int error_number) {
      return abandon_prepublication(path_io_error(std::move(message), temporary_path, error_number));
    };
    auto published_failure = [&](std::string message, int error_number = 0) -> ava::core::Result<std::filesystem::path> {
      auto error = error_number == 0 ? ava::core::Error(ava::core::ErrorCategory::PermissionDenied, std::move(message))
                                     : path_io_error(std::move(message), final_path, error_number);
      error.with_context("quarantine_path", final_path.string()).with_context("temporary_path", temporary_path.string());
      int const close_error = quarantine_fd.close_checked();
      if (close_error != 0)
        error.with_context("quarantine_close_cause", std::strerror(close_error));
      return std::unexpected(std::move(error));
    };
    auto descriptor_content_matches = [&]() -> bool {
      if (initial_status.st_size < 0 || static_cast<std::uintmax_t>(initial_status.st_size) != suffix.size())
        return false;
      std::array<char, 8192> buffer{};
      std::size_t offset = 0;
      while (offset < suffix.size())
      {
        auto const wanted = std::min(buffer.size(), suffix.size() - offset);
        ssize_t count = 0;
        do
        {
          count = ::pread(quarantine_fd.get(), buffer.data(), wanted, static_cast<off_t>(offset));
        } while (count < 0 && errno == EINTR);
        if (count <= 0 || std::string_view(buffer.data(), static_cast<std::size_t>(count)) != suffix.substr(offset, static_cast<std::size_t>(count)))
          return false;
        offset += static_cast<std::size_t>(count);
      }
      return true;
    };

    if (fchmod(quarantine_fd.get(), 0600) != 0)
      return prepublication_io_failure("failed to set session recovery quarantine permissions", errno);
    if (fstat(quarantine_fd.get(), &initial_status) != 0)
      return prepublication_io_failure("failed to inspect temporary session recovery quarantine", errno);
    if (!S_ISREG(initial_status.st_mode) || (initial_status.st_mode & 07777) != 0600 || initial_status.st_uid != geteuid() || initial_status.st_nlink != 1)
    {
      auto error = ava::core::Error(ava::core::ErrorCategory::PermissionDenied, "temporary session recovery quarantine has unsafe metadata");
      error.with_context("mode", std::to_string(initial_status.st_mode & 07777))
          .with_context("owner", std::to_string(initial_status.st_uid))
          .with_context("link_count", std::to_string(initial_status.st_nlink));
      return abandon_prepublication(std::move(error));
    }
    int write_error = 0;
    if (!write_all(quarantine_fd.get(), suffix, write_error))
      return prepublication_io_failure("failed to write session recovery quarantine", write_error);
    if (fdatasync(quarantine_fd.get()) != 0)
      return prepublication_io_failure("failed to sync session recovery quarantine", errno);
    if (fstat(quarantine_fd.get(), &initial_status) != 0)
      return prepublication_io_failure("failed to re-inspect temporary session recovery quarantine", errno);

    if (before_publication_for_test && *before_publication_for_test)
    {
      try
      {
        (*before_publication_for_test)(temporary_path);
      }
      catch (...)
      {
        return abandon_prepublication(ava::core::Error(ava::core::ErrorCategory::Unknown, "session recovery quarantine prepublication test hook failed"));
      }
    }
    if (auto canceled = check_recovery_cancellation(cancel_requested); !canceled)
      return abandon_prepublication(std::move(canceled.error()));

    struct stat prepublication_status{};
    if (fstat(quarantine_fd.get(), &prepublication_status) != 0)
      return prepublication_io_failure("failed to verify session recovery quarantine before publication", errno);
    if (!same_quarantine_metadata(initial_status, prepublication_status) || prepublication_status.st_nlink != 1 || !descriptor_content_matches())
    {
      auto error = ava::core::Error(ava::core::ErrorCategory::PermissionDenied,
                                    "session recovery quarantine inode or content changed before descriptor-anchored publication");
      error.with_context("link_count", std::to_string(prepublication_status.st_nlink));
      return abandon_prepublication(std::move(error));
    }

    if (::linkat(quarantine_fd.get(), "", parent_fd, final_name.c_str(), AT_EMPTY_PATH) != 0)
    {
      int const error_number = errno;
      auto failed = prepublication_io_failure("failed to publish exact session recovery quarantine inode", error_number);
      if (error_number == EEXIST)
        continue;
      return failed;
    }

    struct stat linked_status{};
    struct stat final_status{};
    if (fstat(quarantine_fd.get(), &linked_status) != 0)
      return published_failure("failed to inspect published session recovery quarantine", errno);
    if (fstatat(parent_fd, final_name.c_str(), &final_status, AT_SYMLINK_NOFOLLOW) != 0)
      return published_failure("failed to inspect final session recovery quarantine name", errno);
    if (!same_quarantine_metadata(initial_status, linked_status) || !same_quarantine_metadata(initial_status, final_status) || linked_status.st_nlink != 2 ||
        final_status.st_nlink != 2 || !descriptor_content_matches())
    {
      return published_failure("published session recovery quarantine does not identify the exact validated inode");
    }

    struct stat temporary_status{};
    if (fstatat(parent_fd, temporary_name.c_str(), &temporary_status, AT_SYMLINK_NOFOLLOW) != 0)
      return published_failure("published session recovery quarantine temporary name disappeared before verified cleanup", errno);
    if (!same_quarantine_metadata(initial_status, temporary_status) || temporary_status.st_nlink != 2)
      return published_failure("session recovery quarantine temporary name was replaced; replacement preserved");
    if (::unlinkat(parent_fd, temporary_name.c_str(), 0) != 0)
      return published_failure("session recovery quarantine was published but its verified temporary name could not be removed", errno);

    struct stat committed_status{};
    struct stat committed_final_status{};
    if (fstat(quarantine_fd.get(), &committed_status) != 0 || fstatat(parent_fd, final_name.c_str(), &committed_final_status, AT_SYMLINK_NOFOLLOW) != 0)
    {
      return published_failure("failed to verify committed session recovery quarantine", errno);
    }
    if (!same_quarantine_metadata(initial_status, committed_status) || !same_quarantine_metadata(initial_status, committed_final_status) ||
        committed_status.st_nlink != 1 || committed_final_status.st_nlink != 1 || !descriptor_content_matches())
    {
      return published_failure("committed session recovery quarantine failed inode, metadata, link-count, or content verification");
    }

    int const close_error = quarantine_fd.close_checked();
    int const sync_error = fsync(parent_fd) == 0 ? 0 : errno;
    if (close_error != 0)
    {
      auto error = path_io_error("session recovery quarantine was published but its descriptor close failed", final_path, close_error);
      error.with_context("quarantine_path", final_path.string());
      if (sync_error != 0)
        error.with_context("directory_sync_cause", std::strerror(sync_error));
      return std::unexpected(std::move(error));
    }
    if (sync_error != 0)
    {
      auto error = path_io_error("session recovery quarantine was published but the session directory sync failed", final_path, sync_error);
      error.with_context("quarantine_path", final_path.string());
      return std::unexpected(std::move(error));
    }
    return final_path;
  }

  auto error = ava::core::Error(ava::core::ErrorCategory::Io, "failed to allocate a unique session recovery quarantine path");
  error.with_context("path", (parent_path / session_name).string());
  return std::unexpected(std::move(error));
}

}  // namespace

SessionStore::SessionStore(SessionStoreOptions options) : options_(std::move(options)), observation_attachment_(std::make_shared<ObservationAttachment>())
{
}

SessionStore::SessionStore(SessionStoreOptions options, std::shared_ptr<EphemeralState> ephemeral_state)
    : options_(std::move(options)), ephemeral_state_(std::move(ephemeral_state)), observation_attachment_(std::make_shared<ObservationAttachment>())
{
}

std::string const& SessionStore::session_id() const noexcept
{
  return options_.session_id;
}

std::filesystem::path SessionStore::session_path() const
{
  return options_.root_dir / project_key(options_.workspace_dir) / (options_.session_id + ".jsonl");
}

bool SessionStore::is_ephemeral() const noexcept
{
  return static_cast<bool>(ephemeral_state_);
}

std::uint64_t SessionStore::set_run_observation(std::shared_ptr<ava::observability::RunObservation> const& observation,
                                                ava::observability::TraceContext const& context) noexcept
{
  // Disabled observation has no attachment state and does not take its lock.
  if (!observation || !observation->enabled())
    return 0;
  try
  {
    if (observation_attachment_->fail_next_attachment_for_test.exchange(false, std::memory_order_relaxed))
      throw std::bad_alloc();
    // Finish every allocation before acquiring the lock so a failure leaves
    // the live attachment untouched. Immutable shared context also makes the
    // append/load snapshot and SessionTraceScope construction no-throw.
    auto prepared_context = std::make_shared<ava::observability::TraceContext>(context);
    if (prepared_context->session_id.empty())
      prepared_context->session_id = session_id();
    auto prepared_observation = observation;

    std::lock_guard lock(observation_attachment_->mutex);
    observation_attachment_->observation = std::move(prepared_observation);
    observation_attachment_->context = std::move(prepared_context);
    observation_attachment_->enabled.store(true, std::memory_order_release);
    auto generation = observation_attachment_->generation + 1;
    if (generation == 0)
      generation = 1;
    observation_attachment_->generation = generation;
    return generation;
  }
  catch (...)
  {
    observation->account_external_failure();
    return 0;
  }
}

void SessionStore::fail_next_run_observation_attachment_for_test() noexcept
{
  observation_attachment_->fail_next_attachment_for_test.store(true, std::memory_order_relaxed);
}

void SessionStore::clear_run_observation(std::uint64_t generation) noexcept
{
  if (generation == 0)
    return;
  try
  {
    std::lock_guard lock(observation_attachment_->mutex);
    if (observation_attachment_->generation == generation)
    {
      observation_attachment_->enabled.store(false, std::memory_order_release);
      observation_attachment_->observation.reset();
      observation_attachment_->context = {};
    }
  }
  catch (...)
  {
    // A cleanup failure is observer-only and must never terminate a run.
  }
}

SessionStore SessionStore::detached_copy_for_background_persistence() const
{
  return SessionStore(options_, ephemeral_state_);
}

void SessionStore::set_before_append_identity_check_for_test(std::function<void()> hook)
{
  before_append_identity_check_for_test_ = std::move(hook);
}

void SessionStore::set_after_append_write_for_test(std::function<void()> hook)
{
  after_append_write_for_test_ = std::move(hook);
}

void SessionStore::set_after_lease_bound_read_for_test(std::function<void()> hook)
{
  after_lease_bound_read_for_test_ = std::move(hook);
}

void SessionStore::fail_persistent_append_target_allocation_for_test(bool fail) noexcept
{
  fail_persistent_append_target_allocation_for_test_ = fail;
}

void SessionStore::fail_persistent_read_authority_allocation_for_test(bool fail) noexcept
{
  fail_persistent_read_authority_allocation_for_test_ = fail;
}

void SessionStore::set_append_write_for_test(std::function<ssize_t(int, std::string_view)> hook)
{
  append_write_for_test_ = std::move(hook);
}

void SessionStore::set_before_recovery_quarantine_publication_for_test(std::function<void(std::filesystem::path const&)> hook)
{
  before_recovery_quarantine_publication_for_test_ = std::move(hook);
}

void SessionStore::set_after_recovery_quarantine_publication_for_test(std::function<void()> hook)
{
  after_recovery_quarantine_publication_for_test_ = std::move(hook);
}

void SessionStore::set_before_created_file_rollback_detach_for_test(std::function<void()> hook)
{
  before_created_file_rollback_detach_for_test_ = std::move(hook);
}

void SessionStore::set_after_created_file_rollback_detach_for_test(std::function<void()> hook)
{
  after_created_file_rollback_detach_for_test_ = std::move(hook);
}

ava::core::VoidResult SessionStore::append(SessionLease const& lease, SessionEntry const& entry)
{
  if (entry.type == EntryType::AssistantOutputItem || entry.type == EntryType::AssistantTurnCommit)
  {
    auto error = ava::core::Error(ava::core::ErrorCategory::InvalidArgument,
                                  "raw SessionStore append cannot mutate v4 assistant-output records; use SessionAppendTarget or append_validated_copy");
    error.with_context("entry_type", to_string(entry.type));
    return std::unexpected(with_append_commit_state(std::move(error), AppendCommitState::NotStarted, session_path()));
  }
  if (is_ephemeral())
  {
    auto error = ava::core::Error(ava::core::ErrorCategory::InvalidArgument, "persistent session append lease cannot be used with an ephemeral store");
    return std::unexpected(with_append_commit_state(std::move(error), AppendCommitState::NotStarted, session_path()));
  }

  auto const path = session_path();
  auto const canonical_path = normalized_absolute_path(path);
  // Keep the legacy ordinary-record escape hatch from splitting a staged v4
  // assistant transaction. Its authoritative snapshot, state transition, and
  // durable write share the same per-path serialization as append targets.
  if (!lease.active() || canonical_path != lease.canonical_path())
  {
    auto appended = append_impl(&lease, entry);
    if (!appended && !has_append_commit_state(appended.error()))
      return std::unexpected(with_append_commit_state(std::move(appended.error()), AppendCommitState::NotStarted, path));
    return appended;
  }

  std::lock_guard serialization_lock(append_mutex_for_path(canonical_path));
  std::vector<SessionEntry> history;
  auto loaded = visit_entries_leased(
      lease, legacy_unbounded_session_read_limits(),
      [&](SessionEntry const& historical_entry) -> ava::core::Result<bool> {
        history.push_back(historical_entry);
        return true;
      },
      nullptr, false);
  if (!loaded)
    return std::unexpected(with_append_commit_state(std::move(loaded.error()), AppendCommitState::NotStarted, path));
  auto current_state = AssistantOutputAppendState::from_validated_history(history);
  if (!current_state)
    return std::unexpected(with_append_commit_state(std::move(current_state.error()), AppendCommitState::NotStarted, path));
  try
  {
    if (auto preflight = current_state->apply_candidate(entry); !preflight)
      return std::unexpected(with_append_commit_state(std::move(preflight.error()), AppendCommitState::NotStarted, path));
  }
  catch (...)
  {
    return std::unexpected(with_append_commit_state(
        ava::core::Error(ava::core::ErrorCategory::Unknown, "failed to preflight raw assistant-output append state"), AppendCommitState::NotStarted, path));
  }

  auto appended = append_impl(&lease, entry, true);
  if (!appended && !has_append_commit_state(appended.error()))
    return std::unexpected(with_append_commit_state(std::move(appended.error()), AppendCommitState::NotStarted, path));
  return appended;
}

ava::core::VoidResult SessionStore::append_ephemeral(SessionEntry const& entry)
{
  if (entry.type == EntryType::AssistantOutputItem || entry.type == EntryType::AssistantTurnCommit)
  {
    auto error = ava::core::Error(ava::core::ErrorCategory::InvalidArgument,
                                  "raw SessionStore append cannot mutate v4 assistant-output records; use SessionAppendTarget");
    error.with_context("entry_type", to_string(entry.type));
    return std::unexpected(std::move(error));
  }
  if (!is_ephemeral())
  {
    return std::unexpected(ava::core::Error(ava::core::ErrorCategory::InvalidArgument, "persistent session append requires an active exact SessionLease"));
  }

  std::lock_guard serialization_lock(ephemeral_state_->mutation_mutex);
  auto history = load();
  if (!history)
    return std::unexpected(std::move(history.error()));
  auto current_state = AssistantOutputAppendState::from_validated_history(*history);
  if (!current_state)
    return std::unexpected(std::move(current_state.error()));
  try
  {
    if (auto preflight = current_state->apply_candidate(entry); !preflight)
      return std::unexpected(std::move(preflight.error()));
  }
  catch (...)
  {
    return std::unexpected(ava::core::Error(ava::core::ErrorCategory::Unknown, "failed to preflight raw assistant-output append state"));
  }
  return append_impl(nullptr, entry, true);
}

ava::core::VoidResult SessionStore::append_validated_copy(SessionLease const& lease, std::vector<SessionEntry> const& entries)
{
  if (is_ephemeral())
    return std::unexpected(ava::core::Error(ava::core::ErrorCategory::InvalidArgument, "validated session copy requires a persistent destination"));
  if (!lease.active() || !lease.created_)
    return std::unexpected(
        ava::core::Error(ava::core::ErrorCategory::InvalidArgument, "validated session copy requires the active creating SessionLease for a new destination"));

  auto const path = session_path();
  auto const canonical_path = normalized_absolute_path(path);
  if (canonical_path != lease.canonical_path())
  {
    auto error = append_authority_error("validated session copy lease does not exactly match its destination", canonical_path);
    error.with_context("lease_path", lease.canonical_path().string());
    return std::unexpected(std::move(error));
  }

  for (auto const& entry : entries)
  {
    auto serialized = serialize_session_entry_line(entry);
    if (!serialized)
      return std::unexpected(std::move(serialized.error()));
    auto strict = parse_strict_session_record(*serialized, path, false);
    if (!strict)
      return std::unexpected(std::move(strict.error()));
  }
  auto const replay = validate_session_replay(entries);
  if (!replay.ok())
  {
    auto error = ava::core::Error(ava::core::ErrorCategory::Session, "validated session copy history fails replay validation");
    error.with_context("errors", std::to_string(replay.error_count));
    if (!replay.issues.empty())
      error.with_context("first_issue", replay.issues.front().message).with_context("entry_id", replay.issues.front().entry_id);
    return std::unexpected(std::move(error));
  }
  auto const assistant_output = classify_assistant_output(entries);
  if (!assistant_output.diagnostics.empty())
  {
    auto const& diagnostic = assistant_output.diagnostics.front();
    auto error =
        ava::core::Error(ava::core::ErrorCategory::Session, "validated session copy history contains an assistant-output diagnostic and cannot be copied");
    error.with_context("diagnostic_kind", std::string(to_string(diagnostic.kind)))
        .with_context("diagnostic_entry_id", diagnostic.entry_id)
        .with_context("diagnostic", diagnostic.message);
    return std::unexpected(std::move(error));
  }

  auto& append_mutex = append_mutex_for_path(canonical_path);
  std::lock_guard append_lock(append_mutex);
  auto destination_entries = load(lease);
  if (!destination_entries)
    return std::unexpected(std::move(destination_entries.error()));
  if (!destination_entries->empty())
  {
    auto error = ava::core::Error(ava::core::ErrorCategory::InvalidArgument, "validated session copy destination must be newly created and empty");
    error.with_context("path", path.string()).with_context("existing_entries", std::to_string(destination_entries->size()));
    return std::unexpected(std::move(error));
  }

  for (auto const& entry : entries)
  {
    auto appended = append_impl(&lease, entry, true);
    if (!appended)
      return appended;
  }
  return {};
}

ava::core::VoidResult SessionStore::append_impl(SessionLease const* lease, SessionEntry const& entry, bool append_serialization_held)
{
  std::optional<SessionTraceScope> trace;
  if (observation_attachment_->enabled.load(std::memory_order_acquire))
  {
    std::shared_ptr<ava::observability::RunObservation> observation;
    try
    {
      std::shared_ptr<ava::observability::TraceContext const> context;
      {
        std::lock_guard lock(observation_attachment_->mutex);
        observation = observation_attachment_->observation;
        context = observation_attachment_->context;
      }
      if (observation && observation->enabled() && context)
      {
        trace.emplace(std::move(observation), std::move(context), ava::observability::TraceEventType::SessionAppendAttempt,
                      ava::observability::TraceEventType::SessionAppendResult, entry.type, is_ephemeral());
      }
    }
    catch (...)
    {
      if (observation)
        observation->account_external_failure();
    }
  }
  if (auto valid_session_id = validate_session_id(options_.session_id); !valid_session_id)
    return valid_session_id;
  if (entry.data_json.empty() || entry.data_json.front() != '{' || entry.data_json.back() != '}')
  {
    auto error = ava::core::Error(ava::core::ErrorCategory::InvalidArgument, "session entry data must be a JSON object");
    error.with_context("entry_id", entry.id);
    return std::unexpected(std::move(error));
  }
  if (entry.data_json.find('\n') != std::string::npos || entry.data_json.find('\r') != std::string::npos)
  {
    auto error = ava::core::Error(ava::core::ErrorCategory::InvalidArgument, "session entry data must not contain raw newlines");
    error.with_context("entry_id", entry.id);
    return std::unexpected(std::move(error));
  }
  if (auto valid_parent_id = validate_parent_id(entry.parent_id, entry.id); !valid_parent_id)
    return valid_parent_id;

  auto line = serialize_session_entry_line(entry);
  if (!line)
    return std::unexpected(std::move(line.error()));
  auto strict_entry = parse_strict_session_record(*line, session_path(), false);
  if (!strict_entry)
  {
    auto error = std::move(strict_entry.error());
    error.with_context("operation", "append").with_context("entry_id", entry.id);
    return std::unexpected(std::move(error));
  }

  if (ephemeral_state_)
  {
    if (lease != nullptr)
      return std::unexpected(ava::core::Error(ava::core::ErrorCategory::InvalidArgument, "ephemeral session append must not receive a lease"));
    std::lock_guard lock(ephemeral_state_->entries_mutex);
    ephemeral_state_->entries.push_back(entry);
    if (trace)
      trace->succeed();
    return {};
  }

  auto const path = session_path();
  if (lease == nullptr || !lease->active())
    return std::unexpected(with_append_commit_state(append_authority_error("persistent session append requires an active session lease", path),
                                                    AppendCommitState::NotStarted, path));
  auto const canonical_path = normalized_absolute_path(path);
  if (canonical_path != lease->canonical_path())
  {
    auto error = append_authority_error("session lease does not exactly match the persistent append target", canonical_path);
    error.with_context("lease_path", lease->canonical_path().string());
    return std::unexpected(with_append_commit_state(std::move(error), AppendCommitState::NotStarted, path));
  }
  auto const parent_path = canonical_path.parent_path();
  auto const session_name = canonical_path.filename().string();
  if (session_name.empty())
    return std::unexpected(
        with_append_commit_state(path_io_error("session append target has no basename", canonical_path), AppendCommitState::NotStarted, path));

  auto& append_mutex = append_mutex_for_path(canonical_path);
  std::optional<std::unique_lock<std::mutex>> append_lock;
  if (!append_serialization_held)
    append_lock.emplace(append_mutex);

  int parent_fd = ::open(parent_path.c_str(), O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
  if (parent_fd < 0)
    return std::unexpected(
        with_append_commit_state(path_io_error("failed to anchor session append directory", parent_path, errno), AppendCommitState::NotStarted, path));
  int append_fd = -1;
  auto close_fd = [](int& fd) {
    if (fd < 0)
      return 0;
    int const result = ::close(fd);
    int const error = result == 0 ? 0 : errno;
    fd = -1;
    return error;
  };
  auto close_both = [&] {
    int const append_close_error = close_fd(append_fd);
    int const parent_close_error = close_fd(parent_fd);
    return std::pair(append_close_error, parent_close_error);
  };
  auto fail_before_write = [&](ava::core::Error error) -> ava::core::VoidResult {
    auto const [append_close_error, parent_close_error] = close_both();
    if (append_close_error != 0)
      error.with_context("append_close_cause", std::strerror(append_close_error));
    if (parent_close_error != 0)
      error.with_context("parent_close_cause", std::strerror(parent_close_error));
    return std::unexpected(with_append_commit_state(std::move(error), AppendCommitState::NotStarted, path));
  };
  auto fail_after_write = [&](ava::core::Error error) -> ava::core::VoidResult {
    auto const [append_close_error, parent_close_error] = close_both();
    if (append_close_error != 0)
      error.with_context("append_close_cause", std::strerror(append_close_error));
    if (parent_close_error != 0)
      error.with_context("parent_close_cause", std::strerror(parent_close_error));
    return std::unexpected(with_append_commit_state(std::move(error), AppendCommitState::CommittedToLeasedInode, path));
  };

  struct stat anchored_parent_status{};
  if (fstat(parent_fd, &anchored_parent_status) != 0 || !S_ISDIR(anchored_parent_status.st_mode))
  {
    int const error_number = errno;
    return fail_before_write(path_io_error("session append parent is not a directory", parent_path, error_number));
  }
  append_fd = ::openat(parent_fd, session_name.c_str(), O_RDWR | O_APPEND | O_NOFOLLOW | O_NONBLOCK | O_CLOEXEC);
  if (append_fd < 0)
  {
    auto const category = errno == ELOOP ? ava::core::ErrorCategory::PermissionDenied : ava::core::ErrorCategory::Io;
    auto error = ava::core::Error(category, errno == ELOOP ? "session append target must not be a symlink" : "failed to open session append target");
    error.with_context("path", path.string()).with_context("cause", std::strerror(errno));
    return fail_before_write(std::move(error));
  }

  struct AppendIdentity
  {
    struct stat lease_status{};
    struct stat append_status{};
    struct stat named_status{};
  };
  auto validate_identity = [&](std::string_view timing) -> ava::core::Result<AppendIdentity> {
    AppendIdentity identity;
    if (fstat(lease->fd_, &identity.lease_status) != 0)
      return std::unexpected(path_io_error("failed to inspect session lease " + std::string(timing), path, errno));
    if (fstat(append_fd, &identity.append_status) != 0)
      return std::unexpected(path_io_error("failed to inspect session append descriptor " + std::string(timing), path, errno));
    if (fstatat(parent_fd, session_name.c_str(), &identity.named_status, AT_SYMLINK_NOFOLLOW) != 0)
      return std::unexpected(path_io_error("failed to inspect session append name " + std::string(timing), path, errno));
    if (!same_file_identity(identity.lease_status, identity.append_status) || !same_file_identity(identity.lease_status, identity.named_status) ||
        identity.lease_status.st_nlink != 1 || identity.append_status.st_nlink != 1 || identity.named_status.st_nlink != 1)
    {
      auto error = ava::core::Error(ava::core::ErrorCategory::Session, "session append target was replaced " + std::string(timing));
      error.with_context("timing", std::string(timing));
      error.with_context("lease_links", std::to_string(identity.lease_status.st_nlink));
      error.with_context("append_links", std::to_string(identity.append_status.st_nlink));
      error.with_context("name_links", std::to_string(identity.named_status.st_nlink));
      return std::unexpected(std::move(error));
    }
    struct stat current_parent_status{};
    if (fstat(parent_fd, &current_parent_status) != 0)
      return std::unexpected(path_io_error("failed to inspect anchored session directory " + std::string(timing), parent_path, errno));
    if (!same_directory_identity(anchored_parent_status, current_parent_status))
    {
      auto error = ava::core::Error(ava::core::ErrorCategory::Session, "anchored session directory changed during append");
      error.with_context("timing", std::string(timing)).with_context("path", parent_path.string());
      return std::unexpected(std::move(error));
    }
    int publication_fd = ::open(parent_path.c_str(), O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
    if (publication_fd < 0)
      return std::unexpected(path_io_error("failed to re-open session directory publication " + std::string(timing), parent_path, errno));
    struct stat publication_status{};
    int publication_status_error = fstat(publication_fd, &publication_status) == 0 ? 0 : errno;
    int publication_close_error = ::close(publication_fd) == 0 ? 0 : errno;
    if (publication_status_error != 0)
      return std::unexpected(
          path_io_error("failed to inspect re-opened session directory publication " + std::string(timing), parent_path, publication_status_error));
    if (publication_close_error != 0)
      return std::unexpected(
          path_io_error("failed to close re-opened session directory publication " + std::string(timing), parent_path, publication_close_error));
    if (!same_directory_identity(anchored_parent_status, publication_status))
    {
      auto error = ava::core::Error(ava::core::ErrorCategory::Session, "session directory publication changed during append");
      error.with_context("timing", std::string(timing)).with_context("path", parent_path.string());
      return std::unexpected(std::move(error));
    }
    return identity;
  };

  auto initial_identity = validate_identity("before session framing inspection");
  if (!initial_identity)
    return fail_before_write(std::move(initial_identity.error()));
  if (initial_identity->append_status.st_size > 0)
  {
    char last_byte = '\0';
    ssize_t count = 0;
    do
    {
      count = ::pread(append_fd, &last_byte, 1, initial_identity->append_status.st_size - 1);
    } while (count < 0 && errno == EINTR);
    if (count != 1)
      return fail_before_write(path_io_error("failed to read session framing before append", path, count < 0 ? errno : EIO));
    if (last_byte != '\n')
    {
      auto error = ava::core::Error(ava::core::ErrorCategory::Session, "refusing to append to a session with an unterminated tail");
      error.with_context("recovery", "retain the active SessionLease and call SessionStore::recover_torn_tail before appending");
      return fail_before_write(std::move(error));
    }
  }

  auto before_mode = validate_identity("immediately before session mode update");
  if (!before_mode)
    return fail_before_write(std::move(before_mode.error()));
  if (fchmod(append_fd, 0600) != 0)
    return fail_before_write(path_io_error("failed to preserve session file permissions", path, errno));
  if (before_append_identity_check_for_test_)
  {
    try
    {
      before_append_identity_check_for_test_();
    }
    catch (...)
    {
      return fail_before_write(ava::core::Error(ava::core::ErrorCategory::Unknown, "session append pre-write test hook failed"));
    }
  }
  auto before_write = validate_identity("immediately before entry write");
  if (!before_write)
    return fail_before_write(std::move(before_write.error()));

  auto payload = std::move(*line);
  payload.push_back('\n');
  std::size_t written = 0;
  int write_error = 0;
  while (written < payload.size())
  {
    ssize_t count = 0;
    if (append_write_for_test_)
      count = append_write_for_test_(append_fd, std::string_view(payload).substr(written));
    else
      count = ::write(append_fd, payload.data() + written, payload.size() - written);
    if (count < 0)
    {
      if (errno == EINTR)
        continue;
      write_error = errno;
      break;
    }
    if (count == 0 || static_cast<std::size_t>(count) > payload.size() - written)
    {
      write_error = EIO;
      break;
    }
    written += static_cast<std::size_t>(count);
  }
  if (written != payload.size())
  {
    auto error = path_io_error("failed to write complete session entry through the append descriptor", path, write_error == 0 ? EIO : write_error);
    error.with_context("bytes_written", std::to_string(written)).with_context("payload_bytes", std::to_string(payload.size()));
    auto const [append_close_error, parent_close_error] = close_both();
    if (append_close_error != 0)
      error.with_context("append_close_cause", std::strerror(append_close_error));
    if (parent_close_error != 0)
      error.with_context("parent_close_cause", std::strerror(parent_close_error));
    return std::unexpected(
        with_append_commit_state(std::move(error), written == 0 ? AppendCommitState::NotStarted : AppendCommitState::PartialOrUnknown, path));
  }

  if (after_append_write_for_test_)
  {
    try
    {
      after_append_write_for_test_();
    }
    catch (...)
    {
      return fail_after_write(ava::core::Error(ava::core::ErrorCategory::Unknown, "session append post-write test hook failed"));
    }
  }
  auto after_write = validate_identity("after the entry write");
  if (!after_write)
    return fail_after_write(std::move(after_write.error()));
  int const append_close_error = close_fd(append_fd);
  int const parent_close_error = close_fd(parent_fd);
  if (append_close_error != 0)
    return std::unexpected(with_append_commit_state(path_io_error("session entry was written but the append descriptor close failed", path, append_close_error),
                                                    AppendCommitState::CommittedToLeasedInode, path));
  if (parent_close_error != 0)
    return std::unexpected(
        with_append_commit_state(path_io_error("session entry was written but the session directory close failed", parent_path, parent_close_error),
                                 AppendCommitState::CommittedToLeasedInode, path));

  if (trace)
    trace->succeed();
  return {};
}

ava::core::VoidResult SessionStore::remove_created_file(SessionLease const& lease) const
{
  if (ephemeral_state_)
    return std::unexpected(ava::core::Error(ava::core::ErrorCategory::InvalidArgument, "ephemeral sessions do not have a persistent file to remove"));
  if (auto valid_session_id = validate_session_id(options_.session_id); !valid_session_id)
    return valid_session_id;
  if (lease.fd_ < 0 || lease.canonical_path_.empty())
    return std::unexpected(ava::core::Error(ava::core::ErrorCategory::InvalidArgument, "created session cleanup requires an active session lease"));
  if (!lease.created_)
    return std::unexpected(ava::core::Error(ava::core::ErrorCategory::PermissionDenied, "session cleanup lease did not create this session file"));

  auto const path = session_path();
  std::error_code absolute_error;
  auto absolute_path = std::filesystem::absolute(path, absolute_error).lexically_normal();
  if (absolute_error)
    absolute_path = path.lexically_normal();
  if (absolute_path != lease.canonical_path_)
  {
    auto error = ava::core::Error(ava::core::ErrorCategory::InvalidArgument, "session lease does not match the created cleanup target");
    error.with_context("path", absolute_path.string()).with_context("lease_path", lease.canonical_path_.string());
    return std::unexpected(std::move(error));
  }

  auto const parent_path = path.parent_path();
  auto const session_name = path.filename().string();
  if (session_name.empty())
    return std::unexpected(path_io_error("created session cleanup target has no basename", path));

  auto& append_mutex = append_mutex_for_path(path);
  std::lock_guard append_lock(append_mutex);
  struct stat lease_status{};
  if (fstat(lease.fd_, &lease_status) != 0)
    return std::unexpected(path_io_error("failed to inspect the created session lease before cleanup", path, errno));
  if (!S_ISREG(lease_status.st_mode) || lease_status.st_nlink != 1)
  {
    auto error = ava::core::Error(ava::core::ErrorCategory::PermissionDenied, "created session cleanup requires one regular linked lease target");
    error.with_context("path", path.string()).with_context("link_count", std::to_string(lease_status.st_nlink));
    return std::unexpected(std::move(error));
  }

  int parent_fd = ::open(parent_path.c_str(), O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW);
  if (parent_fd < 0)
    return std::unexpected(path_io_error("failed to open the session directory for created-session rollback", parent_path, errno));
  auto finish_error = [&](ava::core::Error error, bool sync_parent) -> ava::core::VoidResult {
    if (parent_fd >= 0)
    {
      if (sync_parent && fsync(parent_fd) != 0)
        error.with_context("rollback_directory_sync_cause", std::strerror(errno));
      if (::close(parent_fd) != 0)
        error.with_context("rollback_directory_close_cause", std::strerror(errno));
      parent_fd = -1;
    }
    return std::unexpected(std::move(error));
  };

  struct stat named_status{};
  if (fstatat(parent_fd, session_name.c_str(), &named_status, AT_SYMLINK_NOFOLLOW) != 0)
    return finish_error(path_io_error("failed to inspect the created session name before cleanup", path, errno), false);
  if (!same_file_identity(lease_status, named_status) || named_status.st_nlink != 1)
  {
    auto error = ava::core::Error(ava::core::ErrorCategory::PermissionDenied, "created session name no longer identifies the leased cleanup target");
    error.with_context("path", path.string()).with_context("link_count", std::to_string(named_status.st_nlink));
    return finish_error(std::move(error), false);
  }

  if (before_created_file_rollback_detach_for_test_)
  {
    try
    {
      before_created_file_rollback_detach_for_test_();
    }
    catch (...)
    {
      return finish_error(ava::core::Error(ava::core::ErrorCategory::Unknown, "created-session rollback pre-detach test hook failed"), false);
    }
  }

  std::string quarantine_name;
  bool detached = false;
  for (int attempt = 0; attempt < 16; ++attempt)
  {
    auto generated_name = rollback_quarantine_name(session_name);
    if (!generated_name)
    {
      auto error = std::move(generated_name.error());
      error.with_context("path", path.string());
      return finish_error(std::move(error), false);
    }
    quarantine_name = std::move(*generated_name);
    if (renameat2_no_replace(parent_fd, session_name, quarantine_name) == 0)
    {
      detached = true;
      break;
    }

    int const error_number = errno;
    if (error_number == EEXIST)
      continue;
    if (renameat2_no_replace_unavailable(error_number))
    {
      auto error = path_io_error("Linux renameat2 RENAME_NOREPLACE is unavailable for created-session rollback", path, error_number);
      return finish_error(std::move(error), false);
    }
    return finish_error(path_io_error("failed to detach the created session name for rollback", path, error_number), false);
  }
  if (!detached)
  {
    auto error = ava::core::Error(ava::core::ErrorCategory::Io, "failed to allocate a unique created-session rollback quarantine path");
    error.with_context("path", path.string());
    return finish_error(std::move(error), false);
  }

  auto const quarantine_path = anchored_child_diagnostic_path(parent_fd, quarantine_name, parent_path);
  auto restore_or_preserve = [&](ava::core::Error error) -> ava::core::VoidResult {
    if (renameat2_no_replace(parent_fd, quarantine_name, session_name) == 0)
    {
      error.with_context("rollback_restored_path", path.string());
      return finish_error(std::move(error), true);
    }

    int const restore_error = errno;
    error.with_context("quarantine_path", quarantine_path.string());
    error.with_context("rollback_restore_cause", std::strerror(restore_error));
    if (renameat2_no_replace_unavailable(restore_error))
      error.with_context("rollback_restore_requirement", "Linux renameat2 RENAME_NOREPLACE");
    return finish_error(std::move(error), true);
  };

  if (after_created_file_rollback_detach_for_test_)
  {
    try
    {
      after_created_file_rollback_detach_for_test_();
    }
    catch (...)
    {
      auto error = ava::core::Error(ava::core::ErrorCategory::Unknown, "created-session rollback post-detach test hook failed");
      error.with_context("quarantine_path", quarantine_path.string());
      return finish_error(std::move(error), true);
    }
  }

  int detached_fd = ::openat(parent_fd, quarantine_name.c_str(), O_PATH | O_CLOEXEC | O_NOFOLLOW);
  if (detached_fd < 0)
  {
    auto error = path_io_error("failed to open the detached created session for rollback verification", quarantine_path, errno);
    return restore_or_preserve(std::move(error));
  }
  struct stat detached_status{};
  int detached_status_error = 0;
  if (fstat(detached_fd, &detached_status) != 0)
    detached_status_error = errno;
  int detached_close_error = 0;
  if (::close(detached_fd) != 0)
    detached_close_error = errno;
  detached_fd = -1;
  if (detached_status_error != 0)
  {
    auto error = path_io_error("failed to inspect the detached created session for rollback verification", quarantine_path, detached_status_error);
    return restore_or_preserve(std::move(error));
  }
  if (detached_close_error != 0)
  {
    auto error = path_io_error("failed to close the detached created session after rollback verification", quarantine_path, detached_close_error);
    error.with_context("quarantine_path", quarantine_path.string());
    return finish_error(std::move(error), true);
  }

  struct stat current_lease_status{};
  if (fstat(lease.fd_, &current_lease_status) != 0)
  {
    auto error = path_io_error("failed to revalidate the created session lease after rollback detachment", path, errno);
    return restore_or_preserve(std::move(error));
  }
  if (!same_file_identity(current_lease_status, detached_status) || current_lease_status.st_nlink != 1 || detached_status.st_nlink != 1)
  {
    auto error = ava::core::Error(ava::core::ErrorCategory::PermissionDenied, "detached session name does not identify the creating lease target");
    error.with_context("path", path.string())
        .with_context("quarantine_path", quarantine_path.string())
        .with_context("lease_link_count", std::to_string(current_lease_status.st_nlink))
        .with_context("detached_link_count", std::to_string(detached_status.st_nlink));
    return restore_or_preserve(std::move(error));
  }

  struct stat republished_status{};
  if (fstatat(parent_fd, session_name.c_str(), &republished_status, AT_SYMLINK_NOFOLLOW) == 0)
  {
    auto error = ava::core::Error(ava::core::ErrorCategory::PermissionDenied, "created session name was republished after rollback detachment");
    error.with_context("path", path.string()).with_context("quarantine_path", quarantine_path.string());
    return finish_error(std::move(error), true);
  }
  if (errno != ENOENT)
  {
    auto error = path_io_error("failed to check for created-session republication after rollback detachment", path, errno);
    error.with_context("quarantine_path", quarantine_path.string());
    return finish_error(std::move(error), true);
  }

  struct stat final_lease_status{};
  if (fstat(lease.fd_, &final_lease_status) != 0)
  {
    auto error = path_io_error("failed to revalidate the created session lease before rollback unlink", path, errno);
    error.with_context("quarantine_path", quarantine_path.string());
    return finish_error(std::move(error), true);
  }
  struct stat final_detached_status{};
  if (fstatat(parent_fd, quarantine_name.c_str(), &final_detached_status, AT_SYMLINK_NOFOLLOW) != 0)
  {
    auto error = path_io_error("failed to revalidate the detached created session before rollback unlink", quarantine_path, errno);
    error.with_context("quarantine_path", quarantine_path.string());
    return finish_error(std::move(error), true);
  }
  if (!same_file_identity(final_lease_status, final_detached_status) || final_lease_status.st_nlink != 1 || final_detached_status.st_nlink != 1)
  {
    auto error = ava::core::Error(ava::core::ErrorCategory::PermissionDenied, "detached created session changed before rollback unlink");
    error.with_context("path", path.string())
        .with_context("quarantine_path", quarantine_path.string())
        .with_context("lease_link_count", std::to_string(final_lease_status.st_nlink))
        .with_context("detached_link_count", std::to_string(final_detached_status.st_nlink));
    return finish_error(std::move(error), true);
  }

  if (::unlinkat(parent_fd, quarantine_name.c_str(), 0) != 0)
  {
    auto error = path_io_error("failed to remove the detached created session during rollback", quarantine_path, errno);
    error.with_context("quarantine_path", quarantine_path.string());
    return finish_error(std::move(error), true);
  }

  struct stat unlinked_status{};
  if (fstat(lease.fd_, &unlinked_status) != 0)
  {
    auto error = path_io_error("created session was removed but its lease could not be revalidated", path, errno);
    error.with_context("quarantine_path", quarantine_path.string());
    return finish_error(std::move(error), true);
  }
  if (unlinked_status.st_nlink != 0)
  {
    auto error = ava::core::Error(ava::core::ErrorCategory::PermissionDenied, "created session cleanup left an unexpected linked name");
    error.with_context("path", path.string()).with_context("link_count", std::to_string(unlinked_status.st_nlink));
    return finish_error(std::move(error), true);
  }
  if (fsync(parent_fd) != 0)
  {
    auto error = path_io_error("created session was removed but the rollback directory sync failed", parent_path, errno);
    error.with_context("quarantine_path", quarantine_path.string());
    return finish_error(std::move(error), false);
  }
  if (::close(parent_fd) != 0)
  {
    int const close_error = errno;
    parent_fd = -1;
    auto error = path_io_error("created session was removed but the rollback directory close failed", parent_path, close_error);
    error.with_context("quarantine_path", quarantine_path.string());
    return std::unexpected(std::move(error));
  }
  parent_fd = -1;
  return {};
}

ava::core::Result<std::optional<std::filesystem::path>> SessionStore::recover_torn_tail(SessionLease const& lease, SessionReadLimits limits,
                                                                                        SessionCancelCallback cancel_requested) const
{
  if (ephemeral_state_)
    return std::unexpected(ava::core::Error(ava::core::ErrorCategory::InvalidArgument, "ephemeral sessions do not support torn tail recovery"));
  if (auto valid_limits = validate_read_limits(limits); !valid_limits)
    return std::unexpected(std::move(valid_limits.error()));
  if (auto valid_session_id = validate_session_id(options_.session_id); !valid_session_id)
    return std::unexpected(std::move(valid_session_id.error()));
  if (lease.fd_ < 0 || lease.canonical_path_.empty())
    return std::unexpected(ava::core::Error(ava::core::ErrorCategory::InvalidArgument, "torn tail recovery requires an active session lease"));
  if (auto canceled = check_recovery_cancellation(cancel_requested); !canceled)
    return std::unexpected(std::move(canceled.error()));

  auto const path = session_path();
  auto const parent_path = path.parent_path();
  auto const session_name = path.filename().string();
  auto& append_mutex = append_mutex_for_path(path);
  std::lock_guard append_lock(append_mutex);

  ScopedFd parent_fd(::open(parent_path.c_str(), O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW));
  if (parent_fd.get() < 0)
    return std::unexpected(path_io_error("failed to open the session directory for torn tail recovery", parent_path, errno));
  ScopedFd repair_fd;
  auto close_fd = [](ScopedFd& fd) { return fd.close_checked(); };
  auto close_both_best_effort = [&] {
    static_cast<void>(close_fd(repair_fd));
    static_cast<void>(close_fd(parent_fd));
  };

  struct stat parent_status{};
  if (fstat(parent_fd.get(), &parent_status) != 0 || !S_ISDIR(parent_status.st_mode))
  {
    int const error_number = errno;
    close_both_best_effort();
    return std::unexpected(path_io_error("session recovery parent is not a directory", parent_path, error_number));
  }

  repair_fd.reset(::openat(parent_fd.get(), session_name.c_str(), O_RDWR | O_CLOEXEC | O_NOFOLLOW));
  if (repair_fd.get() < 0)
  {
    int const error_number = errno;
    close_both_best_effort();
    return std::unexpected(path_io_error("failed to open the original session path for torn tail recovery", path, error_number));
  }

  struct stat lease_status{};
  struct stat repair_status{};
  if (fstat(lease.fd_, &lease_status) != 0)
  {
    auto error = path_io_error("failed to verify session lease for torn tail recovery", lease.canonical_path_, errno);
    close_both_best_effort();
    return std::unexpected(std::move(error));
  }
  if (fstat(repair_fd.get(), &repair_status) != 0)
  {
    auto error = path_io_error("failed to inspect session opened for torn tail recovery", path, errno);
    close_both_best_effort();
    return std::unexpected(std::move(error));
  }
  if (!same_file_identity(lease_status, repair_status))
  {
    close_both_best_effort();
    auto error = ava::core::Error(ava::core::ErrorCategory::Session,
                                  "session lease does not match the store because its descriptor does not identify the recovery target");
    error.with_context("path", path.string()).with_context("lease_path", lease.canonical_path_.string());
    return std::unexpected(std::move(error));
  }
  if (repair_status.st_size < 0)
  {
    close_both_best_effort();
    return std::unexpected(path_io_error("session size is invalid during torn tail recovery", path));
  }
  if (static_cast<std::uintmax_t>(repair_status.st_size) > limits.max_file_bytes)
  {
    close_both_best_effort();
    auto error = ava::core::Error(ava::core::ErrorCategory::Session, "session file exceeds torn tail recovery byte limit");
    error.with_context("path", path.string()).with_context("max_file_bytes", std::to_string(limits.max_file_bytes));
    return std::unexpected(std::move(error));
  }

  auto close_without_repair = [&]() -> ava::core::Result<std::optional<std::filesystem::path>> {
    int const repair_close_error = close_fd(repair_fd);
    int const parent_close_error = close_fd(parent_fd);
    if (repair_close_error != 0)
      return std::unexpected(path_io_error("failed to close complete session during torn tail recovery", path, repair_close_error));
    if (parent_close_error != 0)
      return std::unexpected(path_io_error("failed to close session directory during torn tail recovery", parent_path, parent_close_error));
    return std::optional<std::filesystem::path>{};
  };
  if (repair_status.st_size == 0)
    return close_without_repair();

  char final_byte = '\0';
  ssize_t final_byte_count = 0;
  do
  {
    final_byte_count = ::pread(repair_fd.get(), &final_byte, 1, repair_status.st_size - 1);
  } while (final_byte_count < 0 && errno == EINTR);
  if (final_byte_count != 1)
  {
    int const error_number = final_byte_count < 0 ? errno : EIO;
    close_both_best_effort();
    return std::unexpected(path_io_error("failed to inspect final session byte during torn tail recovery", path, error_number));
  }
  if (final_byte == '\n')
    return close_without_repair();

  std::unordered_set<std::string> entry_ids;
  entry_ids.reserve(std::min<std::size_t>(limits.max_entries, 1024));
  std::size_t entry_count = 0;
  auto validate_entry_integrity = [&](SessionEntry const& entry) -> ava::core::VoidResult {
    if (entry_count >= limits.max_entries)
    {
      auto error = ava::core::Error(ava::core::ErrorCategory::Session, "session entry count exceeds torn tail recovery limit");
      error.with_context("path", path.string()).with_context("max_entries", std::to_string(limits.max_entries));
      return std::unexpected(std::move(error));
    }
    if (entry_ids.contains(entry.id))
    {
      auto error = ava::core::Error(ava::core::ErrorCategory::Session, "session recovery found a duplicate entry id");
      error.with_context("path", path.string()).with_context("entry_id", entry.id);
      return std::unexpected(std::move(error));
    }
    if (!entry.parent_id.empty() && !entry_ids.contains(entry.parent_id))
    {
      auto error = ava::core::Error(ava::core::ErrorCategory::Session, "session recovery found a parent id that does not name an earlier record");
      error.with_context("path", path.string()).with_context("entry_id", entry.id).with_context("parent_id", entry.parent_id);
      return std::unexpected(std::move(error));
    }
    entry_ids.insert(entry.id);
    ++entry_count;
    return {};
  };

  std::array<char, 8192> buffer{};
  std::string line;
  line.reserve(std::min<std::size_t>(limits.max_line_bytes, buffer.size()));
  off_t offset = 0;
  off_t last_validated_newline = 0;
  while (offset < repair_status.st_size)
  {
    if (auto canceled = check_recovery_cancellation(cancel_requested); !canceled)
    {
      close_both_best_effort();
      return std::unexpected(std::move(canceled.error()));
    }
    auto const remaining = repair_status.st_size - offset;
    auto const wanted = remaining < static_cast<off_t>(buffer.size()) ? static_cast<std::size_t>(remaining) : buffer.size();
    ssize_t count = 0;
    do
    {
      count = ::pread(repair_fd.get(), buffer.data(), wanted, offset);
    } while (count < 0 && errno == EINTR);
    if (count < 0)
    {
      auto error = path_io_error("failed while scanning session for torn tail recovery", path, errno);
      close_both_best_effort();
      return std::unexpected(std::move(error));
    }
    if (count == 0)
    {
      auto error = path_io_error("session became unreadable while scanning for torn tail recovery", path);
      close_both_best_effort();
      return std::unexpected(std::move(error));
    }
    for (ssize_t index = 0; index < count; ++index)
    {
      char const ch = buffer[static_cast<std::size_t>(index)];
      if (ch == '\n')
      {
        if (auto canceled = check_recovery_cancellation(cancel_requested); !canceled)
        {
          close_both_best_effort();
          return std::unexpected(std::move(canceled.error()));
        }
        auto entry = parse_strict_session_record(line, path, false);
        if (!entry)
        {
          auto error = std::move(entry.error());
          error.with_context("record_end_offset", std::to_string(offset + index + 1));
          close_both_best_effort();
          return std::unexpected(std::move(error));
        }
        if (auto valid_integrity = validate_entry_integrity(*entry); !valid_integrity)
        {
          auto error = std::move(valid_integrity.error());
          error.with_context("record_end_offset", std::to_string(offset + index + 1));
          close_both_best_effort();
          return std::unexpected(std::move(error));
        }
        line.clear();
        last_validated_newline = offset + index + 1;
      }
      else
      {
        if (line.size() >= limits.max_line_bytes || line.size() + 1 >= kMaxSessionLineBytes)
        {
          close_both_best_effort();
          auto error = ava::core::Error(ava::core::ErrorCategory::Session, "session line exceeds torn tail recovery scan limit");
          error.with_context("path", path.string()).with_context("max_line_bytes", std::to_string(limits.max_line_bytes));
          return std::unexpected(std::move(error));
        }
        line.push_back(ch);
      }
    }
    offset += count;
  }

  if (line.empty())
  {
    int const repair_close_error = close_fd(repair_fd);
    int const parent_close_error = close_fd(parent_fd);
    if (repair_close_error != 0)
      return std::unexpected(path_io_error("failed to close session after torn tail scan", path, repair_close_error));
    if (parent_close_error != 0)
      return std::unexpected(path_io_error("failed to close session directory after torn tail scan", parent_path, parent_close_error));
    return std::optional<std::filesystem::path>{};
  }

  auto const strict_status = ava::core::validate_strict_json(line, ava::core::json::kMaxNestingDepth);
  bool const append_final_lf = strict_status == ava::core::StrictJsonStatus::Valid;
  if (append_final_lf)
  {
    auto entry = parse_strict_session_record(line, path, true);
    if (!entry)
    {
      auto error = std::move(entry.error());
      close_both_best_effort();
      return std::unexpected(std::move(error));
    }
    if (auto valid_integrity = validate_entry_integrity(*entry); !valid_integrity)
    {
      auto error = std::move(valid_integrity.error());
      close_both_best_effort();
      return std::unexpected(std::move(error));
    }
  }
  else if (strict_status != ava::core::StrictJsonStatus::Invalid)
  {
    auto error = strict_session_record_error(strict_status, path, true);
    close_both_best_effort();
    return std::unexpected(std::move(error));
  }

  if (auto canceled = check_recovery_cancellation(cancel_requested); !canceled)
  {
    close_both_best_effort();
    return std::unexpected(std::move(canceled.error()));
  }
  if (auto unchanged = require_unchanged_repair_namespace(repair_fd.get(), parent_fd.get(), repair_status, parent_status, parent_path, session_name, path);
      !unchanged)
  {
    auto error = std::move(unchanged.error());
    close_both_best_effort();
    return std::unexpected(std::move(error));
  }

  if (append_final_lf)
  {
    ssize_t count = 0;
    do
    {
      count = ::pwrite(repair_fd.get(), "\n", 1, repair_status.st_size);
    } while (count < 0 && errno == EINTR);
    if (count != 1)
    {
      int const error_number = count < 0 ? errno : EIO;
      auto error = path_io_error("failed to append final LF during torn tail recovery", path, error_number);
      close_both_best_effort();
      return std::unexpected(std::move(error));
    }
    if (fdatasync(repair_fd.get()) != 0)
    {
      auto error = path_io_error("failed to sync final LF during torn tail recovery", path, errno);
      close_both_best_effort();
      return std::unexpected(std::move(error));
    }
    struct stat repaired_status{};
    if (fstat(repair_fd.get(), &repaired_status) != 0)
    {
      auto error = path_io_error("failed to inspect session after appending final LF", path, errno);
      close_both_best_effort();
      return std::unexpected(std::move(error));
    }
    if (auto unchanged = require_unchanged_repair_namespace(repair_fd.get(), parent_fd.get(), repaired_status, parent_status, parent_path, session_name, path);
        !unchanged)
    {
      auto error = std::move(unchanged.error());
      close_both_best_effort();
      return std::unexpected(std::move(error));
    }
    int const repair_close_error = close_fd(repair_fd);
    int const parent_close_error = close_fd(parent_fd);
    if (repair_close_error != 0)
      return std::unexpected(path_io_error("failed to close session after torn tail recovery", path, repair_close_error));
    if (parent_close_error != 0)
      return std::unexpected(path_io_error("failed to close session directory after torn tail recovery", parent_path, parent_close_error));
    return std::optional<std::filesystem::path>{};
  }

  auto quarantined = quarantine_session_suffix(parent_fd.get(), parent_path, session_name, "torn-tail", line, cancel_requested,
                                               before_recovery_quarantine_publication_for_test_ ? &before_recovery_quarantine_publication_for_test_ : nullptr);
  if (!quarantined)
  {
    auto error = std::move(quarantined.error());
    close_both_best_effort();
    return std::unexpected(std::move(error));
  }
  if (after_recovery_quarantine_publication_for_test_)
  {
    try
    {
      after_recovery_quarantine_publication_for_test_();
    }
    catch (...)
    {
      auto error = ava::core::Error(ava::core::ErrorCategory::Unknown, "session recovery post-publication test hook failed");
      error.with_context("quarantine_path", quarantined->string());
      close_both_best_effort();
      return std::unexpected(std::move(error));
    }
  }
  if (auto unchanged = require_unchanged_repair_namespace(repair_fd.get(), parent_fd.get(), repair_status, parent_status, parent_path, session_name, path);
      !unchanged)
  {
    auto error = std::move(unchanged.error());
    error.with_context("quarantine_path", quarantined->string());
    close_both_best_effort();
    return std::unexpected(std::move(error));
  }
  if (ftruncate(repair_fd.get(), last_validated_newline) != 0)
  {
    auto error = path_io_error("failed to truncate quarantined torn session tail", path, errno);
    error.with_context("quarantine_path", quarantined->string());
    close_both_best_effort();
    return std::unexpected(std::move(error));
  }
  if (fdatasync(repair_fd.get()) != 0)
  {
    auto error = path_io_error("session tail was truncated but the source sync failed", path, errno);
    error.with_context("quarantine_path", quarantined->string());
    close_both_best_effort();
    return std::unexpected(std::move(error));
  }
  struct stat repaired_status{};
  if (fstat(repair_fd.get(), &repaired_status) != 0)
  {
    auto error = path_io_error("failed to inspect session after truncating its torn tail", path, errno);
    error.with_context("quarantine_path", quarantined->string());
    close_both_best_effort();
    return std::unexpected(std::move(error));
  }
  if (auto unchanged = require_unchanged_repair_namespace(repair_fd.get(), parent_fd.get(), repaired_status, parent_status, parent_path, session_name, path);
      !unchanged)
  {
    auto error = std::move(unchanged.error());
    error.with_context("quarantine_path", quarantined->string());
    close_both_best_effort();
    return std::unexpected(std::move(error));
  }
  int const repair_close_error = close_fd(repair_fd);
  int const parent_close_error = close_fd(parent_fd);
  if (repair_close_error != 0)
  {
    auto error = path_io_error("failed to close repaired session", path, repair_close_error);
    error.with_context("quarantine_path", quarantined->string());
    return std::unexpected(std::move(error));
  }
  if (parent_close_error != 0)
  {
    auto error = path_io_error("failed to close session directory after torn tail recovery", parent_path, parent_close_error);
    error.with_context("quarantine_path", quarantined->string());
    return std::unexpected(std::move(error));
  }
  return std::optional<std::filesystem::path>(std::move(*quarantined));
}

ava::core::Result<std::optional<AssistantOutputSuffixRecovery>> SessionStore::recover_incomplete_assistant_output_suffix(
    SessionReadLimits limits, SessionCancelCallback cancel_requested) const
{
  return recover_incomplete_assistant_output_suffix_ephemeral_impl(limits, std::move(cancel_requested), false);
}

ava::core::Result<std::optional<AssistantOutputSuffixRecovery>> SessionStore::recover_incomplete_assistant_output_suffix_ephemeral_impl(
    SessionReadLimits limits, SessionCancelCallback cancel_requested, bool mutation_serialization_held) const
{
  if (!ephemeral_state_)
  {
    return std::unexpected(
        ava::core::Error(ava::core::ErrorCategory::InvalidArgument, "in-memory assistant-output suffix recovery requires an ephemeral session"));
  }
  if (auto valid_limits = validate_read_limits(limits); !valid_limits)
    return std::unexpected(std::move(valid_limits.error()));

  std::optional<std::unique_lock<std::mutex>> mutation_lock;
  if (!mutation_serialization_held)
    mutation_lock.emplace(ephemeral_state_->mutation_mutex);
  std::lock_guard entries_lock(ephemeral_state_->entries_mutex);
  std::vector<SessionEntry> const& entries = ephemeral_state_->entries;
  std::unordered_set<std::string> entry_ids;
  std::size_t total_bytes = 0;
  for (std::size_t index = 0; index < entries.size(); ++index)
  {
    if (auto canceled = check_recovery_cancellation(cancel_requested); !canceled)
      return std::unexpected(std::move(canceled.error()));
    if (index >= limits.max_entries)
    {
      auto error = ava::core::Error(ava::core::ErrorCategory::Session, "ephemeral session entry count exceeds assistant-output recovery limit");
      error.with_context("max_entries", std::to_string(limits.max_entries));
      return std::unexpected(std::move(error));
    }
    if (entry_ids.contains(entries[index].id) || (!entries[index].parent_id.empty() && !entry_ids.contains(entries[index].parent_id)))
    {
      auto error = ava::core::Error(ava::core::ErrorCategory::Session, "ephemeral assistant-output recovery found malformed session entry identity history");
      error.with_context("entry_id", entries[index].id);
      return std::unexpected(std::move(error));
    }
    entry_ids.insert(entries[index].id);
    auto serialized = serialize_session_entry_line(entries[index]);
    if (!serialized)
      return std::unexpected(std::move(serialized.error()));
    if (serialized->size() > limits.max_line_bytes || serialized->size() + 1 > limits.max_file_bytes - total_bytes)
    {
      auto error = ava::core::Error(ava::core::ErrorCategory::Session, "ephemeral session exceeds assistant-output recovery read limit");
      error.with_context("max_file_bytes", std::to_string(limits.max_file_bytes)).with_context("max_line_bytes", std::to_string(limits.max_line_bytes));
      return std::unexpected(std::move(error));
    }
    total_bytes += serialized->size() + 1;
  }

  auto const projection = classify_assistant_output(entries);
  std::optional<std::size_t> suffix_start;
  for (auto const& diagnostic : projection.diagnostics)
  {
    if (diagnostic.severity == AssistantOutputDiagnosticSeverity::Warning && diagnostic.kind == AssistantOutputDiagnosticKind::IncompleteAssistantTurn)
    {
      if (suffix_start || diagnostic.entry_index >= entries.size())
        return std::unexpected(ava::core::Error(ava::core::ErrorCategory::Session, "ephemeral assistant-output recovery found an ambiguous staging suffix"));
      suffix_start = diagnostic.entry_index;
      continue;
    }
    auto error = ava::core::Error(ava::core::ErrorCategory::Session, "ephemeral assistant-output history is malformed; refusing suffix recovery");
    error.with_context("diagnostic_kind", std::string(to_string(diagnostic.kind)))
        .with_context("diagnostic_entry_id", diagnostic.entry_id)
        .with_context("diagnostic", diagnostic.message);
    return std::unexpected(std::move(error));
  }
  if (!suffix_start)
    return std::optional<AssistantOutputSuffixRecovery>{};
  for (std::size_t index = *suffix_start; index < entries.size(); ++index)
  {
    if (entries[index].type != EntryType::AssistantOutputItem)
    {
      return std::unexpected(ava::core::Error(ava::core::ErrorCategory::Session, "ephemeral assistant-output recovery suffix contains an unrelated entry"));
    }
  }

  std::size_t removed_bytes = 0;
  for (std::size_t index = *suffix_start; index < entries.size(); ++index)
  {
    auto serialized = serialize_session_entry_line(entries[index]);
    if (!serialized)
      return std::unexpected(std::move(serialized.error()));
    removed_bytes += serialized->size() + 1;
  }
  auto const removed_entries = entries.size() - *suffix_start;
  ephemeral_state_->entries.erase(ephemeral_state_->entries.begin() + static_cast<std::ptrdiff_t>(*suffix_start), ephemeral_state_->entries.end());
  return std::optional<AssistantOutputSuffixRecovery>(
      AssistantOutputSuffixRecovery{.quarantine_path = std::nullopt, .removed_entry_count = removed_entries, .removed_byte_count = removed_bytes});
}

ava::core::Result<std::optional<AssistantOutputSuffixRecovery>> SessionStore::recover_incomplete_assistant_output_suffix(
    SessionLease const& lease, SessionReadLimits limits, SessionCancelCallback cancel_requested) const
{
  if (ephemeral_state_)
  {
    return std::unexpected(
        ava::core::Error(ava::core::ErrorCategory::InvalidArgument, "persistent assistant-output suffix recovery does not accept an ephemeral session"));
  }
  if (auto valid_limits = validate_read_limits(limits); !valid_limits)
    return std::unexpected(std::move(valid_limits.error()));
  if (auto valid_session_id = validate_session_id(options_.session_id); !valid_session_id)
    return std::unexpected(std::move(valid_session_id.error()));
  if (!lease.active() || lease.canonical_path_.empty())
  {
    return std::unexpected(ava::core::Error(ava::core::ErrorCategory::InvalidArgument, "assistant-output suffix recovery requires an active session lease"));
  }
  auto const path = session_path();
  if (normalized_absolute_path(path) != lease.canonical_path_)
  {
    auto error = ava::core::Error(ava::core::ErrorCategory::Session, "assistant-output suffix recovery lease does not exactly match the store");
    error.with_context("path", path.string()).with_context("lease_path", lease.canonical_path_.string());
    return std::unexpected(std::move(error));
  }
  if (auto canceled = check_recovery_cancellation(cancel_requested); !canceled)
    return std::unexpected(std::move(canceled.error()));

  auto const parent_path = path.parent_path();
  auto const session_name = path.filename().string();
  auto& append_mutex = append_mutex_for_path(path);
  std::lock_guard append_lock(append_mutex);
  ScopedFd parent_fd(::open(parent_path.c_str(), O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW));
  if (parent_fd.get() < 0)
  {
    return std::unexpected(path_io_error("failed to open the session directory for assistant-output suffix recovery", parent_path, errno));
  }
  ScopedFd repair_fd(::openat(parent_fd.get(), session_name.c_str(), O_RDWR | O_NONBLOCK | O_CLOEXEC | O_NOFOLLOW));
  if (repair_fd.get() < 0)
  {
    auto error = path_io_error("failed to securely open the original session path for assistant-output suffix recovery", path, errno);
    static_cast<void>(parent_fd.close_checked());
    return std::unexpected(std::move(error));
  }
  auto close_both_best_effort = [&] {
    static_cast<void>(repair_fd.close_checked());
    static_cast<void>(parent_fd.close_checked());
  };
  auto close_without_repair = [&]() -> ava::core::Result<std::optional<AssistantOutputSuffixRecovery>> {
    int const repair_close_error = repair_fd.close_checked();
    int const parent_close_error = parent_fd.close_checked();
    if (repair_close_error != 0)
      return std::unexpected(path_io_error("failed to close complete session during assistant-output suffix recovery", path, repair_close_error));
    if (parent_close_error != 0)
      return std::unexpected(path_io_error("failed to close session directory during assistant-output suffix recovery", parent_path, parent_close_error));
    return std::optional<AssistantOutputSuffixRecovery>{};
  };

  struct stat parent_status{};
  if (fstat(parent_fd.get(), &parent_status) != 0 || !S_ISDIR(parent_status.st_mode))
  {
    int const error_number = errno;
    close_both_best_effort();
    return std::unexpected(path_io_error("session recovery parent is not a directory", parent_path, error_number));
  }
  struct stat lease_status{};
  struct stat repair_status{};
  if (fstat(lease.fd_, &lease_status) != 0)
  {
    auto error = path_io_error("failed to verify session lease for assistant-output suffix recovery", lease.canonical_path_, errno);
    close_both_best_effort();
    return std::unexpected(std::move(error));
  }
  if (fstat(repair_fd.get(), &repair_status) != 0)
  {
    auto error = path_io_error("failed to inspect session opened for assistant-output suffix recovery", path, errno);
    close_both_best_effort();
    return std::unexpected(std::move(error));
  }
  if (!same_file_identity(lease_status, repair_status))
  {
    close_both_best_effort();
    auto error = ava::core::Error(ava::core::ErrorCategory::Session, "session lease does not identify the named assistant-output recovery target");
    error.with_context("path", path.string()).with_context("lease_path", lease.canonical_path_.string());
    return std::unexpected(std::move(error));
  }
  if (repair_status.st_size < 0 || static_cast<std::uintmax_t>(repair_status.st_size) > limits.max_file_bytes)
  {
    close_both_best_effort();
    auto error = ava::core::Error(ava::core::ErrorCategory::Session, "session file exceeds assistant-output suffix recovery byte limit");
    error.with_context("path", path.string()).with_context("max_file_bytes", std::to_string(limits.max_file_bytes));
    return std::unexpected(std::move(error));
  }
  if (repair_status.st_size == 0)
    return close_without_repair();

  char final_byte = '\0';
  ssize_t final_byte_count = 0;
  do
  {
    final_byte_count = ::pread(repair_fd.get(), &final_byte, 1, repair_status.st_size - 1);
  } while (final_byte_count < 0 && errno == EINTR);
  if (final_byte_count != 1)
  {
    auto error = path_io_error("failed to inspect final session byte during assistant-output suffix recovery", path, final_byte_count < 0 ? errno : EIO);
    close_both_best_effort();
    return std::unexpected(std::move(error));
  }
  if (final_byte != '\n')
  {
    close_both_best_effort();
    auto error =
        ava::core::Error(ava::core::ErrorCategory::Session, "assistant-output suffix recovery requires torn-tail recovery before scanning complete records");
    error.with_context("path", path.string());
    return std::unexpected(std::move(error));
  }

  struct PhysicalRecord
  {
    off_t start = 0;
    off_t end = 0;
    SessionEntry entry;
  };
  std::vector<PhysicalRecord> records;
  std::unordered_set<std::string> entry_ids;
  std::array<char, 8192> buffer{};
  std::string line;
  line.reserve(std::min<std::size_t>(limits.max_line_bytes, buffer.size()));
  off_t offset = 0;
  off_t line_start = 0;
  while (offset < repair_status.st_size)
  {
    if (auto canceled = check_recovery_cancellation(cancel_requested); !canceled)
    {
      close_both_best_effort();
      return std::unexpected(std::move(canceled.error()));
    }
    auto const remaining = repair_status.st_size - offset;
    auto const wanted = remaining < static_cast<off_t>(buffer.size()) ? static_cast<std::size_t>(remaining) : buffer.size();
    ssize_t count = 0;
    do
    {
      count = ::pread(repair_fd.get(), buffer.data(), wanted, offset);
    } while (count < 0 && errno == EINTR);
    if (count <= 0)
    {
      auto error = path_io_error("failed while scanning session for assistant-output suffix recovery", path, count < 0 ? errno : EIO);
      close_both_best_effort();
      return std::unexpected(std::move(error));
    }
    for (ssize_t index = 0; index < count; ++index)
    {
      char const ch = buffer[static_cast<std::size_t>(index)];
      if (ch != '\n')
      {
        if (line.size() >= limits.max_line_bytes || line.size() + 1 >= kMaxSessionLineBytes)
        {
          close_both_best_effort();
          auto error = ava::core::Error(ava::core::ErrorCategory::Session, "session line exceeds assistant-output suffix recovery scan limit");
          error.with_context("path", path.string()).with_context("max_line_bytes", std::to_string(limits.max_line_bytes));
          return std::unexpected(std::move(error));
        }
        line.push_back(ch);
        continue;
      }

      if (auto canceled = check_recovery_cancellation(cancel_requested); !canceled)
      {
        close_both_best_effort();
        return std::unexpected(std::move(canceled.error()));
      }
      if (records.size() >= limits.max_entries)
      {
        close_both_best_effort();
        auto error = ava::core::Error(ava::core::ErrorCategory::Session, "session entry count exceeds assistant-output suffix recovery limit");
        error.with_context("path", path.string()).with_context("max_entries", std::to_string(limits.max_entries));
        return std::unexpected(std::move(error));
      }
      auto parsed = parse_strict_session_record(line, path, false);
      if (!parsed)
      {
        auto error = std::move(parsed.error());
        error.with_context("record_end_offset", std::to_string(offset + index + 1));
        close_both_best_effort();
        return std::unexpected(std::move(error));
      }
      if (entry_ids.contains(parsed->id) || (!parsed->parent_id.empty() && !entry_ids.contains(parsed->parent_id)))
      {
        close_both_best_effort();
        auto error = ava::core::Error(ava::core::ErrorCategory::Session, "assistant-output suffix recovery found malformed session entry identity history");
        error.with_context("entry_id", parsed->id).with_context("record_end_offset", std::to_string(offset + index + 1));
        return std::unexpected(std::move(error));
      }
      entry_ids.insert(parsed->id);
      records.push_back(PhysicalRecord{.start = line_start, .end = offset + index + 1, .entry = std::move(*parsed)});
      line.clear();
      line_start = offset + index + 1;
    }
    offset += count;
  }
  if (!line.empty() || records.empty())
  {
    close_both_best_effort();
    return std::unexpected(
        ava::core::Error(ava::core::ErrorCategory::Session, "assistant-output suffix recovery could not establish a complete framed session history"));
  }

  std::vector<SessionEntry> entries;
  entries.reserve(records.size());
  for (auto const& record : records) entries.push_back(record.entry);
  auto const projection = classify_assistant_output(entries);
  std::optional<std::size_t> suffix_start_index;
  for (auto const& diagnostic : projection.diagnostics)
  {
    if (diagnostic.severity == AssistantOutputDiagnosticSeverity::Warning && diagnostic.kind == AssistantOutputDiagnosticKind::IncompleteAssistantTurn)
    {
      if (suffix_start_index || diagnostic.entry_index >= records.size())
      {
        close_both_best_effort();
        return std::unexpected(ava::core::Error(ava::core::ErrorCategory::Session, "assistant-output suffix recovery found an ambiguous final staging suffix"));
      }
      suffix_start_index = diagnostic.entry_index;
      continue;
    }
    auto error = ava::core::Error(ava::core::ErrorCategory::Session, "assistant-output history is malformed; refusing suffix recovery");
    error.with_context("diagnostic_kind", std::string(to_string(diagnostic.kind)))
        .with_context("diagnostic_entry_id", diagnostic.entry_id)
        .with_context("diagnostic", diagnostic.message);
    close_both_best_effort();
    return std::unexpected(std::move(error));
  }
  if (!suffix_start_index)
    return close_without_repair();
  for (std::size_t index = *suffix_start_index; index < records.size(); ++index)
  {
    if (records[index].entry.type != EntryType::AssistantOutputItem)
    {
      close_both_best_effort();
      return std::unexpected(ava::core::Error(ava::core::ErrorCategory::Session, "assistant-output recovery suffix contains an unrelated session entry"));
    }
  }

  off_t const suffix_start_offset = records[*suffix_start_index].start;
  auto const removed_byte_count = static_cast<std::size_t>(repair_status.st_size - suffix_start_offset);
  std::string suffix_bytes(removed_byte_count, '\0');
  std::size_t suffix_offset = 0;
  while (suffix_offset < suffix_bytes.size())
  {
    if (auto canceled = check_recovery_cancellation(cancel_requested); !canceled)
    {
      close_both_best_effort();
      return std::unexpected(std::move(canceled.error()));
    }
    ssize_t count = 0;
    do
    {
      count = ::pread(repair_fd.get(), suffix_bytes.data() + suffix_offset, suffix_bytes.size() - suffix_offset,
                      suffix_start_offset + static_cast<off_t>(suffix_offset));
    } while (count < 0 && errno == EINTR);
    if (count <= 0)
    {
      auto error = path_io_error("failed to read the exact assistant-output suffix for quarantine", path, count < 0 ? errno : EIO);
      close_both_best_effort();
      return std::unexpected(std::move(error));
    }
    suffix_offset += static_cast<std::size_t>(count);
  }
  if (auto unchanged = require_unchanged_repair_namespace(repair_fd.get(), parent_fd.get(), repair_status, parent_status, parent_path, session_name, path);
      !unchanged)
  {
    auto error = std::move(unchanged.error());
    close_both_best_effort();
    return std::unexpected(std::move(error));
  }

  auto quarantined = quarantine_session_suffix(parent_fd.get(), parent_path, session_name, "incomplete-assistant-output", suffix_bytes, cancel_requested,
                                               before_recovery_quarantine_publication_for_test_ ? &before_recovery_quarantine_publication_for_test_ : nullptr);
  if (!quarantined)
  {
    auto error = std::move(quarantined.error());
    close_both_best_effort();
    return std::unexpected(std::move(error));
  }
  if (after_recovery_quarantine_publication_for_test_)
  {
    try
    {
      after_recovery_quarantine_publication_for_test_();
    }
    catch (...)
    {
      auto error = ava::core::Error(ava::core::ErrorCategory::Unknown, "assistant-output suffix recovery post-publication test hook failed");
      error.with_context("quarantine_path", quarantined->string());
      close_both_best_effort();
      return std::unexpected(std::move(error));
    }
  }
  if (auto unchanged = require_unchanged_repair_namespace(repair_fd.get(), parent_fd.get(), repair_status, parent_status, parent_path, session_name, path);
      !unchanged)
  {
    auto error = std::move(unchanged.error());
    error.with_context("quarantine_path", quarantined->string());
    close_both_best_effort();
    return std::unexpected(std::move(error));
  }
  if (ftruncate(repair_fd.get(), suffix_start_offset) != 0)
  {
    auto error = path_io_error("failed to truncate quarantined incomplete assistant-output suffix", path, errno);
    error.with_context("quarantine_path", quarantined->string());
    close_both_best_effort();
    return std::unexpected(std::move(error));
  }
  if (fdatasync(repair_fd.get()) != 0)
  {
    auto error = path_io_error("assistant-output suffix was truncated but the source sync failed", path, errno);
    error.with_context("quarantine_path", quarantined->string());
    close_both_best_effort();
    return std::unexpected(std::move(error));
  }
  struct stat repaired_status{};
  if (fstat(repair_fd.get(), &repaired_status) != 0)
  {
    auto error = path_io_error("failed to inspect session after truncating incomplete assistant-output suffix", path, errno);
    error.with_context("quarantine_path", quarantined->string());
    close_both_best_effort();
    return std::unexpected(std::move(error));
  }
  if (auto unchanged = require_unchanged_repair_namespace(repair_fd.get(), parent_fd.get(), repaired_status, parent_status, parent_path, session_name, path);
      !unchanged)
  {
    auto error = std::move(unchanged.error());
    error.with_context("quarantine_path", quarantined->string());
    close_both_best_effort();
    return std::unexpected(std::move(error));
  }
  int const repair_close_error = repair_fd.close_checked();
  int const parent_close_error = parent_fd.close_checked();
  if (repair_close_error != 0)
  {
    auto error = path_io_error("failed to close repaired session after assistant-output suffix recovery", path, repair_close_error);
    error.with_context("quarantine_path", quarantined->string());
    return std::unexpected(std::move(error));
  }
  if (parent_close_error != 0)
  {
    auto error = path_io_error("failed to close session directory after assistant-output suffix recovery", parent_path, parent_close_error);
    error.with_context("quarantine_path", quarantined->string());
    return std::unexpected(std::move(error));
  }
  return std::optional<AssistantOutputSuffixRecovery>(AssistantOutputSuffixRecovery{
      .quarantine_path = std::move(*quarantined), .removed_entry_count = records.size() - *suffix_start_index, .removed_byte_count = removed_byte_count});
}

SessionLease::SessionLease(int fd, std::filesystem::path canonical_path, bool created) : fd_(fd), canonical_path_(std::move(canonical_path)), created_(created)
{
}

SessionLease::SessionLease(SessionLease&& other) noexcept
    : fd_(std::exchange(other.fd_, -1)), canonical_path_(std::move(other.canonical_path_)), created_(std::exchange(other.created_, false))
{
}

SessionLease& SessionLease::operator=(SessionLease&& other) noexcept
{
  if (this != &other)
  {
    if (fd_ >= 0)
      ::close(fd_);
    fd_ = std::exchange(other.fd_, -1);
    canonical_path_ = std::move(other.canonical_path_);
    created_ = std::exchange(other.created_, false);
  }
  return *this;
}

SessionLease::~SessionLease()
{
  if (fd_ >= 0)
    ::close(fd_);
}

ava::core::Result<SessionLease> SessionLease::create_and_acquire(std::filesystem::path const& session_path)
{
  std::error_code absolute_error;
  auto diagnostic_path = std::filesystem::absolute(session_path, absolute_error).lexically_normal();
  if (absolute_error)
    diagnostic_path = session_path.lexically_normal();

  auto const parent_path = session_path.parent_path();
  auto const final_name = session_path.filename().string();
  if (final_name.empty())
  {
    auto error = ava::core::Error(ava::core::ErrorCategory::InvalidArgument, "new session lease path must name a file");
    error.with_context("path", diagnostic_path.string());
    return std::unexpected(std::move(error));
  }
  std::error_code mkdir_error;
  std::filesystem::create_directories(parent_path, mkdir_error);
  if (mkdir_error)
  {
    auto error = ava::core::Error(ava::core::ErrorCategory::Io, "failed to create session lease directory");
    error.with_context("path", parent_path.string()).with_context("cause", mkdir_error.message());
    return std::unexpected(std::move(error));
  }
  std::error_code permissions_error;
  std::filesystem::permissions(parent_path, std::filesystem::perms::owner_all, std::filesystem::perm_options::replace, permissions_error);
  if (permissions_error)
  {
    auto error = ava::core::Error(ava::core::ErrorCategory::Io, "failed to set session lease directory permissions");
    error.with_context("path", parent_path.string()).with_context("cause", permissions_error.message());
    return std::unexpected(std::move(error));
  }

  int parent_fd = ::open(parent_path.c_str(), O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW);
  if (parent_fd < 0)
    return std::unexpected(path_io_error("failed to open new session lease directory", parent_path, errno));

  static std::atomic<std::uint64_t> temporary_sequence = 0;
  std::string temporary_name;
  int fd = -1;
  for (std::size_t attempt = 0; attempt < 32; ++attempt)
  {
    temporary_name = ".ava-session-lease-" + std::to_string(static_cast<long long>(::getpid())) + "-" +
                     std::to_string(temporary_sequence.fetch_add(1, std::memory_order_relaxed) + 1) + ".tmp";
    fd = ::openat(parent_fd, temporary_name.c_str(), O_RDWR | O_CREAT | O_EXCL | O_CLOEXEC | O_NOFOLLOW, 0600);
    if (fd >= 0 || errno != EEXIST)
      break;
  }
  if (fd < 0)
  {
    int const error_number = errno;
    ::close(parent_fd);
    auto const category = error_number == ELOOP ? ava::core::ErrorCategory::PermissionDenied : ava::core::ErrorCategory::Io;
    auto error = ava::core::Error(
        category, error_number == ELOOP ? "temporary session lease path must not be a symlink" : "failed to create private new session lease inode");
    error.with_context("path", diagnostic_path.string()).with_context("directory", parent_path.string()).with_context("cause", std::strerror(error_number));
    return std::unexpected(std::move(error));
  }

  SessionLease lease(fd, diagnostic_path, true);
  bool temporary_link_exists = true;
  bool final_link_exists = false;
  struct stat status{};
  bool status_valid = false;
  auto cleanup_owned_name = [&](std::string const& name, std::string_view context_key, ava::core::Error& error) {
    struct stat named_status{};
    struct stat current_status{};
    if ((!status_valid && fstat(fd, &current_status) != 0) || (fstatat(parent_fd, name.c_str(), &named_status, AT_SYMLINK_NOFOLLOW) != 0 && errno != ENOENT))
    {
      error.with_context(std::string(context_key), std::strerror(errno));
      return;
    }
    auto const& expected_status = status_valid ? status : current_status;
    if (same_file_identity(expected_status, named_status) && ::unlinkat(parent_fd, name.c_str(), 0) != 0)
      error.with_context(std::string(context_key), std::strerror(errno));
  };
  auto fail_created_inode = [&](ava::core::Error error) -> ava::core::Result<SessionLease> {
    if (final_link_exists)
      cleanup_owned_name(final_name, "final_cleanup_cause", error);
    if (temporary_link_exists)
      cleanup_owned_name(temporary_name, "temporary_cleanup_cause", error);
    ::close(parent_fd);
    return std::unexpected(std::move(error));
  };

  if (fstat(fd, &status) != 0)
    return fail_created_inode(path_io_error("failed to inspect private new session lease inode", diagnostic_path, errno));
  status_valid = true;
  if (!S_ISREG(status.st_mode))
    return fail_created_inode(path_io_error("private new session lease inode is not a regular file", diagnostic_path));
  if (status.st_nlink != 1)
  {
    auto error = ava::core::Error(ava::core::ErrorCategory::PermissionDenied, "private new session lease inode must have exactly one link");
    error.with_context("path", diagnostic_path.string()).with_context("link_count", std::to_string(status.st_nlink));
    return fail_created_inode(std::move(error));
  }
  if (fchmod(fd, 0600) != 0)
    return fail_created_inode(path_io_error("failed to set private new session lease permissions", diagnostic_path, errno));
  if (flock(fd, LOCK_EX | LOCK_NB) != 0)
    return fail_created_inode(path_io_error("failed to acquire private new session lease", diagnostic_path, errno));

  if (::linkat(parent_fd, temporary_name.c_str(), parent_fd, final_name.c_str(), 0) != 0)
  {
    int const error_number = errno;
    auto const category = error_number == ELOOP ? ava::core::ErrorCategory::PermissionDenied : ava::core::ErrorCategory::Io;
    auto error = ava::core::Error(
        category, error_number == ELOOP ? "new session lease path must not be a symlink" : "failed to publish locked new session lease without replacement");
    error.with_context("path", diagnostic_path.string()).with_context("cause", std::strerror(error_number));
    return fail_created_inode(std::move(error));
  }
  final_link_exists = true;

  if (fstat(fd, &status) != 0)
    return fail_created_inode(path_io_error("failed to inspect published new session lease", diagnostic_path, errno));
  if (status.st_nlink != 2)
  {
    auto error = ava::core::Error(ava::core::ErrorCategory::PermissionDenied, "published new session lease must have exactly two links before commit");
    error.with_context("path", diagnostic_path.string()).with_context("link_count", std::to_string(status.st_nlink));
    return fail_created_inode(std::move(error));
  }
  if (::unlinkat(parent_fd, temporary_name.c_str(), 0) != 0)
    return fail_created_inode(path_io_error("failed to remove private new session lease name after publication", diagnostic_path, errno));
  temporary_link_exists = false;

  if (fstat(fd, &status) != 0)
    return fail_created_inode(path_io_error("failed to inspect committed new session lease", diagnostic_path, errno));
  if (!S_ISREG(status.st_mode) || status.st_nlink != 1 || (status.st_mode & 0777) != 0600)
  {
    auto error = ava::core::Error(ava::core::ErrorCategory::PermissionDenied, "committed new session lease has invalid ownership invariants");
    error.with_context("path", diagnostic_path.string())
        .with_context("link_count", std::to_string(status.st_nlink))
        .with_context("mode", std::to_string(status.st_mode & 0777));
    return fail_created_inode(std::move(error));
  }
  struct stat final_status{};
  if (fstatat(parent_fd, final_name.c_str(), &final_status, AT_SYMLINK_NOFOLLOW) != 0)
    return fail_created_inode(path_io_error("failed to verify committed new session lease name", diagnostic_path, errno));
  if (!same_file_identity(status, final_status) || final_status.st_nlink != 1)
  {
    auto error = ava::core::Error(ava::core::ErrorCategory::PermissionDenied, "committed new session name does not identify the locked lease inode");
    error.with_context("path", diagnostic_path.string()).with_context("link_count", std::to_string(final_status.st_nlink));
    return fail_created_inode(std::move(error));
  }
  ::close(parent_fd);
  return lease;
}

ava::core::Result<SessionLease> SessionLease::acquire(std::filesystem::path const& session_path)
{
  std::error_code absolute_error;
  auto diagnostic_path = std::filesystem::absolute(session_path, absolute_error).lexically_normal();
  if (absolute_error)
    diagnostic_path = session_path.lexically_normal();

  // Classify the final component without a potentially blocking data open.
  // A hostile FIFO replacement must fail as metadata, never stall lease
  // acquisition before the regular-file check.
  int metadata_fd = ::open(session_path.c_str(), O_PATH | O_CLOEXEC | O_NOFOLLOW);
  if (metadata_fd < 0)
  {
    auto const category = errno == ELOOP ? ava::core::ErrorCategory::PermissionDenied : ava::core::ErrorCategory::Io;
    auto error = ava::core::Error(category, errno == ELOOP ? "session lease path must not be a symlink" : "failed to inspect session lease");
    error.with_context("path", diagnostic_path.string()).with_context("cause", std::strerror(errno));
    return std::unexpected(std::move(error));
  }
  struct stat metadata_status{};
  int const metadata_error = fstat(metadata_fd, &metadata_status) == 0 ? 0 : errno;
  int const metadata_close_error = ::close(metadata_fd) == 0 ? 0 : errno;
  if (metadata_error != 0)
    return std::unexpected(path_io_error("failed to inspect session lease metadata", diagnostic_path, metadata_error));
  if (metadata_close_error != 0)
    return std::unexpected(path_io_error("failed to close session lease metadata descriptor", diagnostic_path, metadata_close_error));
  if (!S_ISREG(metadata_status.st_mode))
  {
    if (S_ISLNK(metadata_status.st_mode))
    {
      auto error = ava::core::Error(ava::core::ErrorCategory::PermissionDenied, "session lease path must not be a symlink");
      error.with_context("path", diagnostic_path.string());
      return std::unexpected(std::move(error));
    }
    return std::unexpected(path_io_error("session lease target is not a regular file", diagnostic_path));
  }

  int const fd = ::open(session_path.c_str(), O_RDWR | O_NONBLOCK | O_CLOEXEC | O_NOFOLLOW);
  if (fd < 0)
  {
    auto const category = errno == ELOOP ? ava::core::ErrorCategory::PermissionDenied : ava::core::ErrorCategory::Io;
    auto error = ava::core::Error(category, errno == ELOOP ? "session lease path must not be a symlink" : "failed to open session lease");
    error.with_context("path", diagnostic_path.string()).with_context("cause", std::strerror(errno));
    return std::unexpected(std::move(error));
  }
  SessionLease lease(fd, diagnostic_path, false);
  struct stat status{};
  if (fstat(fd, &status) != 0)
    return std::unexpected(path_io_error("failed to inspect session lease target", diagnostic_path, errno));
  if (!same_file_identity(metadata_status, status))
    return std::unexpected(path_io_error("session lease target changed while opening", diagnostic_path));
  if (status.st_nlink != 1)
  {
    auto error = ava::core::Error(ava::core::ErrorCategory::PermissionDenied, "session lease target must have exactly one link");
    error.with_context("path", diagnostic_path.string()).with_context("link_count", std::to_string(status.st_nlink));
    return std::unexpected(std::move(error));
  }
  if (flock(fd, LOCK_EX | LOCK_NB) != 0)
  {
    int const error_number = errno;
    auto error = ava::core::Error(
        error_number == EWOULDBLOCK || error_number == EAGAIN ? ava::core::ErrorCategory::Session : ava::core::ErrorCategory::Io,
        error_number == EWOULDBLOCK || error_number == EAGAIN ? "session is already owned by another AVA host" : "failed to acquire session lease");
    error.with_context("path", diagnostic_path.string()).with_context("cause", std::strerror(error_number));
    return std::unexpected(std::move(error));
  }
  return lease;
}

bool SessionLease::active() const noexcept
{
  return fd_ >= 0;
}

ava::core::Result<SessionLease> SessionLease::duplicate() const
{
  if (fd_ < 0)
    return std::unexpected(ava::core::Error(ava::core::ErrorCategory::InvalidArgument, "cannot duplicate an inactive session lease"));
  int const duplicate_fd = fcntl(fd_, F_DUPFD_CLOEXEC, 3);
  if (duplicate_fd < 0)
    return std::unexpected(path_io_error("failed to duplicate exact session lease with CLOEXEC", canonical_path_, errno));
  return SessionLease(duplicate_fd, canonical_path_, created_);
}

std::filesystem::path const& SessionLease::canonical_path() const noexcept
{
  return canonical_path_;
}

off_t SessionLease::offset_for_test() const noexcept
{
  if (fd_ < 0)
    return -1;
  return ::lseek(fd_, 0, SEEK_CUR);
}

struct SessionReadAuthority::State
{
  State(SessionStore store_in, std::optional<SessionLease> lease_in, SessionReadLimits limits_in)
      : store(std::move(store_in)), lease(std::move(lease_in)), limits(limits_in)
  {
  }

  SessionStore store;
  std::optional<SessionLease> lease;
  SessionReadLimits limits;
};

SessionReadAuthority::SessionReadAuthority(std::shared_ptr<State const> state) : state_(std::move(state))
{
}

ava::core::Result<SessionReadAuthority> SessionReadAuthority::create_persistent(SessionStore const& store, SessionLease const& lease, SessionReadLimits limits)
{
  if (store.is_ephemeral())
    return std::unexpected(ava::core::Error(ava::core::ErrorCategory::InvalidArgument, "persistent read authority requires a persistent store"));
  if (!lease.active())
    return std::unexpected(ava::core::Error(ava::core::ErrorCategory::InvalidArgument, "persistent read authority requires an active session lease"));

  try
  {
    // Complete every potentially throwing metadata copy before duplicating the
    // locked open-file description. The local SessionLease adopts the duplicate
    // immediately, so all later allocation failures close it through RAII.
    SessionStore authority_store = store;
    std::filesystem::path authority_path = normalized_absolute_path(store.session_path());
    std::filesystem::path authority_lease_path = lease.canonical_path_;
    bool const authority_created = lease.created_;
    bool const fail_allocation_for_test = store.fail_persistent_read_authority_allocation_for_test_;

    if (authority_path != authority_lease_path)
    {
      auto error = append_authority_error("persistent read authority lease does not exactly match the store", authority_path);
      error.with_context("lease_path", authority_lease_path.string());
      return std::unexpected(std::move(error));
    }
    auto const parent_path = authority_path.parent_path();
    auto const name = authority_path.filename().string();
    if (name.empty())
      return std::unexpected(path_io_error("persistent read authority has no basename", authority_path));

    ScopedFd parent_fd(::open(parent_path.c_str(), O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC));
    if (parent_fd.get() < 0)
      return std::unexpected(path_io_error("failed to anchor persistent read authority directory", parent_path, errno));
    ScopedFd name_fd(::openat(parent_fd.get(), name.c_str(), O_PATH | O_NOFOLLOW | O_CLOEXEC));
    if (name_fd.get() < 0)
      return std::unexpected(path_io_error("failed to inspect persistent read authority target", authority_path, errno));

    struct stat lease_status{};
    struct stat name_status{};
    struct stat named_status{};
    int const lease_error = fstat(lease.fd_, &lease_status) == 0 ? 0 : errno;
    int const name_error = fstat(name_fd.get(), &name_status) == 0 ? 0 : errno;
    int const named_error = fstatat(parent_fd.get(), name.c_str(), &named_status, AT_SYMLINK_NOFOLLOW) == 0 ? 0 : errno;
    int const name_close_error = name_fd.close_checked();
    int const parent_close_error = parent_fd.close_checked();
    if (lease_error != 0)
      return std::unexpected(path_io_error("failed to inspect persistent read authority lease", authority_path, lease_error));
    if (name_error != 0)
      return std::unexpected(path_io_error("failed to inspect persistent read authority name", authority_path, name_error));
    if (named_error != 0)
      return std::unexpected(path_io_error("failed to inspect persistent read authority publication", authority_path, named_error));
    if (name_close_error != 0)
      return std::unexpected(path_io_error("failed to close persistent read authority name descriptor", authority_path, name_close_error));
    if (parent_close_error != 0)
      return std::unexpected(path_io_error("failed to close persistent read authority directory", parent_path, parent_close_error));
    if (!same_file_identity(lease_status, name_status) || !same_file_identity(lease_status, named_status) || lease_status.st_nlink != 1 ||
        name_status.st_nlink != 1 || named_status.st_nlink != 1)
    {
      auto error = ava::core::Error(ava::core::ErrorCategory::Session, "persistent read authority does not identify one regular leased inode");
      error.with_context("path", authority_path.string());
      return std::unexpected(std::move(error));
    }

    int const duplicate_fd = fcntl(lease.fd_, F_DUPFD_CLOEXEC, 3);
    if (duplicate_fd < 0)
      return std::unexpected(path_io_error("failed to duplicate persistent read lease with CLOEXEC", authority_path, errno));
    SessionLease duplicated_lease(duplicate_fd, std::move(authority_lease_path), authority_created);
    if (fail_allocation_for_test)
      throw std::bad_alloc();
    auto state = std::make_shared<State>(std::move(authority_store), std::optional<SessionLease>(std::move(duplicated_lease)), limits);
    return SessionReadAuthority(std::move(state));
  }
  catch (...)
  {
    return std::unexpected(ava::core::Error(ava::core::ErrorCategory::Unknown, "failed to allocate persistent session read authority"));
  }
}

ava::core::Result<SessionReadAuthority> SessionReadAuthority::create_ephemeral(SessionStore const& store, SessionReadLimits limits)
{
  if (!store.is_ephemeral())
    return std::unexpected(ava::core::Error(ava::core::ErrorCategory::InvalidArgument, "ephemeral read authority requires an ephemeral store"));
  try
  {
    auto state = std::make_shared<State>(store, std::nullopt, limits);
    return SessionReadAuthority(std::move(state));
  }
  catch (...)
  {
    return std::unexpected(ava::core::Error(ava::core::ErrorCategory::Unknown, "failed to allocate ephemeral session read authority"));
  }
}

ava::core::Result<std::vector<SessionEntry>> SessionReadAuthority::load() const
{
  if (!state_)
    return std::unexpected(ava::core::Error(ava::core::ErrorCategory::InvalidArgument, "session read authority is inactive"));
  return state_->lease ? state_->store.load_bounded(*state_->lease, state_->limits) : state_->store.load_bounded(state_->limits);
}

ava::core::Result<std::vector<SessionEntry>> SessionReadAuthority::load_bounded(SessionReadLimits limits, SessionCancelCallback cancel_requested) const
{
  if (!state_)
    return std::unexpected(ava::core::Error(ava::core::ErrorCategory::InvalidArgument, "session read authority is inactive"));
  return state_->lease ? state_->store.load_bounded(*state_->lease, limits, std::move(cancel_requested))
                       : state_->store.load_bounded(limits, std::move(cancel_requested));
}

ava::core::Result<SessionSummary> SessionReadAuthority::inspect_bounded(SessionReadLimits limits, SessionCancelCallback cancel_requested) const
{
  if (!state_)
    return std::unexpected(ava::core::Error(ava::core::ErrorCategory::InvalidArgument, "session read authority is inactive"));
  return state_->lease ? state_->store.inspect_bounded(*state_->lease, limits, std::move(cancel_requested))
                       : state_->store.inspect_bounded(limits, std::move(cancel_requested));
}

SessionAppendTarget::SessionAppendTarget(SessionStore store, std::optional<SessionLease> lease, AssistantOutputAppendState assistant_output_state,
                                         SessionReadLimits read_limits)
    : store_(std::move(store)), lease_(std::move(lease)), read_limits_(read_limits), assistant_output_state_(std::move(assistant_output_state))
{
}

ava::core::Result<std::shared_ptr<SessionAppendTarget>> SessionAppendTarget::create_persistent(SessionStore const& store, SessionLease const& lease,
                                                                                               SessionReadLimits read_limits)
{
  if (store.is_ephemeral())
    return std::unexpected(ava::core::Error(ava::core::ErrorCategory::InvalidArgument, "persistent append target requires a persistent store"));
  if (!lease.active())
    return std::unexpected(ava::core::Error(ava::core::ErrorCategory::InvalidArgument, "persistent append target requires an active session lease"));
  auto const path = normalized_absolute_path(store.session_path());
  if (path != lease.canonical_path())
  {
    auto error = append_authority_error("persistent append target lease does not exactly match the store", path);
    error.with_context("lease_path", lease.canonical_path().string());
    return std::unexpected(std::move(error));
  }
  auto const parent_path = path.parent_path();
  auto const name = path.filename().string();
  if (name.empty())
    return std::unexpected(path_io_error("persistent append target has no basename", path));
  ScopedFd parent_fd(::open(parent_path.c_str(), O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC));
  if (parent_fd.get() < 0)
    return std::unexpected(path_io_error("failed to anchor persistent append target directory", parent_path, errno));
  ScopedFd name_fd(::openat(parent_fd.get(), name.c_str(), O_PATH | O_NOFOLLOW | O_CLOEXEC));
  if (name_fd.get() < 0)
    return std::unexpected(path_io_error("failed to inspect persistent append target", path, errno));
  struct stat lease_status{};
  struct stat name_status{};
  struct stat named_status{};
  int const lease_error = fstat(lease.fd_, &lease_status) == 0 ? 0 : errno;
  int const name_error = fstat(name_fd.get(), &name_status) == 0 ? 0 : errno;
  int const named_error = fstatat(parent_fd.get(), name.c_str(), &named_status, AT_SYMLINK_NOFOLLOW) == 0 ? 0 : errno;
  int const name_close_error = name_fd.close_checked();
  int const parent_close_error = parent_fd.close_checked();
  if (lease_error != 0)
    return std::unexpected(path_io_error("failed to inspect persistent append target lease", path, lease_error));
  if (name_error != 0)
    return std::unexpected(path_io_error("failed to inspect persistent append target name", path, name_error));
  if (named_error != 0)
    return std::unexpected(path_io_error("failed to inspect persistent append target publication", path, named_error));
  if (name_close_error != 0)
    return std::unexpected(path_io_error("failed to close persistent append target name descriptor", path, name_close_error));
  if (parent_close_error != 0)
    return std::unexpected(path_io_error("failed to close persistent append target directory", parent_path, parent_close_error));
  if (!same_file_identity(lease_status, name_status) || !same_file_identity(lease_status, named_status) || lease_status.st_nlink != 1 ||
      name_status.st_nlink != 1 || named_status.st_nlink != 1)
  {
    auto error = ava::core::Error(ava::core::ErrorCategory::Session, "persistent append target does not identify one regular leased inode");
    error.with_context("path", path.string());
    return std::unexpected(std::move(error));
  }
  auto history = store.load(lease);
  if (!history)
    return std::unexpected(std::move(history.error()));
  auto assistant_output_state = AssistantOutputAppendState::from_validated_history(*history);
  if (!assistant_output_state)
    return std::unexpected(std::move(assistant_output_state.error()));

  struct PersistentTarget final : SessionAppendTarget
  {
    PersistentTarget(SessionStore store_in, SessionLease lease_in, AssistantOutputAppendState assistant_output_state_in, SessionReadLimits read_limits_in)
        : SessionAppendTarget(std::move(store_in), std::optional<SessionLease>(std::move(lease_in)), std::move(assistant_output_state_in), read_limits_in)
    {
    }
  };
  try
  {
    // Finish every potentially throwing copy before duplicating the locked open
    // file description. Once duplicated, local SessionLease RAII owns it across
    // all allocation and move failures.
    SessionStore target_store = store;
    std::filesystem::path target_canonical_path = lease.canonical_path_;
    bool const target_created = lease.created_;
    bool const fail_allocation_for_test = store.fail_persistent_append_target_allocation_for_test_;

    int const duplicate_fd = fcntl(lease.fd_, F_DUPFD_CLOEXEC, 3);
    if (duplicate_fd < 0)
      return std::unexpected(path_io_error("failed to duplicate persistent append lease with CLOEXEC", path, errno));
    SessionLease duplicated_lease(duplicate_fd, std::move(target_canonical_path), target_created);
    if (fail_allocation_for_test)
      throw std::bad_alloc();
    return std::make_shared<PersistentTarget>(std::move(target_store), std::move(duplicated_lease), std::move(*assistant_output_state), read_limits);
  }
  catch (...)
  {
    return std::unexpected(ava::core::Error(ava::core::ErrorCategory::Unknown, "failed to allocate persistent session append target"));
  }
}

ava::core::Result<std::shared_ptr<SessionAppendTarget>> SessionAppendTarget::create_ephemeral(SessionStore const& store, SessionReadLimits read_limits)
{
  if (!store.is_ephemeral())
    return std::unexpected(ava::core::Error(ava::core::ErrorCategory::InvalidArgument, "ephemeral append target requires an ephemeral store"));
  auto history = store.load();
  if (!history)
    return std::unexpected(std::move(history.error()));
  auto assistant_output_state = AssistantOutputAppendState::from_validated_history(*history);
  if (!assistant_output_state)
    return std::unexpected(std::move(assistant_output_state.error()));
  struct EphemeralTarget final : SessionAppendTarget
  {
    EphemeralTarget(SessionStore store_in, AssistantOutputAppendState assistant_output_state_in, SessionReadLimits read_limits_in)
        : SessionAppendTarget(std::move(store_in), std::nullopt, std::move(assistant_output_state_in), read_limits_in)
    {
    }
  };
  try
  {
    return std::make_shared<EphemeralTarget>(store, std::move(*assistant_output_state), read_limits);
  }
  catch (...)
  {
    return std::unexpected(ava::core::Error(ava::core::ErrorCategory::Unknown, "failed to allocate ephemeral session append target"));
  }
}

static_assert(std::is_nothrow_move_assignable_v<AssistantOutputAppendState>);

ava::core::VoidResult SessionAppendTarget::append(SessionEntry const& entry)
{
  std::lock_guard lock(mutex_);
  if (recovery_required_)
    return std::unexpected(append_target_recovery_required_error(store_.session_path()));

  std::optional<std::unique_lock<std::mutex>> serialization_lock;
  if (lease_)
    serialization_lock.emplace(append_mutex_for_path(lease_->canonical_path()));
  else
    serialization_lock.emplace(store_.ephemeral_state_->mutation_mutex);

  auto history = lease_ ? store_.load(*lease_) : store_.load();
  if (!history)
    return std::unexpected(std::move(history.error()));
  auto current_state = AssistantOutputAppendState::from_validated_history(*history);
  if (!current_state)
    return std::unexpected(std::move(current_state.error()));

  AssistantOutputAppendState next_state;
  try
  {
    next_state = *current_state;
    if (auto preflight = next_state.apply_candidate(entry); !preflight)
      return std::unexpected(std::move(preflight.error()));
  }
  catch (...)
  {
    return std::unexpected(ava::core::Error(ava::core::ErrorCategory::Unknown, "failed to preflight assistant-output append state"));
  }

  auto appended = store_.append_impl(lease_ ? &*lease_ : nullptr, entry, true);
  if (!appended && lease_ && !has_append_commit_state(appended.error()))
    appended = std::unexpected(with_append_commit_state(std::move(appended.error()), AppendCommitState::NotStarted, store_.session_path()));
  if (!appended)
  {
    auto error = std::move(appended.error());
    if (append_partial_or_unknown(error))
      recovery_required_ = true;
    else if (append_committed_to_leased_inode(error))
      assistant_output_state_ = std::move(next_state);
    return std::unexpected(std::move(error));
  }
  assistant_output_state_ = std::move(next_state);
  return {};
}

ava::core::VoidResult SessionAppendTarget::append_batch(std::vector<SessionEntry> entries)
{
  std::lock_guard lock(mutex_);
  if (recovery_required_)
    return std::unexpected(append_target_recovery_required_error(store_.session_path()));
  if (entries.empty())
    return std::unexpected(ava::core::Error(ava::core::ErrorCategory::InvalidArgument, "session append batch must not be empty"));
  if (entries.size() > kMaxSessionAppendBatchEntries)
  {
    auto error = ava::core::Error(ava::core::ErrorCategory::InvalidArgument, "session append batch exceeds record limit");
    error.with_context("max_records", std::to_string(kMaxSessionAppendBatchEntries));
    return std::unexpected(std::move(error));
  }
  if (entries.back().type != EntryType::AssistantTurnCommit)
  {
    return std::unexpected(ava::core::Error(ava::core::ErrorCategory::InvalidArgument, "session append batch must end with one assistant_turn_commit"));
  }
  for (std::size_t index = 0; index + 1 < entries.size(); ++index)
  {
    if (entries[index].type != EntryType::AssistantOutputItem)
    {
      return std::unexpected(ava::core::Error(ava::core::ErrorCategory::InvalidArgument,
                                              "session append batch may contain only assistant_output_item records before its final commit"));
    }
  }

  std::size_t serialized_bytes = 0;
  for (auto const& entry : entries)
  {
    auto serialized = serialize_session_entry_line(entry);
    if (!serialized)
      return std::unexpected(std::move(serialized.error()));
    if (auto strict_entry = parse_strict_session_record(*serialized, store_.session_path(), false); !strict_entry)
      return std::unexpected(std::move(strict_entry.error()));
    if (serialized->size() >= kMaxSessionAppendBatchBytes || serialized_bytes >= kMaxSessionAppendBatchBytes ||
        serialized->size() > kMaxSessionAppendBatchBytes - serialized_bytes - 1)
    {
      auto error = ava::core::Error(ava::core::ErrorCategory::InvalidArgument, "session append batch exceeds serialized byte limit");
      error.with_context("max_bytes", std::to_string(kMaxSessionAppendBatchBytes));
      return std::unexpected(std::move(error));
    }
    serialized_bytes += serialized->size() + 1;
  }

  // The same persistent-path or shared ephemeral mutation lock spans the
  // authoritative history snapshot, v4 preflight, and every write. A second
  // target constructed from an old snapshot therefore cannot commit stale
  // state after this target advances the session.
  std::optional<std::unique_lock<std::mutex>> serialization_lock;
  if (lease_)
    serialization_lock.emplace(append_mutex_for_path(lease_->canonical_path()));
  else
    serialization_lock.emplace(store_.ephemeral_state_->mutation_mutex);
  auto history = lease_ ? store_.load(*lease_) : store_.load();
  if (!history)
    return std::unexpected(std::move(history.error()));
  auto current_state = AssistantOutputAppendState::from_validated_history(*history);
  if (!current_state)
    return std::unexpected(std::move(current_state.error()));
  if (!current_state->ready())
  {
    return std::unexpected(
        ava::core::Error(ava::core::ErrorCategory::Session, "session append batch requires a ready assistant-output state with no staged transaction"));
  }

  AssistantOutputAppendState final_state;
  try
  {
    final_state = *current_state;
    for (auto const& entry : entries)
    {
      if (auto preflight = final_state.apply_candidate(entry); !preflight)
        return std::unexpected(std::move(preflight.error()));
    }
  }
  catch (...)
  {
    return std::unexpected(ava::core::Error(ava::core::ErrorCategory::Unknown, "failed to preflight assistant-output append batch state"));
  }
  if (!final_state.ready())
  {
    return std::unexpected(
        ava::core::Error(ava::core::ErrorCategory::Session, "session append batch must complete its assistant-output transaction before writing"));
  }

  std::size_t persisted_entries = 0;
  for (auto const& entry : entries)
  {
    auto appended = store_.append_impl(lease_ ? &*lease_ : nullptr, entry, true);
    if (!appended && lease_ && !has_append_commit_state(appended.error()))
      appended = std::unexpected(with_append_commit_state(std::move(appended.error()), AppendCommitState::NotStarted, store_.session_path()));
    if (!appended)
    {
      auto error = std::move(appended.error());
      bool const partial_or_unknown = append_partial_or_unknown(error);
      if (append_committed_to_leased_inode(error))
        ++persisted_entries;
      if (partial_or_unknown || persisted_entries != 0)
      {
        recovery_required_ = true;
        error = batch_partial_failure(error, store_.session_path(), persisted_entries);
      }
      return std::unexpected(std::move(error));
    }
    ++persisted_entries;
  }
  assistant_output_state_ = std::move(final_state);
  return {};
}

ava::core::VoidResult SessionAppendTarget::recover()
{
  std::lock_guard lock(mutex_);
  std::optional<std::unique_lock<std::mutex>> ephemeral_mutation_lock;
  if (!lease_)
    ephemeral_mutation_lock.emplace(store_.ephemeral_state_->mutation_mutex);

  if (lease_)
  {
    auto recovered = store_.recover_torn_tail(*lease_, read_limits_);
    if (!recovered)
      return std::unexpected(std::move(recovered.error()));
    auto staged_recovery = store_.recover_incomplete_assistant_output_suffix(*lease_, read_limits_);
    if (!staged_recovery)
      return std::unexpected(std::move(staged_recovery.error()));
  }
  else
  {
    auto staged_recovery = store_.recover_incomplete_assistant_output_suffix_ephemeral_impl(read_limits_, nullptr, true);
    if (!staged_recovery)
      return std::unexpected(std::move(staged_recovery.error()));
  }
  auto history = lease_ ? store_.load(*lease_) : store_.load();
  if (!history)
    return std::unexpected(std::move(history.error()));
  auto rebuilt_state = AssistantOutputAppendState::from_validated_history(*history);
  if (!rebuilt_state)
    return std::unexpected(std::move(rebuilt_state.error()));
  assistant_output_state_ = std::move(*rebuilt_state);
  recovery_required_ = false;
  return {};
}

ava::core::Result<SessionReadAuthority> SessionAppendTarget::read_authority() const
{
  return lease_ ? SessionReadAuthority::create_persistent(store_, *lease_, read_limits_) : SessionReadAuthority::create_ephemeral(store_, read_limits_);
}

bool SessionAppendTarget::is_ephemeral() const noexcept
{
  return !lease_.has_value();
}

}  // namespace ava::session
