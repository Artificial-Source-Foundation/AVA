#include "sys.h"
#include "ava/diagnostics/artifact_store.h"
#include "ava/core/strict_json.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cerrno>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <limits>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <fcntl.h>
#include <sys/file.h>
#include <sys/random.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <sys/types.h>
#include <unistd.h>

namespace ava::diagnostics {
namespace {

constexpr std::size_t kMaxRecordBytes = 64 * 1024;
constexpr mode_t kPrivateDirectoryMode = S_IRWXU;
constexpr mode_t kPrivateFileMode = S_IRUSR | S_IWUSR;

class ScopedFd
{
 public:
  ScopedFd() noexcept = default;
  explicit ScopedFd(int fd) noexcept : fd_(fd) { }
  ScopedFd(ScopedFd const&) = delete;
  ScopedFd& operator=(ScopedFd const&) = delete;
  ScopedFd(ScopedFd&& other) noexcept : fd_(std::exchange(other.fd_, -1)) { }
  ScopedFd& operator=(ScopedFd&& other) noexcept
  {
    if (this != &other)
    {
      reset();
      fd_ = std::exchange(other.fd_, -1);
    }
    return *this;
  }
  ~ScopedFd() { reset(); }
  [[nodiscard]] int get() const noexcept { return fd_; }
  [[nodiscard]] int release() noexcept { return std::exchange(fd_, -1); }
  [[nodiscard]] explicit operator bool() const noexcept { return fd_ >= 0; }

 private:
  void reset() noexcept
  {
    if (fd_ >= 0)
      static_cast<void>(::close(fd_));
    fd_ = -1;
  }
  int fd_ = -1;
};

enum class OpenState
{
  Ready,
  Missing,
  Unsafe,
  Unavailable,
};

struct OpenDirectory
{
  OpenState state = OpenState::Unavailable;
  ScopedFd fd = ScopedFd{};
};

struct OpenFile
{
  OpenState state = OpenState::Unavailable;
  ScopedFd fd = ScopedFd{};
};

bool private_directory_metadata(struct stat const& metadata) noexcept
{
  return S_ISDIR(metadata.st_mode) && metadata.st_uid == ::geteuid() && metadata.st_gid == ::getegid() &&
         (metadata.st_mode & static_cast<mode_t>(07777)) == kPrivateDirectoryMode;
}

bool private_file_metadata(struct stat const& metadata) noexcept
{
  return S_ISREG(metadata.st_mode) && metadata.st_uid == ::geteuid() && metadata.st_gid == ::getegid() && metadata.st_nlink == 1 &&
         (metadata.st_mode & static_cast<mode_t>(07777)) == kPrivateFileMode;
}

OpenDirectory open_private_child(int parent_fd, std::string_view name, bool create)
{
  std::string const child(name);
  int fd = ::openat(parent_fd, child.c_str(), O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NONBLOCK | O_NOFOLLOW);
  if (fd < 0 && errno == ENOENT && create)
  {
    if (::mkdirat(parent_fd, child.c_str(), kPrivateDirectoryMode) != 0 && errno != EEXIST)
      return {.state = OpenState::Unavailable};
    fd = ::openat(parent_fd, child.c_str(), O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NONBLOCK | O_NOFOLLOW);
  }
  if (fd < 0)
  {
    if (errno == ENOENT)
      return {.state = OpenState::Missing};
    if (errno == ELOOP || errno == ENOTDIR)
      return {.state = OpenState::Unsafe};
    return {.state = OpenState::Unavailable};
  }
  ScopedFd opened(fd);
  struct stat metadata{};
  if (::fstat(opened.get(), &metadata) != 0)
    return {.state = OpenState::Unavailable};
  if (!private_directory_metadata(metadata))
    return {.state = OpenState::Unsafe};
  return {.state = OpenState::Ready, .fd = std::move(opened)};
}

OpenDirectory open_ava_state(ava::config::XdgPaths const& paths, ava::core::AnchorSet const& anchors) noexcept
{
  try
  {
    auto const ava_state = paths.ava_state_dir.lexically_normal();
    auto ref = anchors.find_anchor(ava_state);
    if (!ref || !ref->relative().empty() || ref->anchor().root != ava_state)
      return {.state = OpenState::Unsafe};
    int const duplicated = ::fcntl(ref->anchor().fd, F_DUPFD_CLOEXEC, 0);
    if (duplicated < 0)
      return {.state = OpenState::Unavailable};
    ScopedFd opened(duplicated);
    struct stat metadata{};
    if (::fstat(opened.get(), &metadata) != 0)
      return {.state = OpenState::Unavailable};
    if (!private_directory_metadata(metadata))
      return {.state = OpenState::Unsafe};
    return {.state = OpenState::Ready, .fd = std::move(opened)};
  }
  catch (...)
  {
    return {.state = OpenState::Unavailable};
  }
}

OpenDirectory open_artifact_directory(ava::config::XdgPaths const& paths, ava::core::AnchorSet const& anchors, std::string_view name, bool create)
{
  auto state = open_ava_state(paths, anchors);
  if (state.state != OpenState::Ready)
    return state;
  return open_private_child(state.fd.get(), name, create);
}

StoredRecordState stored_state(OpenState state) noexcept
{
  switch (state)
  {
    case OpenState::Ready:
      return StoredRecordState::Present;
    case OpenState::Missing:
      return StoredRecordState::Absent;
    case OpenState::Unsafe:
      return StoredRecordState::Unsafe;
    case OpenState::Unavailable:
      return StoredRecordState::Unavailable;
  }
  return StoredRecordState::Unavailable;
}

ArtifactWriteStatus write_state(OpenState state) noexcept
{
  switch (state)
  {
    case OpenState::Ready:
      return ArtifactWriteStatus::Success;
    case OpenState::Unsafe:
      return ArtifactWriteStatus::UnsafeStorage;
    case OpenState::Missing:
    case OpenState::Unavailable:
      return ArtifactWriteStatus::StorageUnavailable;
  }
  return ArtifactWriteStatus::StorageUnavailable;
}

std::optional<std::string> read_private_file(int directory_fd, std::string_view name, StoredRecordState& state)
{
  std::string const filename(name);
  struct stat before{};
  if (::fstatat(directory_fd, filename.c_str(), &before, AT_SYMLINK_NOFOLLOW) != 0)
  {
    state = errno == ENOENT ? StoredRecordState::Absent : StoredRecordState::Unavailable;
    return std::nullopt;
  }
  if (!private_file_metadata(before))
  {
    state = StoredRecordState::Unsafe;
    return std::nullopt;
  }
  ScopedFd file(::openat(directory_fd, filename.c_str(), O_RDONLY | O_CLOEXEC | O_NONBLOCK | O_NOFOLLOW));
  if (!file)
  {
    state = errno == ELOOP ? StoredRecordState::Unsafe : StoredRecordState::Unavailable;
    return std::nullopt;
  }
  struct stat opened{};
  if (::fstat(file.get(), &opened) != 0)
  {
    state = StoredRecordState::Unavailable;
    return std::nullopt;
  }
  if (!private_file_metadata(opened) || opened.st_dev != before.st_dev || opened.st_ino != before.st_ino)
  {
    state = StoredRecordState::Unsafe;
    return std::nullopt;
  }
  if (opened.st_size < 0 || static_cast<std::uint64_t>(opened.st_size) > kMaxRecordBytes)
  {
    state = StoredRecordState::Malformed;
    return std::nullopt;
  }
  std::string body;
  body.reserve(static_cast<std::size_t>(opened.st_size));
  std::array<char, 4096> buffer{};
  while (true)
  {
    auto const bytes = ::read(file.get(), buffer.data(), buffer.size());
    if (bytes == 0)
      break;
    if (bytes < 0)
    {
      if (errno == EINTR)
        continue;
      state = StoredRecordState::Unavailable;
      return std::nullopt;
    }
    if (body.size() + static_cast<std::size_t>(bytes) > kMaxRecordBytes)
    {
      state = StoredRecordState::Malformed;
      return std::nullopt;
    }
    body.append(buffer.data(), static_cast<std::size_t>(bytes));
  }
  state = StoredRecordState::Present;
  return body;
}

OpenFile open_trace_counter_lock(int directory_fd) noexcept
{
  constexpr char name[] = "trace-counters-v1.lock";
  int const fd = ::openat(directory_fd, name, O_RDWR | O_CREAT | O_CLOEXEC | O_NOFOLLOW | O_NONBLOCK, kPrivateFileMode);
  if (fd < 0)
  {
    struct stat existing{};
    if (errno == ELOOP || errno == EISDIR || ::fstatat(directory_fd, name, &existing, AT_SYMLINK_NOFOLLOW) == 0)
      return {.state = OpenState::Unsafe};
    return {.state = OpenState::Unavailable};
  }
  ScopedFd opened(fd);
  struct stat metadata{};
  struct stat published{};
  if (::fstat(opened.get(), &metadata) != 0 || ::fstatat(directory_fd, name, &published, AT_SYMLINK_NOFOLLOW) != 0)
    return {.state = OpenState::Unavailable};
  if (!private_file_metadata(metadata) || !private_file_metadata(published) || metadata.st_dev != published.st_dev || metadata.st_ino != published.st_ino)
    return {.state = OpenState::Unsafe};
  return {.state = OpenState::Ready, .fd = std::move(opened)};
}

bool lock_trace_counter_file(int directory_fd, int lock_fd) noexcept
{
  while (::flock(lock_fd, LOCK_EX) != 0)
  {
    if (errno == EINTR)
      continue;
    return false;
  }
  struct stat metadata{};
  struct stat published{};
  return ::fstat(lock_fd, &metadata) == 0 && ::fstatat(directory_fd, "trace-counters-v1.lock", &published, AT_SYMLINK_NOFOLLOW) == 0 &&
         private_file_metadata(metadata) && private_file_metadata(published) && metadata.st_dev == published.st_dev && metadata.st_ino == published.st_ino;
}

bool write_all(int fd, std::string_view body) noexcept
{
  std::size_t offset = 0;
  while (offset < body.size())
  {
    auto const bytes = ::write(fd, body.data() + offset, body.size() - offset);
    if (bytes < 0 && errno == EINTR)
      continue;
    if (bytes <= 0)
      return false;
    offset += static_cast<std::size_t>(bytes);
  }
  return true;
}

std::string opaque_token()
{
  std::array<unsigned char, 16> bytes{};
  std::size_t offset = 0;
  while (offset < bytes.size())
  {
    auto const count = ::getrandom(bytes.data() + offset, bytes.size() - offset, 0);
    if (count < 0 && errno == EINTR)
      continue;
    if (count <= 0)
      break;
    offset += static_cast<std::size_t>(count);
  }
  if (offset != bytes.size())
  {
    static std::atomic<std::uint64_t> sequence{0};
    auto value = static_cast<std::uint64_t>(std::chrono::steady_clock::now().time_since_epoch().count()) ^ (static_cast<std::uint64_t>(::getpid()) << 32U) ^
                 sequence.fetch_add(1, std::memory_order_relaxed);
    for (std::size_t index = 0; index < bytes.size(); ++index)
    {
      value ^= value << 13U;
      value ^= value >> 7U;
      value ^= value << 17U;
      bytes[index] = static_cast<unsigned char>(value & 0xffU);
    }
  }
  constexpr char hex[] = "0123456789abcdef";
  std::string result;
  result.reserve(bytes.size() * 2);
  for (auto const byte : bytes)
  {
    result.push_back(hex[byte >> 4U]);
    result.push_back(hex[byte & 0x0fU]);
  }
  return result;
}

bool safe_existing_replace_target(int directory_fd, char const* name) noexcept
{
  struct stat metadata{};
  if (::fstatat(directory_fd, name, &metadata, AT_SYMLINK_NOFOLLOW) != 0)
    return errno == ENOENT;
  return private_file_metadata(metadata);
}

ArtifactWriteStatus atomic_replace(int directory_fd, char const* target, std::string_view body)
{
  if (body.empty() || body.size() > kMaxRecordBytes || !safe_existing_replace_target(directory_fd, target))
    return ArtifactWriteStatus::UnsafeStorage;
  auto const temporary = ".diagnostic-tmp-" + opaque_token();
  ScopedFd file(::openat(directory_fd, temporary.c_str(), O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC | O_NONBLOCK | O_NOFOLLOW, kPrivateFileMode));
  if (!file)
    return ArtifactWriteStatus::IoFailure;
  auto rollback = [&] { static_cast<void>(::unlinkat(directory_fd, temporary.c_str(), 0)); };
  if (!write_all(file.get(), body) || ::fsync(file.get()) != 0)
  {
    rollback();
    return ArtifactWriteStatus::IoFailure;
  }
  struct stat metadata{};
  if (::fstat(file.get(), &metadata) != 0 || !private_file_metadata(metadata))
  {
    rollback();
    return ArtifactWriteStatus::UnsafeStorage;
  }
  if (::renameat(directory_fd, temporary.c_str(), directory_fd, target) != 0)
  {
    rollback();
    return ArtifactWriteStatus::IoFailure;
  }
  if (::fsync(directory_fd) != 0)
    return ArtifactWriteStatus::IoFailure;
  return ArtifactWriteStatus::Success;
}

int rename_noreplace(int directory_fd, char const* old_name, char const* new_name) noexcept
{
#if defined(SYS_renameat2) && defined(RENAME_NOREPLACE)
  return static_cast<int>(::syscall(SYS_renameat2, directory_fd, old_name, directory_fd, new_name, RENAME_NOREPLACE));
#else
  if (::linkat(directory_fd, old_name, directory_fd, new_name, 0) != 0)
    return -1;
  if (::unlinkat(directory_fd, old_name, 0) != 0)
  {
    auto const saved = errno;
    static_cast<void>(::unlinkat(directory_fd, new_name, 0));
    errno = saved;
    return -1;
  }
  return 0;
#endif
}

struct UniquePublication
{
  ArtifactWriteStatus status = ArtifactWriteStatus::IoFailure;
  std::string filename;
};

UniquePublication publish_unique(int directory_fd, std::string_view body)
{
  if (body.empty() || body.size() > kMaxRecordBytes)
    return {.status = ArtifactWriteStatus::InvalidRecord, .filename = {}};
  for (int attempt = 0; attempt < 32; ++attempt)
  {
    auto const token = opaque_token();
    auto const temporary = ".support-tmp-" + token;
    auto final_name = "ava-support-v1-" + token + ".json";
    ScopedFd file(::openat(directory_fd, temporary.c_str(), O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC | O_NONBLOCK | O_NOFOLLOW, kPrivateFileMode));
    if (!file)
    {
      if (errno == EEXIST)
        continue;
      return {.status = ArtifactWriteStatus::IoFailure, .filename = {}};
    }
    auto rollback_temp = [&] { static_cast<void>(::unlinkat(directory_fd, temporary.c_str(), 0)); };
    if (!write_all(file.get(), body) || ::fsync(file.get()) != 0)
    {
      rollback_temp();
      return {.status = ArtifactWriteStatus::IoFailure, .filename = {}};
    }
    struct stat metadata{};
    if (::fstat(file.get(), &metadata) != 0 || !private_file_metadata(metadata))
    {
      rollback_temp();
      return {.status = ArtifactWriteStatus::UnsafeStorage, .filename = {}};
    }
    if (rename_noreplace(directory_fd, temporary.c_str(), final_name.c_str()) != 0)
    {
      auto const saved = errno;
      rollback_temp();
      if (saved == EEXIST)
        continue;
      return {.status = ArtifactWriteStatus::IoFailure, .filename = {}};
    }
    if (::fsync(directory_fd) != 0)
    {
      static_cast<void>(::unlinkat(directory_fd, final_name.c_str(), 0));
      static_cast<void>(::fsync(directory_fd));
      return {.status = ArtifactWriteStatus::IoFailure, .filename = {}};
    }
    return {.status = ArtifactWriteStatus::Success, .filename = std::move(final_name)};
  }
  return {.status = ArtifactWriteStatus::IoFailure, .filename = {}};
}

TraceArtifactPreparation create_trace_artifact(int directory_fd, ava::config::XdgPaths const& paths)
{
  for (int attempt = 0; attempt < 32; ++attempt)
  {
    auto const filename = "trace-v1-" + opaque_token() + ".jsonl";
    ScopedFd file(::openat(directory_fd, filename.c_str(), O_WRONLY | O_APPEND | O_CREAT | O_EXCL | O_CLOEXEC | O_NONBLOCK | O_NOFOLLOW,
                           kPrivateFileMode));
    if (!file)
    {
      if (errno == EEXIST)
        continue;
      return {.status = ArtifactWriteStatus::IoFailure, .path = {}, .descriptor = {}};
    }
    struct stat metadata{};
    if (::fstat(file.get(), &metadata) != 0 || !private_file_metadata(metadata))
    {
      static_cast<void>(::unlinkat(directory_fd, filename.c_str(), 0));
      return {.status = ArtifactWriteStatus::UnsafeStorage, .path = {}, .descriptor = {}};
    }
    if (::fsync(file.get()) != 0 || ::fsync(directory_fd) != 0)
    {
      static_cast<void>(::unlinkat(directory_fd, filename.c_str(), 0));
      static_cast<void>(::fsync(directory_fd));
      return {.status = ArtifactWriteStatus::IoFailure, .path = {}, .descriptor = {}};
    }
    auto descriptor = std::make_shared<TraceArtifactDescriptor>(file.get());
    static_cast<void>(file.release());
    return {.status = ArtifactWriteStatus::Success,
            .path = paths.ava_state_dir / "diagnostics" / "traces" / filename,
            .descriptor = std::move(descriptor)};
  }
  return {.status = ArtifactWriteStatus::IoFailure, .path = {}, .descriptor = {}};
}

std::uint64_t saturating_add(std::uint64_t left, std::uint64_t right) noexcept
{
  auto const maximum = std::numeric_limits<std::uint64_t>::max();
  return right > maximum - left ? maximum : left + right;
}

TraceCounterSnapshot merge_trace_counter_snapshots(TraceCounterSnapshot const& aggregate, TraceCounterSnapshot const& contribution) noexcept
{
  return {.captured_at = std::max(aggregate.captured_at, contribution.captured_at),
          .runtime_starts = saturating_add(aggregate.runtime_starts, contribution.runtime_starts),
          .provider_requests = saturating_add(aggregate.provider_requests, contribution.provider_requests),
          .provider_failures = saturating_add(aggregate.provider_failures, contribution.provider_failures),
          .session_failures = saturating_add(aggregate.session_failures, contribution.session_failures),
          .plugin_failures = saturating_add(aggregate.plugin_failures, contribution.plugin_failures),
          .mcp_failures = saturating_add(aggregate.mcp_failures, contribution.mcp_failures),
          .writer_health = {.complete = aggregate.writer_health.complete && contribution.writer_health.complete,
                            .events_written = saturating_add(aggregate.writer_health.events_written, contribution.writer_health.events_written),
                            .events_dropped = saturating_add(aggregate.writer_health.events_dropped, contribution.writer_health.events_dropped),
                            .writer_failures = saturating_add(aggregate.writer_health.writer_failures, contribution.writer_health.writer_failures),
                            .bytes_written = saturating_add(aggregate.writer_health.bytes_written, contribution.writer_health.bytes_written)}};
}

template <typename Record, typename Parser>
StoredRecord<Record> read_record(ava::config::XdgPaths const& paths, ava::core::AnchorSet const& anchors, std::string_view filename, Parser parser) noexcept
{
  try
  {
    auto directory = open_artifact_directory(paths, anchors, "diagnostics", false);
    if (directory.state != OpenState::Ready)
      return {.state = stored_state(directory.state)};
    StoredRecordState state = StoredRecordState::Unavailable;
    auto body = read_private_file(directory.fd.get(), filename, state);
    if (!body)
      return {.state = state};
    auto record = parser(*body);
    if (!record)
      return {.state = StoredRecordState::Malformed};
    return {.state = StoredRecordState::Present, .record = std::move(*record)};
  }
  catch (...)
  {
    return {.state = StoredRecordState::Unavailable};
  }
}

}  // namespace

TraceArtifactDescriptor::TraceArtifactDescriptor(int fd) noexcept : fd_(fd)
{
}

TraceArtifactDescriptor::~TraceArtifactDescriptor()
{
  if (fd_ >= 0)
    static_cast<void>(::close(fd_));
}

int TraceArtifactDescriptor::fd() const noexcept
{
  return fd_;
}

std::string_view to_string(ArtifactWriteStatus status) noexcept
{
  switch (status)
  {
    case ArtifactWriteStatus::Success:
      return "success";
    case ArtifactWriteStatus::UnsafeStorage:
      return "unsafe_storage";
    case ArtifactWriteStatus::StorageUnavailable:
      return "storage_unavailable";
    case ArtifactWriteStatus::IoFailure:
      return "io_failure";
    case ArtifactWriteStatus::InvalidRecord:
      return "invalid_record";
  }
  return "io_failure";
}

StoredRecord<LastFailureRecord> read_last_failure_record(ava::config::XdgPaths const& paths, ava::core::AnchorSet const& anchors) noexcept
{
  return read_record<LastFailureRecord>(paths, anchors, "last-failure-v1.json", parse_last_failure_record);
}

StoredRecord<TraceCounterSnapshot> read_trace_counter_snapshot(ava::config::XdgPaths const& paths, ava::core::AnchorSet const& anchors) noexcept
{
  return read_record<TraceCounterSnapshot>(paths, anchors, "trace-counters-v1.json", parse_trace_counter_snapshot);
}

ArtifactWriteStatus write_last_failure_record(ava::config::XdgPaths const& paths, ava::core::AnchorSet const& anchors,
                                                    LastFailureRecord const& record) noexcept
{
  try
  {
    if (record.schema_version != kDiagnosticSchemaVersion)
      return ArtifactWriteStatus::InvalidRecord;
    auto const body = serialize_last_failure_record(record);
    if (!parse_last_failure_record(body))
      return ArtifactWriteStatus::InvalidRecord;
    auto directory = open_artifact_directory(paths, anchors, "diagnostics", true);
    if (directory.state != OpenState::Ready)
      return write_state(directory.state);
    return atomic_replace(directory.fd.get(), "last-failure-v1.json", body);
  }
  catch (...)
  {
    return ArtifactWriteStatus::IoFailure;
  }
}

ArtifactWriteStatus write_trace_counter_snapshot(ava::config::XdgPaths const& paths, ava::core::AnchorSet const& anchors,
                                                       TraceCounterSnapshot const& snapshot) noexcept
{
  try
  {
    if (snapshot.schema_version != kDiagnosticSchemaVersion || !parse_trace_counter_snapshot(serialize_trace_counter_snapshot(snapshot)))
      return ArtifactWriteStatus::InvalidRecord;
    auto directory = open_artifact_directory(paths, anchors, "diagnostics", true);
    if (directory.state != OpenState::Ready)
      return write_state(directory.state);
    auto lock = open_trace_counter_lock(directory.fd.get());
    if (lock.state != OpenState::Ready)
      return write_state(lock.state);
    if (!lock_trace_counter_file(directory.fd.get(), lock.fd.get()))
      return ArtifactWriteStatus::UnsafeStorage;

    TraceCounterSnapshot aggregate{.writer_health = {.complete = true}};
    StoredRecordState existing_state = StoredRecordState::Unavailable;
    auto existing_body = read_private_file(directory.fd.get(), "trace-counters-v1.json", existing_state);
    if (existing_body)
    {
      auto existing = parse_trace_counter_snapshot(*existing_body);
      if (!existing)
        return ArtifactWriteStatus::InvalidRecord;
      aggregate = std::move(*existing);
    }
    else if (existing_state != StoredRecordState::Absent)
    {
      if (existing_state == StoredRecordState::Unsafe)
        return ArtifactWriteStatus::UnsafeStorage;
      if (existing_state == StoredRecordState::Malformed)
        return ArtifactWriteStatus::InvalidRecord;
      return ArtifactWriteStatus::StorageUnavailable;
    }

    auto const merged = merge_trace_counter_snapshots(aggregate, snapshot);
    auto const body = serialize_trace_counter_snapshot(merged);
    if (!parse_trace_counter_snapshot(body))
      return ArtifactWriteStatus::InvalidRecord;
    return atomic_replace(directory.fd.get(), "trace-counters-v1.json", body);
  }
  catch (...)
  {
    return ArtifactWriteStatus::IoFailure;
  }
}

void write_last_failure_record_best_effort(ava::config::XdgPaths const& paths, ava::core::AnchorSet const& anchors,
                                           LastFailureRecord const& record) noexcept
{
  static_cast<void>(write_last_failure_record(paths, anchors, record));
}

TraceArtifactPreparation prepare_trace_artifact(ava::config::XdgPaths const& paths, ava::core::AnchorSet const& anchors) noexcept
{
  try
  {
    auto diagnostics = open_artifact_directory(paths, anchors, "diagnostics", true);
    if (diagnostics.state != OpenState::Ready)
      return {.status = write_state(diagnostics.state), .path = {}, .descriptor = {}};
    auto traces = open_private_child(diagnostics.fd.get(), "traces", true);
    if (traces.state != OpenState::Ready)
      return {.status = write_state(traces.state), .path = {}, .descriptor = {}};
    return create_trace_artifact(traces.fd.get(), paths);
  }
  catch (...)
  {
    return {.status = ArtifactWriteStatus::IoFailure, .path = {}, .descriptor = {}};
  }
}

SupportArtifactPublication publish_support_artifact(ava::config::XdgPaths const& paths, ava::core::AnchorSet const& anchors,
                                                      SupportArtifact const& artifact) noexcept
{
  try
  {
    if (artifact.schema_version != kDiagnosticSchemaVersion || artifact.generated_at < 0 || artifact.doctor.schema_version != kDiagnosticSchemaVersion)
      return {.status = ArtifactWriteStatus::InvalidRecord, .path = {}};
    if ((artifact.trace.state == StoredRecordState::Present) != artifact.trace.record.has_value() ||
        (artifact.last_failure.state == StoredRecordState::Present) != artifact.last_failure.record.has_value())
      return {.status = ArtifactWriteStatus::InvalidRecord, .path = {}};
    if (artifact.trace.record && (artifact.trace.record->schema_version != kDiagnosticSchemaVersion ||
                                  !parse_trace_counter_snapshot(serialize_trace_counter_snapshot(*artifact.trace.record))))
      return {.status = ArtifactWriteStatus::InvalidRecord, .path = {}};
    if (artifact.last_failure.record && (artifact.last_failure.record->schema_version != kDiagnosticSchemaVersion ||
                                         !parse_last_failure_record(serialize_last_failure_record(*artifact.last_failure.record))))
      return {.status = ArtifactWriteStatus::InvalidRecord, .path = {}};
    auto const doctor_json = serialize_doctor_report_json(artifact.doctor);
    if (!parse_doctor_report_json(doctor_json))
      return {.status = ArtifactWriteStatus::InvalidRecord, .path = {}};
    auto const body = serialize_support_artifact(artifact);
    if (body.size() > kMaxRecordBytes || ava::core::validate_strict_json(body, 16) != ava::core::StrictJsonStatus::Valid)
      return {.status = ArtifactWriteStatus::InvalidRecord, .path = {}};
    auto directory = open_artifact_directory(paths, anchors, "support", true);
    if (directory.state != OpenState::Ready)
      return {.status = write_state(directory.state), .path = {}};
    auto published = publish_unique(directory.fd.get(), body);
    return {.status = published.status,
            .path = published.status == ArtifactWriteStatus::Success ? paths.ava_state_dir / "support" / published.filename : std::filesystem::path{}};
  }
  catch (...)
  {
    return {.status = ArtifactWriteStatus::IoFailure, .path = {}};
  }
}

}  // namespace ava::diagnostics
