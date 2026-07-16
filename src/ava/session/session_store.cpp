#include "sys.h"
#include "ava/config/xdg_paths.h"
#include "ava/session/EphemeralState.h"
#include "ava/session/record.h"
#include "ava/session/session_store.h"
#include "ava/core/ids.h"
#include "ava/core/json.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cerrno>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iomanip>
#include <limits>
#include <map>
#include <mutex>
#include <new>
#include <optional>
#include <sstream>
#include <string_view>
#include <utility>
#include <fcntl.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <unistd.h>
#include "debug.h"

namespace ava::session {

struct SessionStore::ObservationAttachment
{
  mutable std::mutex mutex;
  std::shared_ptr<ava::observability::RunObservation> observation;
  std::shared_ptr<ava::observability::TraceContext const> context;
  std::uint64_t generation = 0;
  std::atomic_bool enabled = false;
  std::atomic_bool fail_next_attachment_for_test = false;
};

namespace {

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

ava::core::Error path_io_error(std::string message, std::filesystem::path const& path, int error_number = 0)
{
  auto error = ava::core::Error(ava::core::ErrorCategory::Io, std::move(message));
  error.with_context("path", path.string());
  if (error_number != 0)
    error.with_context("cause", std::strerror(error_number));
  return error;
}

ava::core::Result<std::pair<int, std::size_t>> open_regular_snapshot(std::filesystem::path const& path, std::size_t max_file_bytes)
{
  int const fd = ::open(path.c_str(), O_RDONLY | O_CLOEXEC | O_NOFOLLOW);
  if (fd < 0)
  {
    auto category =
        errno == ENOENT ? ava::core::ErrorCategory::NotFound : (errno == ELOOP ? ava::core::ErrorCategory::PermissionDenied : ava::core::ErrorCategory::Io);
    auto error = ava::core::Error(category, errno == ENOENT ? "session not found" : "failed to securely open session file");
    error.with_context("path", path.string()).with_context("cause", std::strerror(errno));
    return std::unexpected(std::move(error));
  }
  struct stat status{};
  if (fstat(fd, &status) != 0 || !S_ISREG(status.st_mode))
  {
    int const error_number = errno;
    ::close(fd);
    auto error = path_io_error("session path is not a regular file", path, error_number);
    return std::unexpected(std::move(error));
  }
  auto const size = static_cast<std::uintmax_t>(status.st_size);
  if (size > max_file_bytes)
  {
    ::close(fd);
    auto error = ava::core::Error(ava::core::ErrorCategory::Session, "session file exceeds bounded read limit");
    error.with_context("path", path.string()).with_context("max_file_bytes", std::to_string(max_file_bytes));
    return std::unexpected(std::move(error));
  }
  return std::pair<int, std::size_t>(fd, static_cast<std::size_t>(size));
}

ava::core::VoidResult validate_read_limits(SessionReadLimits const& limits)
{
  if (limits.max_file_bytes == 0 || limits.max_line_bytes == 0 || limits.max_entries == 0 || limits.max_line_bytes > limits.max_file_bytes)
    return std::unexpected(ava::core::Error(ava::core::ErrorCategory::InvalidArgument, "session bounded read limits are invalid"));
  return {};
}

class SessionTraceScope
{
 public:
  SessionTraceScope(std::shared_ptr<ava::observability::RunObservation> observation, std::shared_ptr<ava::observability::TraceContext const> context,
                    ava::observability::TraceEventType attempt, ava::observability::TraceEventType result, std::optional<EntryType> entry_type,
                    bool ephemeral) noexcept
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
  ~SessionTraceScope() noexcept
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
  void succeed(std::size_t entry_count = 0) noexcept
  {
    outcome_ = ava::observability::TraceOutcome::Success;
    entry_count_ = entry_count;
  }

 private:
  std::shared_ptr<ava::observability::RunObservation> observation_;
  std::shared_ptr<ava::observability::TraceContext const> context_;
  ava::observability::TraceEventType result_;
  std::optional<EntryType> entry_type_;
  bool ephemeral_ = false;
  std::size_t entry_count_ = 0;
  ava::observability::TraceOutcome outcome_ = ava::observability::TraceOutcome::Error;
};

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

ava::core::VoidResult SessionStore::append(SessionEntry const& entry)
{
  // The normal disabled path does not lock, copy, or allocate for observation.
  // Enabled setup is best effort and is fully isolated from the append.
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
  {
    return valid_session_id;
  }

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
  {
    return valid_parent_id;
  }

  auto line = serialize_session_entry_line(entry);
  if (!line)
    return std::unexpected(std::move(line.error()));

  if (ephemeral_state_)
  {
    std::lock_guard lock(ephemeral_state_->mutex);
    ephemeral_state_->entries.push_back(entry);
    if (trace)
      trace->succeed();
    return {};
  }

  auto const path = session_path();
  auto& append_mutex = append_mutex_for_path(path);
  std::lock_guard append_lock(append_mutex);

  std::error_code mkdir_error;
  std::filesystem::create_directories(path.parent_path(), mkdir_error);
  if (mkdir_error)
  {
    auto error = ava::core::Error(ava::core::ErrorCategory::Io, "failed to create session directory");
    error.with_context("path", path.parent_path().string());
    error.with_context("cause", mkdir_error.message());
    return std::unexpected(std::move(error));
  }

  for (auto const& directory : {options_.root_dir, path.parent_path()})
  {
    std::error_code permissions_error;
    std::filesystem::permissions(directory, std::filesystem::perms::owner_all, std::filesystem::perm_options::replace, permissions_error);
    if (permissions_error)
    {
      auto error = ava::core::Error(ava::core::ErrorCategory::Io, "failed to set session directory permissions");
      error.with_context("path", directory.string());
      error.with_context("cause", permissions_error.message());
      return std::unexpected(std::move(error));
    }
  }

  std::error_code status_error;
  auto const status = std::filesystem::symlink_status(path, status_error);
  if (!status_error && std::filesystem::exists(status))
  {
    if (std::filesystem::is_symlink(status))
    {
      auto error = ava::core::Error(ava::core::ErrorCategory::PermissionDenied, "session path must not be a symlink");
      error.with_context("session_id", session_id());
      error.with_context("path", path.string());
      return std::unexpected(std::move(error));
    }
    if (!std::filesystem::is_regular_file(status))
    {
      auto error = ava::core::Error(ava::core::ErrorCategory::Session, "session path is not a regular file");
      error.with_context("session_id", session_id());
      error.with_context("path", path.string());
      return std::unexpected(std::move(error));
    }
  }
  else if (status_error && status_error != std::errc::no_such_file_or_directory)
  {
    auto error = ava::core::Error(ava::core::ErrorCategory::Io, "failed to inspect session file");
    error.with_context("path", path.string());
    error.with_context("cause", status_error.message());
    return std::unexpected(std::move(error));
  }

  std::ofstream file(path, std::ios::app);
  if (!file)
  {
    auto error = ava::core::Error(ava::core::ErrorCategory::Io, "failed to open session file");
    error.with_context("path", path.string());
    return std::unexpected(std::move(error));
  }

  std::error_code file_permissions_error;
  std::filesystem::permissions(path, std::filesystem::perms::owner_read | std::filesystem::perms::owner_write, std::filesystem::perm_options::replace,
                               file_permissions_error);
  if (file_permissions_error)
  {
    auto error = ava::core::Error(ava::core::ErrorCategory::Io, "failed to set session file permissions");
    error.with_context("path", path.string());
    error.with_context("cause", file_permissions_error.message());
    return std::unexpected(std::move(error));
  }

  file << *line << '\n';
  file.flush();

  if (!file)
  {
    auto error = ava::core::Error(ava::core::ErrorCategory::Io, "failed to write session entry");
    error.with_context("path", path.string());
    return std::unexpected(std::move(error));
  }

  if (trace)
    trace->succeed();
  return {};
}

ava::core::Result<std::vector<SessionEntry>> SessionStore::load() const
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
        trace.emplace(std::move(observation), std::move(context), ava::observability::TraceEventType::SessionLoadAttempt,
                      ava::observability::TraceEventType::SessionLoadResult, std::nullopt, is_ephemeral());
      }
    }
    catch (...)
    {
      if (observation)
        observation->account_external_failure();
    }
  }
  if (auto valid_session_id = validate_session_id(options_.session_id); !valid_session_id)
  {
    return std::unexpected(std::move(valid_session_id.error()));
  }

  if (ephemeral_state_)
  {
    std::lock_guard lock(ephemeral_state_->mutex);
    auto entries = ephemeral_state_->entries;
    if (trace)
      trace->succeed(entries.size());
    return entries;
  }

  auto const path = session_path();
  auto& append_mutex = append_mutex_for_path(path);
  std::lock_guard append_lock(append_mutex);

  std::error_code status_error;
  auto const status = std::filesystem::symlink_status(path, status_error);
  if (status_error || !std::filesystem::exists(status))
  {
    if (trace)
      trace->succeed();
    return std::vector<SessionEntry>{};
  }
  if (std::filesystem::is_symlink(status))
  {
    auto error = ava::core::Error(ava::core::ErrorCategory::PermissionDenied, "session path must not be a symlink");
    error.with_context("session_id", session_id());
    error.with_context("path", path.string());
    return std::unexpected(std::move(error));
  }
  if (!std::filesystem::is_regular_file(status))
  {
    auto error = ava::core::Error(ava::core::ErrorCategory::Session, "session path is not a regular file");
    error.with_context("session_id", session_id());
    error.with_context("path", path.string());
    return std::unexpected(std::move(error));
  }

  std::ifstream file(path);
  if (!file)
  {
    auto error = ava::core::Error(ava::core::ErrorCategory::Io, "failed to open session file");
    error.with_context("session_id", session_id());
    error.with_context("path", path.string());
    return std::unexpected(std::move(error));
  }

  std::vector<SessionEntry> entries;
  std::string line;
  while (true)
  {
    auto line_read = read_limited_session_line(file, line);
    if (!line_read)
    {
      return std::unexpected(line_read.error());
    }
    if (!*line_read)
    {
      break;
    }
    auto entry = parse_session_entry_line(line, path);
    if (!entry)
      return std::unexpected(std::move(entry.error()));
    entries.push_back(std::move(*entry));
  }
  if (trace)
    trace->succeed(entries.size());
  return entries;
}

ava::core::VoidResult SessionStore::visit_entries(SessionReadLimits limits, SessionEntryVisitor const& visitor, SessionCancelCallback cancel_requested) const
{
  if (auto valid = validate_read_limits(limits); !valid)
    return valid;
  if (!visitor)
    return std::unexpected(ava::core::Error(ava::core::ErrorCategory::InvalidArgument, "session entry visitor is required"));
  if (auto valid_session_id = validate_session_id(options_.session_id); !valid_session_id)
    return valid_session_id;

  if (ephemeral_state_)
  {
    std::vector<SessionEntry> snapshot;
    {
      std::lock_guard lock(ephemeral_state_->mutex);
      if (ephemeral_state_->entries.size() > limits.max_entries)
        return std::unexpected(ava::core::Error(ava::core::ErrorCategory::Session, "session entry count exceeds bounded read limit"));
      snapshot = ephemeral_state_->entries;
    }
    for (auto const& entry : snapshot)
    {
      if (cancel_requested && cancel_requested())
        return std::unexpected(ava::core::Error(ava::core::ErrorCategory::Unknown, "session read canceled"));
      auto keep_going = visitor(entry);
      if (!keep_going)
        return std::unexpected(std::move(keep_going.error()));
      if (!*keep_going)
        break;
    }
    return {};
  }

  auto const path = session_path();
  // Reads are pinned to one descriptor/fstat byte snapshot. No registry or
  // append mutex is held across open/read/parse; a concurrently incomplete
  // trailing record fails this transient read rather than being accepted.
  auto opened = open_regular_snapshot(path, limits.max_file_bytes);
  if (!opened)
    return std::unexpected(std::move(opened.error()));
  int fd = opened->first;
  auto close_fd = [&] {
    if (fd >= 0)
    {
      ::close(fd);
      fd = -1;
    }
  };

  std::array<char, 8192> buffer{};
  std::string line;
  line.reserve(std::min<std::size_t>(limits.max_line_bytes, buffer.size()));
  std::size_t remaining = opened->second;
  std::size_t entry_count = 0;
  auto consume_line = [&](bool final_line) -> ava::core::Result<bool> {
    if (!final_line && !line.empty() && line.back() == '\r')
      line.pop_back();
    if (line.empty())
      return true;
    if (++entry_count > limits.max_entries)
    {
      auto error = ava::core::Error(ava::core::ErrorCategory::Session, "session entry count exceeds bounded read limit");
      error.with_context("max_entries", std::to_string(limits.max_entries));
      return std::unexpected(std::move(error));
    }
    auto entry = parse_session_entry_line(line, path);
    if (!entry)
      return std::unexpected(std::move(entry.error()));
    auto keep_going = visitor(*entry);
    if (!keep_going)
      return std::unexpected(std::move(keep_going.error()));
    line.clear();
    return *keep_going;
  };

  while (remaining > 0)
  {
    if (cancel_requested && cancel_requested())
    {
      close_fd();
      return std::unexpected(ava::core::Error(ava::core::ErrorCategory::Unknown, "session read canceled"));
    }
    auto const wanted = std::min<std::size_t>(remaining, buffer.size());
    auto const count = ::read(fd, buffer.data(), wanted);
    if (count < 0)
    {
      if (errno == EINTR)
        continue;
      auto error = path_io_error("failed while reading session file", path, errno);
      close_fd();
      return std::unexpected(std::move(error));
    }
    if (count == 0)
      break;
    remaining -= static_cast<std::size_t>(count);
    for (ssize_t index = 0; index < count; ++index)
    {
      char const ch = buffer[static_cast<std::size_t>(index)];
      if (ch == '\n')
      {
        auto keep_going = consume_line(false);
        if (!keep_going)
        {
          close_fd();
          return std::unexpected(std::move(keep_going.error()));
        }
        if (!*keep_going)
        {
          close_fd();
          return {};
        }
      }
      else
      {
        if (line.size() >= limits.max_line_bytes)
        {
          close_fd();
          auto error = ava::core::Error(ava::core::ErrorCategory::Session, "session line exceeds bounded read limit");
          error.with_context("path", path.string()).with_context("max_line_bytes", std::to_string(limits.max_line_bytes));
          return std::unexpected(std::move(error));
        }
        line.push_back(ch);
      }
    }
  }
  if (remaining != 0)
  {
    close_fd();
    return std::unexpected(path_io_error("session file changed or became unreadable during bounded read", path));
  }
  if (!line.empty())
  {
    auto keep_going = consume_line(true);
    if (!keep_going)
    {
      close_fd();
      return std::unexpected(std::move(keep_going.error()));
    }
  }
  close_fd();
  return {};
}

ava::core::Result<std::vector<SessionEntry>> SessionStore::load_bounded(SessionReadLimits limits, SessionCancelCallback cancel_requested) const
{
  std::vector<SessionEntry> entries;
  entries.reserve(std::min<std::size_t>(limits.max_entries, 256));
  auto visited = visit_entries(
      limits,
      [&](SessionEntry const& entry) -> ava::core::Result<bool> {
        entries.push_back(entry);
        return true;
      },
      std::move(cancel_requested));
  if (!visited)
    return std::unexpected(std::move(visited.error()));
  return entries;
}

ava::core::Result<SessionSummary> SessionStore::inspect_bounded(SessionReadLimits limits, SessionCancelCallback cancel_requested) const
{
  SessionSummary summary{.session_id = session_id(), .path = session_path(), .last_updated = {}, .entry_count = 0, .original_cwd = {}, .title = {}};
  auto visited = visit_entries(
      limits,
      [&](SessionEntry const& entry) -> ava::core::Result<bool> {
        ++summary.entry_count;
        summary.last_updated = entry.timestamp;
        if (entry.type == EntryType::SessionMetadata)
        {
          if (auto name = ava::core::json::string_field(entry.data_json, "name"))
            summary.title = std::move(*name);
        }
        if (entry.type == EntryType::SessionStart || entry.type == EntryType::SessionMetadata)
        {
          if (auto cwd = ava::core::json::string_field(entry.data_json, "original_cwd"); cwd && !cwd->empty())
          {
            std::filesystem::path candidate(*cwd);
            if (!candidate.is_absolute() || candidate.lexically_normal() != candidate)
              return std::unexpected(ava::core::Error(ava::core::ErrorCategory::Session, "persisted session cwd is invalid"));
            if (!summary.original_cwd.empty() && summary.original_cwd != candidate)
              return std::unexpected(ava::core::Error(ava::core::ErrorCategory::Session, "persisted session cwd changed"));
            summary.original_cwd = std::move(candidate);
          }
        }
        return true;
      },
      std::move(cancel_requested));
  if (!visited)
    return std::unexpected(std::move(visited.error()));
  return summary;
}

ava::core::Result<SessionStore> SessionStore::create(std::filesystem::path const& workspace_dir, std::filesystem::path const& root_dir)
{
  return SessionStore(SessionStoreOptions{
      .root_dir = root_dir,
      .workspace_dir = workspace_dir,
      .session_id = ava::core::make_id("session"),
  });
}

ava::core::Result<SessionStore> SessionStore::create_ephemeral(std::filesystem::path const& workspace_dir)
{
  std::error_code temp_error;
  auto temp_root = std::filesystem::temp_directory_path(temp_error);
  if (temp_error)
  {
    auto error = ava::core::Error(ava::core::ErrorCategory::Io, "failed to resolve temporary directory for ephemeral session");
    error.with_context("cause", temp_error.message());
    return std::unexpected(std::move(error));
  }

  auto scratch_root = temp_root / ("ava-" + ava::core::make_id("ephemeral-session"));
  return SessionStore(
      SessionStoreOptions{
          .root_dir = scratch_root,
          .workspace_dir = workspace_dir,
          .session_id = ava::core::make_id("session"),
      },
      std::make_shared<EphemeralState>(scratch_root));
}

ava::core::Result<SessionStore> SessionStore::open(std::filesystem::path const& workspace_dir, std::string session_id, std::filesystem::path const& root_dir)
{
  if (auto valid_session_id = validate_session_id(session_id); !valid_session_id)
  {
    return std::unexpected(std::move(valid_session_id.error()));
  }

  SessionStore store(SessionStoreOptions{
      .root_dir = root_dir,
      .workspace_dir = workspace_dir,
      .session_id = std::move(session_id),
  });
  auto opened = open_regular_snapshot(store.session_path(), std::numeric_limits<std::size_t>::max());
  if (!opened)
  {
    auto error = std::move(opened.error());
    error.with_context("session_id", store.session_id());
    return std::unexpected(std::move(error));
  }
  ::close(opened->first);
  return store;
}

ava::core::Result<std::vector<SessionSummary>> SessionStore::list_sessions(std::filesystem::path const& workspace_dir, std::filesystem::path const& root_dir)
{
  auto const directory = root_dir / project_key(workspace_dir);
  std::error_code exists_error;
  bool const directory_exists = std::filesystem::exists(directory, exists_error);
  if (exists_error)
  {
    auto error = ava::core::Error(ava::core::ErrorCategory::Io, "failed to inspect session directory");
    error.with_context("path", directory.string());
    error.with_context("cause", exists_error.message());
    return std::unexpected(std::move(error));
  }
  if (!directory_exists)
  {
    return std::vector<SessionSummary>{};
  }

  std::vector<SessionSummary> summaries;
  std::error_code iter_error;
  for (std::filesystem::directory_iterator iter(directory, iter_error), end; iter != end; iter.increment(iter_error))
  {
    if (iter_error)
    {
      auto error = ava::core::Error(ava::core::ErrorCategory::Io, "failed to list sessions");
      error.with_context("path", directory.string());
      error.with_context("cause", iter_error.message());
      return std::unexpected(std::move(error));
    }
    auto const& entry = *iter;
    std::error_code entry_error;
    auto const entry_status = entry.symlink_status(entry_error);
    if (entry_error || std::filesystem::is_symlink(entry_status) || !std::filesystem::is_regular_file(entry_status) || entry.path().extension() != ".jsonl")
    {
      continue;
    }
    auto const session_id = entry.path().stem().string();
    auto store = SessionStore::open(workspace_dir, session_id, root_dir);
    if (!store)
    {
      continue;
    }
    auto entries = store->load();
    if (!entries)
    {
      if (is_unsupported_session_version_error(entries.error()))
      {
        auto error = std::move(entries.error());
        error.with_context("session_id", session_id);
        return std::unexpected(std::move(error));
      }
      continue;
    }
    std::string last_updated;
    if (!entries->empty())
    {
      last_updated = entries->back().timestamp;
    }
    summaries.push_back(SessionSummary{.session_id = session_id,
                                       .path = entry.path(),
                                       .last_updated = std::move(last_updated),
                                       .entry_count = entries->size(),
                                       .original_cwd = {},
                                       .title = {}});
  }

  std::ranges::sort(summaries, [](SessionSummary const& left, SessionSummary const& right) {
    std::error_code left_error;
    std::error_code right_error;
    auto const left_time = std::filesystem::last_write_time(left.path, left_error);
    auto const right_time = std::filesystem::last_write_time(right.path, right_error);
    if (!left_error && !right_error && left_time != right_time)
    {
      return left_time > right_time;
    }
    return left.session_id > right.session_id;
  });

  return summaries;
}

ava::core::Result<std::vector<SessionSummary>> SessionStore::list_sessions_bounded(std::filesystem::path const& workspace_dir,
                                                                                   std::filesystem::path const& root_dir, SessionListLimits limits,
                                                                                   SessionCancelCallback cancel_requested)
{
  if (limits.max_sessions == 0 || limits.max_total_file_bytes == 0)
    return std::unexpected(ava::core::Error(ava::core::ErrorCategory::InvalidArgument, "session list limits must be non-zero"));
  if (auto valid = validate_read_limits(limits.per_session); !valid)
    return std::unexpected(std::move(valid.error()));

  auto const directory = root_dir / project_key(workspace_dir);
  std::error_code exists_error;
  if (!std::filesystem::exists(directory, exists_error))
  {
    if (!exists_error)
      return std::vector<SessionSummary>{};
    return std::unexpected(path_io_error("failed to inspect session directory", directory, exists_error.value()));
  }

  std::vector<std::pair<std::string, std::filesystem::path>> candidates;
  std::size_t total_file_bytes = 0;
  std::error_code iter_error;
  for (std::filesystem::directory_iterator iter(directory, iter_error), end; iter != end; iter.increment(iter_error))
  {
    if (cancel_requested && cancel_requested())
      return std::unexpected(ava::core::Error(ava::core::ErrorCategory::Unknown, "session list canceled"));
    if (iter_error)
      return std::unexpected(path_io_error("failed to list sessions", directory, iter_error.value()));
    auto const& entry = *iter;
    std::error_code status_error;
    auto const status = entry.symlink_status(status_error);
    if (status_error || std::filesystem::is_symlink(status) || !std::filesystem::is_regular_file(status) || entry.path().extension() != ".jsonl")
      continue;
    auto id = entry.path().stem().string();
    if (!validate_session_id(id))
      continue;
    if (candidates.size() >= limits.max_sessions)
    {
      auto error = ava::core::Error(ava::core::ErrorCategory::Session, "session count exceeds bounded list limit");
      error.with_context("max_sessions", std::to_string(limits.max_sessions));
      return std::unexpected(std::move(error));
    }
    std::error_code size_error;
    auto const size = entry.file_size(size_error);
    if (size_error || size > limits.per_session.max_file_bytes || size > limits.max_total_file_bytes - total_file_bytes)
    {
      auto error = ava::core::Error(ava::core::ErrorCategory::Session, "session files exceed bounded list byte limit");
      error.with_context("max_total_file_bytes", std::to_string(limits.max_total_file_bytes));
      return std::unexpected(std::move(error));
    }
    total_file_bytes += static_cast<std::size_t>(size);
    candidates.emplace_back(std::move(id), entry.path());
  }
  if (iter_error)
    return std::unexpected(path_io_error("failed to list sessions", directory, iter_error.value()));

  std::vector<SessionSummary> summaries;
  summaries.reserve(candidates.size());
  for (auto const& [id, path] : candidates)
  {
    (void)path;
    SessionStore store(SessionStoreOptions{.root_dir = root_dir, .workspace_dir = workspace_dir, .session_id = id});
    auto summary = store.inspect_bounded(limits.per_session, cancel_requested);
    if (!summary)
      return std::unexpected(std::move(summary.error()));
    summaries.push_back(std::move(*summary));
  }
  std::ranges::sort(summaries, [](SessionSummary const& left, SessionSummary const& right) {
    if (left.last_updated != right.last_updated)
      return left.last_updated > right.last_updated;
    return left.session_id > right.session_id;
  });
  return summaries;
}

SessionLease::SessionLease(int fd, std::filesystem::path canonical_path) : fd_(fd), canonical_path_(std::move(canonical_path))
{
}

SessionLease::SessionLease(SessionLease&& other) noexcept : fd_(std::exchange(other.fd_, -1)), canonical_path_(std::move(other.canonical_path_))
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
  }
  return *this;
}

SessionLease::~SessionLease()
{
  if (fd_ >= 0)
    ::close(fd_);
}

ava::core::Result<SessionLease> SessionLease::acquire(std::filesystem::path const& session_path)
{
  std::error_code canonical_error;
  auto canonical_path = std::filesystem::canonical(session_path, canonical_error);
  if (canonical_error)
    return std::unexpected(path_io_error("failed to resolve canonical session path for lease", session_path, canonical_error.value()));
  int const fd = ::open(canonical_path.c_str(), O_RDONLY | O_CLOEXEC | O_NOFOLLOW);
  if (fd < 0)
    return std::unexpected(path_io_error("failed to open session lease", canonical_path, errno));
  struct stat status{};
  if (fstat(fd, &status) != 0 || !S_ISREG(status.st_mode))
  {
    int const error_number = errno;
    ::close(fd);
    return std::unexpected(path_io_error("session lease target is not a regular file", canonical_path, error_number));
  }
  if (flock(fd, LOCK_EX | LOCK_NB) != 0)
  {
    int const error_number = errno;
    ::close(fd);
    auto error = ava::core::Error(
        error_number == EWOULDBLOCK || error_number == EAGAIN ? ava::core::ErrorCategory::Session : ava::core::ErrorCategory::Io,
        error_number == EWOULDBLOCK || error_number == EAGAIN ? "session is already owned by another AVA host" : "failed to acquire session lease");
    error.with_context("path", canonical_path.string()).with_context("cause", std::strerror(error_number));
    return std::unexpected(std::move(error));
  }
  return SessionLease(fd, std::move(canonical_path));
}

std::filesystem::path const& SessionLease::canonical_path() const noexcept
{
  return canonical_path_;
}

std::filesystem::path SessionStore::default_root_dir()
{
  return ava::config::xdg_paths().sessions_dir;
}

std::string to_string(EntryType type)
{
  switch (type)
  {
    case EntryType::SessionStart:
      return "session_start";
    case EntryType::SessionMetadata:
      return "session_metadata";
    case EntryType::UserMessage:
      return "user_message";
    case EntryType::AssistantMessage:
      return "assistant_message";
    case EntryType::ToolCall:
      return "tool_call";
    case EntryType::ToolResult:
      return "tool_result";
    case EntryType::PermissionDecision:
      return "permission_decision";
    case EntryType::ModeChange:
      return "mode_change";
    case EntryType::ModelChange:
      return "model_change";
    case EntryType::ReasoningBlock:
      return "reasoning_block";
    case EntryType::ReasoningChange:
      return "reasoning_change";
    case EntryType::Compaction:
      return "compaction";
    case EntryType::BranchSummary:
      return "branch_summary";
    case EntryType::Error:
      return "error";
    case EntryType::Cancel:
      return "cancel";
  }
  return "error";
}

ava::core::Result<EntryType> parse_entry_type(std::string_view value)
{
  if (value == "session_start")
    return EntryType::SessionStart;
  if (value == "session_metadata")
    return EntryType::SessionMetadata;
  if (value == "user_message")
    return EntryType::UserMessage;
  if (value == "assistant_message")
    return EntryType::AssistantMessage;
  if (value == "tool_call")
    return EntryType::ToolCall;
  if (value == "tool_result")
    return EntryType::ToolResult;
  if (value == "permission_decision")
    return EntryType::PermissionDecision;
  if (value == "mode_change")
    return EntryType::ModeChange;
  if (value == "model_change")
    return EntryType::ModelChange;
  if (value == "reasoning_block")
    return EntryType::ReasoningBlock;
  if (value == "reasoning_change")
    return EntryType::ReasoningChange;
  if (value == "compaction")
    return EntryType::Compaction;
  if (value == "branch_summary")
    return EntryType::BranchSummary;
  if (value == "error")
    return EntryType::Error;
  if (value == "cancel")
    return EntryType::Cancel;

  auto error = ava::core::Error(ava::core::ErrorCategory::Session, "unknown session entry type");
  error.with_context("type", std::string(value));
  return std::unexpected(std::move(error));
}

bool is_internal_replay_user_message(SessionEntry const& entry)
{
  if (entry.type != EntryType::UserMessage)
    return false;
  auto const start = ava::core::json::field_value_start(entry.data_json, "internal_replay");
  return start && entry.data_json.substr(*start, 4) == "true";
}

std::string now_timestamp()
{
  auto const now = std::chrono::system_clock::now();
  auto const time = std::chrono::system_clock::to_time_t(now);
  std::tm tm{};
#ifdef _WIN32
  gmtime_s(&tm, &time);
#else
  gmtime_r(&time, &tm);
#endif
  std::ostringstream out;
  out << std::put_time(&tm, "%Y-%m-%dT%H:%M:%SZ");
  return out.str();
}

}  // namespace ava::session
