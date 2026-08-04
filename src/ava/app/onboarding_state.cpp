#include "sys.h"
#include "ava/app/onboarding_state.h"
#include "ava/app/runtime/Session.h"
#include "ava/config/auth.h"
#include "ava/core/error.h"
#include "ava/core/json.h"

#include <array>
#include <cctype>
#include <cerrno>
#include <cstring>
#include <optional>
#include <string>
#include <utility>
#include <vector>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

namespace ava::app {
namespace {

constexpr std::string_view kOnboardingStateName = "onboarding.json";
constexpr mode_t kPrivateDirectoryMode = S_IRWXU;
constexpr mode_t kPrivateFileMode = S_IRUSR | S_IWUSR;

class ScopedFd
{
 public:
  explicit ScopedFd(int value = -1) noexcept : fd_(value) { }
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

struct DirectoryComponent
{
  std::string name;
  ScopedFd fd;
  dev_t device = 0;
  ino_t inode = 0;
};

struct DirectoryChain
{
  ScopedFd root;
  std::vector<DirectoryComponent> components;

  [[nodiscard]] int final_fd() const noexcept { return components.empty() ? root.get() : components.back().fd.get(); }
};

class TempFileCleanup
{
 public:
  TempFileCleanup(int directory_fd, std::string name) : directory_fd_(directory_fd), name_(std::move(name)) { }
  TempFileCleanup(TempFileCleanup const&) = delete;
  TempFileCleanup& operator=(TempFileCleanup const&) = delete;
  ~TempFileCleanup()
  {
    if (active_)
      static_cast<void>(::unlinkat(directory_fd_, name_.c_str(), 0));
  }

  void dismiss() noexcept { active_ = false; }

 private:
  int directory_fd_ = -1;
  std::string name_;
  bool active_ = true;
};

std::string errno_message(int error_number)
{
  return std::strerror(error_number);
}

ava::core::Error onboarding_error(ava::core::ErrorCategory category, std::string message, std::filesystem::path const& path,
                                  std::optional<int> error_number = std::nullopt)
{
  auto error = ava::core::Error(category, std::move(message));
  error.with_context("path", path.string());
  if (error_number)
    error.with_context("cause", errno_message(*error_number));
  return error;
}

OnboardingLoadResult malformed(std::string diagnostic)
{
  return OnboardingLoadResult{.kind = OnboardingLoadKind::UnsupportedOrMalformed, .status = std::nullopt, .diagnostic = std::move(diagnostic)};
}

// Path-free diagnostics only. Never include raw content, absolute paths, or credential material.
OnboardingLoadResult parse_onboarding_document(std::string_view content)
{
  if (content.empty())
    return malformed("setup state is empty; automatic first-run setup stays closed. Use /setup to open the wizard.");
  if (!ava::core::json::is_valid_object(content))
    return malformed("setup state is not a valid JSON object; automatic first-run setup stays closed. Use /setup to open the wizard.");

  auto const version = ava::core::json::integer_field(content, "version");
  if (!version)
    return malformed("setup state is missing a strict integer version; automatic first-run setup stays closed. Use /setup to open the wizard.");
  if (*version != static_cast<long long>(kOnboardingWizardVersion))
    return malformed("setup state version is unsupported; automatic first-run setup stays closed. Use /setup to open the wizard.");

  auto const status_text = ava::core::json::string_field(content, "status");
  if (!status_text)
    return malformed("setup state is missing a strict status string; automatic first-run setup stays closed. Use /setup to open the wizard.");
  auto const status = onboarding_status_from_string(*status_text);
  if (!status)
    return malformed("setup state status is unknown; automatic first-run setup stays closed. Use /setup to open the wizard.");

  return OnboardingLoadResult{.kind = *status == OnboardingStatus::Completed ? OnboardingLoadKind::CurrentCompleted : OnboardingLoadKind::CurrentSkipped,
                              .status = *status,
                              .diagnostic = {}};
}

ava::core::Result<std::vector<std::string>> absolute_directory_components(std::filesystem::path const& path)
{
  if (path.empty() || !path.is_absolute())
    return std::unexpected(onboarding_error(ava::core::ErrorCategory::InvalidArgument, "setup state directory must be absolute", path));

  std::vector<std::string> components;
  for (auto const& component : path.relative_path())
  {
    auto name = component.string();
    if (name.empty() || name == "." || name == ".." || name.find('/') != std::string::npos)
      return std::unexpected(onboarding_error(ava::core::ErrorCategory::InvalidArgument, "setup state directory contains an unsafe component", path));
    components.push_back(std::move(name));
  }
  if (components.empty())
    return std::unexpected(onboarding_error(ava::core::ErrorCategory::InvalidArgument, "setup state directory must not be the filesystem root", path));
  return components;
}

ava::core::Result<std::optional<DirectoryChain>> open_directory_chain(std::filesystem::path const& path, bool create_missing)
{
  auto names = absolute_directory_components(path);
  if (!names)
    return std::unexpected(std::move(names.error()));

  int root_flags = O_RDONLY | O_DIRECTORY | O_CLOEXEC;
#ifdef O_NOFOLLOW
  root_flags |= O_NOFOLLOW;
#endif
  DirectoryChain chain{.root = ScopedFd(::open("/", root_flags)), .components = {}};
  if (chain.root.get() < 0)
    return std::unexpected(onboarding_error(ava::core::ErrorCategory::Io, "failed to open filesystem root for setup state", path, errno));

  chain.components.reserve(names->size());
  int current_fd = chain.root.get();
  for (auto const& name : *names)
  {
    int flags = O_RDONLY | O_DIRECTORY | O_NONBLOCK | O_CLOEXEC;
#ifdef O_NOFOLLOW
    flags |= O_NOFOLLOW;
#endif
    int next_fd = ::openat(current_fd, name.c_str(), flags);
    if (next_fd < 0 && errno == ENOENT && create_missing)
    {
      if (::mkdirat(current_fd, name.c_str(), kPrivateDirectoryMode) != 0 && errno != EEXIST)
        return std::unexpected(onboarding_error(ava::core::ErrorCategory::Io, "failed to create setup state directory component", path, errno));
      next_fd = ::openat(current_fd, name.c_str(), flags);
    }
    if (next_fd < 0)
    {
      int const saved_errno = errno;
      if (saved_errno == ENOENT && !create_missing)
        return std::optional<DirectoryChain>{};
      auto const category = saved_errno == EACCES || saved_errno == EPERM || saved_errno == ELOOP || saved_errno == ENOTDIR
                                ? ava::core::ErrorCategory::PermissionDenied
                                : ava::core::ErrorCategory::Io;
      auto message = saved_errno == ELOOP
                         ? "setup state directory component must not be a symlink"
                         : (saved_errno == ENOTDIR ? "setup state directory component must be a directory" : "failed to open setup state directory component");
      return std::unexpected(onboarding_error(category, message, path, saved_errno));
    }

    ScopedFd next(next_fd);
    struct stat status{};
    if (::fstat(next.get(), &status) != 0)
      return std::unexpected(onboarding_error(ava::core::ErrorCategory::Io, "failed to inspect setup state directory component", path, errno));
    if (!S_ISDIR(status.st_mode))
      return std::unexpected(onboarding_error(ava::core::ErrorCategory::PermissionDenied, "setup state directory component must be a directory", path));

    chain.components.push_back(DirectoryComponent{.name = name, .fd = std::move(next), .device = status.st_dev, .inode = status.st_ino});
    current_fd = chain.components.back().fd.get();
  }
  return std::optional<DirectoryChain>{std::move(chain)};
}

ava::core::VoidResult revalidate_directory_chain(DirectoryChain const& chain, std::filesystem::path const& path)
{
  int parent_fd = chain.root.get();
  for (auto const& component : chain.components)
  {
    struct stat named{};
    if (::fstatat(parent_fd, component.name.c_str(), &named, AT_SYMLINK_NOFOLLOW) != 0)
      return std::unexpected(onboarding_error(ava::core::ErrorCategory::PermissionDenied, "setup state directory binding changed", path, errno));
    if (!S_ISDIR(named.st_mode) || named.st_dev != component.device || named.st_ino != component.inode)
      return std::unexpected(onboarding_error(ava::core::ErrorCategory::PermissionDenied, "setup state directory binding changed", path));
    parent_fd = component.fd.get();
  }
  return {};
}

ava::core::VoidResult prepare_final_directory(DirectoryChain const& chain, std::filesystem::path const& path)
{
  struct stat status{};
  if (::fstat(chain.final_fd(), &status) != 0)
    return std::unexpected(onboarding_error(ava::core::ErrorCategory::Io, "failed to inspect setup state directory", path, errno));
  if (!S_ISDIR(status.st_mode))
    return std::unexpected(onboarding_error(ava::core::ErrorCategory::PermissionDenied, "setup state directory must be a directory", path));
  if (status.st_uid != ::geteuid())
    return std::unexpected(onboarding_error(ava::core::ErrorCategory::PermissionDenied, "setup state directory must be owned by the current user", path));
  if (::fchmod(chain.final_fd(), kPrivateDirectoryMode) != 0)
    return std::unexpected(onboarding_error(ava::core::ErrorCategory::Io, "failed to set setup state directory permissions", path, errno));
  if (::fstat(chain.final_fd(), &status) != 0 || !S_ISDIR(status.st_mode) || status.st_uid != ::geteuid() || (status.st_mode & 0777) != 0700)
    return std::unexpected(onboarding_error(ava::core::ErrorCategory::PermissionDenied, "setup state directory permissions could not be verified", path));
  return {};
}

ava::core::Result<std::optional<struct stat>> inspect_target(int directory_fd, std::filesystem::path const& path)
{
  struct stat status{};
  if (::fstatat(directory_fd, kOnboardingStateName.data(), &status, AT_SYMLINK_NOFOLLOW) != 0)
  {
    int const saved_errno = errno;
    if (saved_errno == ENOENT)
      return std::optional<struct stat>{};
    auto const category = saved_errno == EACCES || saved_errno == EPERM ? ava::core::ErrorCategory::PermissionDenied : ava::core::ErrorCategory::Io;
    return std::unexpected(onboarding_error(category, "failed to inspect setup state target", path, saved_errno));
  }
  if (S_ISLNK(status.st_mode))
    return std::unexpected(onboarding_error(ava::core::ErrorCategory::PermissionDenied, "setup state target must not be a symlink", path));
  if (!S_ISREG(status.st_mode))
    return std::unexpected(onboarding_error(ava::core::ErrorCategory::PermissionDenied, "setup state target must be a regular file", path));
  if (status.st_uid != ::geteuid())
    return std::unexpected(onboarding_error(ava::core::ErrorCategory::PermissionDenied, "setup state target must be owned by the current user", path));
  if (status.st_nlink != 1)
    return std::unexpected(onboarding_error(ava::core::ErrorCategory::PermissionDenied, "setup state target must have exactly one link", path));
  return std::optional<struct stat>{status};
}

struct BoundedTextRead
{
  enum class Status : std::uint8_t
  {
    Absent,
    Complete,
    Truncated,
  };

  std::string content;
  Status status = Status::Absent;
};

ava::core::Result<BoundedTextRead> read_onboarding_text_bounded(DirectoryChain const& chain, std::filesystem::path const& path)
{
  auto inspected = inspect_target(chain.final_fd(), path);
  if (!inspected)
    return std::unexpected(std::move(inspected.error()));
  if (!*inspected)
    return BoundedTextRead{};

  int flags = O_RDONLY | O_NONBLOCK | O_CLOEXEC;
#ifdef O_NOFOLLOW
  flags |= O_NOFOLLOW;
#endif
  ScopedFd fd(::openat(chain.final_fd(), kOnboardingStateName.data(), flags));
  if (fd.get() < 0)
  {
    int const saved_errno = errno;
    if (saved_errno == ENOENT)
      return BoundedTextRead{};
    auto const category =
        saved_errno == EACCES || saved_errno == EPERM || saved_errno == ELOOP ? ava::core::ErrorCategory::PermissionDenied : ava::core::ErrorCategory::Io;
    return std::unexpected(onboarding_error(category, "failed to open setup state target", path, saved_errno));
  }

  struct stat before{};
  if (::fstat(fd.get(), &before) != 0)
    return std::unexpected(onboarding_error(ava::core::ErrorCategory::Io, "failed to inspect setup state descriptor", path, errno));
  if (!S_ISREG(before.st_mode) || before.st_uid != ::geteuid() || before.st_nlink != 1 || before.st_dev != (*inspected)->st_dev ||
      before.st_ino != (*inspected)->st_ino)
    return std::unexpected(onboarding_error(ava::core::ErrorCategory::PermissionDenied, "setup state target binding changed", path));
  if (before.st_size < 0 || static_cast<std::uintmax_t>(before.st_size) > static_cast<std::uintmax_t>(kMaxOnboardingStateBytes))
    return BoundedTextRead{.content = {}, .status = BoundedTextRead::Status::Truncated};

  BoundedTextRead result{.content = {}, .status = BoundedTextRead::Status::Complete};
  result.content.reserve(static_cast<std::size_t>(before.st_size));
  std::array<char, 1024> buffer{};
  while (result.content.size() < kMaxOnboardingStateBytes)
  {
    auto const remaining = kMaxOnboardingStateBytes - result.content.size();
    auto const count = ::read(fd.get(), buffer.data(), std::min(buffer.size(), remaining));
    if (count == 0)
      break;
    if (count < 0)
    {
      if (errno == EINTR)
        continue;
      return std::unexpected(onboarding_error(ava::core::ErrorCategory::Io, "failed while reading setup state", path, errno));
    }
    result.content.append(buffer.data(), static_cast<std::size_t>(count));
  }

  struct stat after{};
  if (::fstat(fd.get(), &after) != 0)
    return std::unexpected(onboarding_error(ava::core::ErrorCategory::Io, "failed to reinspect setup state descriptor", path, errno));
  auto const read_size = static_cast<std::uintmax_t>(result.content.size());
  if (!S_ISREG(after.st_mode) || after.st_uid != ::geteuid() || after.st_nlink != 1 || after.st_dev != before.st_dev || after.st_ino != before.st_ino ||
      after.st_size < 0 || static_cast<std::uintmax_t>(after.st_size) != read_size || after.st_size != before.st_size)
  {
    result.content.clear();
    result.status = BoundedTextRead::Status::Truncated;
  }
  return result;
}

std::string serialize_onboarding_status(OnboardingStatus status)
{
  std::string json = "{\"version\":";
  json += std::to_string(kOnboardingWizardVersion);
  json += ",\"status\":\"";
  json += to_string(status);
  json += "\"}\n";
  return json;
}

ava::core::VoidResult write_all(int fd, std::string_view body, std::filesystem::path const& path)
{
  std::size_t offset = 0;
  while (offset < body.size())
  {
    auto const count = ::write(fd, body.data() + offset, body.size() - offset);
    if (count < 0)
    {
      if (errno == EINTR)
        continue;
      return std::unexpected(onboarding_error(ava::core::ErrorCategory::Io, "failed to write temporary setup state", path, errno));
    }
    if (count == 0)
      return std::unexpected(onboarding_error(ava::core::ErrorCategory::Io, "temporary setup state write made no progress", path));
    offset += static_cast<std::size_t>(count);
  }
  return {};
}

ava::core::VoidResult sync_fd(int fd, std::filesystem::path const& path, std::string message)
{
  while (::fsync(fd) != 0)
  {
    if (errno == EINTR)
      continue;
    return std::unexpected(onboarding_error(ava::core::ErrorCategory::Io, std::move(message), path, errno));
  }
  return {};
}

std::string provider_display_label(std::string_view provider_id)
{
  if (provider_id.empty() || provider_id == "openai")
    return "OpenAI";
  if (provider_id == "anthropic")
    return "Anthropic";
  if (provider_id == "gemini")
    return "Gemini";
  std::string label(provider_id);
  if (!label.empty())
    label.front() = static_cast<char>(std::toupper(static_cast<unsigned char>(label.front())));
  return label;
}

}  // namespace

std::filesystem::path onboarding_state_file(ava::config::XdgPaths const& paths)
{
  return paths.ava_state_dir / kOnboardingStateName;
}

std::string_view to_string(OnboardingStatus status) noexcept
{
  switch (status)
  {
    case OnboardingStatus::Completed:
      return "completed";
    case OnboardingStatus::Skipped:
      return "skipped";
  }
  return "completed";
}

std::optional<OnboardingStatus> onboarding_status_from_string(std::string_view value) noexcept
{
  if (value == "completed")
    return OnboardingStatus::Completed;
  if (value == "skipped")
    return OnboardingStatus::Skipped;
  return std::nullopt;
}

OnboardingLoadResult load_onboarding_state(ava::config::XdgPaths const& paths)
{
  auto const path = onboarding_state_file(paths);
  if (path.empty())
    return OnboardingLoadResult{.kind = OnboardingLoadKind::Absent, .status = std::nullopt, .diagnostic = {}};

  auto chain = open_directory_chain(paths.ava_state_dir, false);
  if (!chain)
  {
    auto const& message = chain.error().message();
    if (message.find("symlink") != std::string::npos)
      return malformed("setup state directory must not contain symlinks; automatic first-run setup stays closed. Use /setup to open the wizard.");
    if (message.find("directory") != std::string::npos)
      return malformed("setup state directory is unsafe or unreadable; automatic first-run setup stays closed. Use /setup to open the wizard.");
    return malformed("setup state is unreadable; automatic first-run setup stays closed. Use /setup to open the wizard.");
  }
  if (!*chain)
    return OnboardingLoadResult{.kind = OnboardingLoadKind::Absent, .status = std::nullopt, .diagnostic = {}};

  auto read = read_onboarding_text_bounded(**chain, path);
  if (!read)
  {
    auto const& message = read.error().message();
    if (message.find("symlink") != std::string::npos)
      return malformed("setup state must not be a symlink; automatic first-run setup stays closed. Use /setup to open the wizard.");
    if (message.find("regular") != std::string::npos || message.find("link") != std::string::npos)
      return malformed("setup state must be an owned single-link regular file; automatic first-run setup stays closed. Use /setup to open the wizard.");
    return malformed("setup state is unreadable; automatic first-run setup stays closed. Use /setup to open the wizard.");
  }
  if (read->status == BoundedTextRead::Status::Absent)
    return OnboardingLoadResult{.kind = OnboardingLoadKind::Absent, .status = std::nullopt, .diagnostic = {}};
  if (read->status == BoundedTextRead::Status::Truncated)
    return malformed("setup state is too large or changed while reading; automatic first-run setup stays closed. Use /setup to open the wizard.");
  return parse_onboarding_document(read->content);
}

ava::core::VoidResult store_onboarding_status(ava::config::XdgPaths const& paths, OnboardingStatus status)
{
  auto const path = onboarding_state_file(paths);
  auto chain_result = open_directory_chain(paths.ava_state_dir, true);
  if (!chain_result)
    return std::unexpected(std::move(chain_result.error()));
  if (!*chain_result)
    return std::unexpected(onboarding_error(ava::core::ErrorCategory::Io, "setup state directory is unavailable", paths.ava_state_dir));
  auto chain = std::move(**chain_result);

  if (auto prepared = prepare_final_directory(chain, paths.ava_state_dir); !prepared)
    return prepared;
  if (auto validated = revalidate_directory_chain(chain, paths.ava_state_dir); !validated)
    return validated;

  auto original_target = inspect_target(chain.final_fd(), path);
  if (!original_target)
    return std::unexpected(std::move(original_target.error()));

  ScopedFd temp_fd;
  std::string temp_name;
  for (int attempt = 0; attempt < 128; ++attempt)
  {
    temp_name = ".onboarding.tmp." + std::to_string(::getpid()) + "." + std::to_string(attempt);
    int flags = O_WRONLY | O_CREAT | O_EXCL | O_NONBLOCK | O_CLOEXEC;
#ifdef O_NOFOLLOW
    flags |= O_NOFOLLOW;
#endif
    int const fd = ::openat(chain.final_fd(), temp_name.c_str(), flags, kPrivateFileMode);
    if (fd >= 0)
    {
      temp_fd = ScopedFd(fd);
      break;
    }
    if (errno != EEXIST)
      return std::unexpected(onboarding_error(ava::core::ErrorCategory::Io, "failed to create temporary setup state", path, errno));
  }
  if (temp_fd.get() < 0)
    return std::unexpected(onboarding_error(ava::core::ErrorCategory::Io, "failed to allocate unique temporary setup state", path));
  TempFileCleanup cleanup(chain.final_fd(), temp_name);

  struct stat temp_status{};
  if (::fstat(temp_fd.get(), &temp_status) != 0)
    return std::unexpected(onboarding_error(ava::core::ErrorCategory::Io, "failed to inspect temporary setup state", path, errno));
  if (!S_ISREG(temp_status.st_mode) || temp_status.st_uid != ::geteuid() || temp_status.st_nlink != 1)
    return std::unexpected(onboarding_error(ava::core::ErrorCategory::PermissionDenied, "temporary setup state is not a private regular file", path));
  if (::fchmod(temp_fd.get(), kPrivateFileMode) != 0)
    return std::unexpected(onboarding_error(ava::core::ErrorCategory::Io, "failed to set temporary setup state permissions", path, errno));
  if (::fstat(temp_fd.get(), &temp_status) != 0 || !S_ISREG(temp_status.st_mode) || temp_status.st_uid != ::geteuid() || temp_status.st_nlink != 1 ||
      (temp_status.st_mode & 0777) != 0600)
    return std::unexpected(onboarding_error(ava::core::ErrorCategory::PermissionDenied, "temporary setup state permissions could not be verified", path));

  auto const body = serialize_onboarding_status(status);
  if (auto written = write_all(temp_fd.get(), body, path); !written)
    return written;
  if (auto synced = sync_fd(temp_fd.get(), path, "failed to sync temporary setup state"); !synced)
    return synced;

  if (auto validated = revalidate_directory_chain(chain, paths.ava_state_dir); !validated)
    return validated;
  struct stat final_directory{};
  if (::fstat(chain.final_fd(), &final_directory) != 0 || !S_ISDIR(final_directory.st_mode) || final_directory.st_uid != ::geteuid() ||
      (final_directory.st_mode & 0777) != 0700)
    return std::unexpected(
        onboarding_error(ava::core::ErrorCategory::PermissionDenied, "setup state directory changed before publication", paths.ava_state_dir));

  auto current_target = inspect_target(chain.final_fd(), path);
  if (!current_target)
    return std::unexpected(std::move(current_target.error()));
  if (original_target->has_value() != current_target->has_value() ||
      (*original_target && ((*original_target)->st_dev != (*current_target)->st_dev || (*original_target)->st_ino != (*current_target)->st_ino)))
    return std::unexpected(onboarding_error(ava::core::ErrorCategory::PermissionDenied, "setup state target changed before publication", path));

  if (::renameat(chain.final_fd(), temp_name.c_str(), chain.final_fd(), kOnboardingStateName.data()) != 0)
    return std::unexpected(onboarding_error(ava::core::ErrorCategory::Io, "failed to publish setup state", path, errno));
  cleanup.dismiss();
  if (auto synced = sync_fd(chain.final_fd(), paths.ava_state_dir, "failed to sync setup state directory"); !synced)
    return synced;
  return {};
}

bool setup_wizard_auto_eligible(OnboardingLoadResult const& loaded) noexcept
{
  return loaded.kind == OnboardingLoadKind::Absent;
}

ava::tui::SetupReadinessSnapshot build_setup_readiness_snapshot(ava::config::XdgPaths const& paths, std::string_view provider_id)
{
  ava::tui::SetupReadinessSnapshot snapshot;
  auto const effective_provider = provider_id.empty() ? std::string_view{"openai"} : provider_id;
  snapshot.active_provider_label = provider_display_label(effective_provider);
  auto present = ava::config::provider_credential_presence_for_startup(paths, effective_provider);
  if (!present)
  {
    snapshot.active_provider_readiness_known = false;
    snapshot.active_provider_ready = false;
    return snapshot;
  }
  snapshot.active_provider_readiness_known = true;
  snapshot.active_provider_ready = *present;
  return snapshot;
}

ava::tui::SetupReadinessSnapshot build_setup_readiness_snapshot(runtime::session_ts const& session)
{
  SCOPED_CRITICAL_AREA_CR(session_r, session);
  auto const provider_id = session_r->model().provider_id.empty() ? std::string("openai") : session_r->model().provider_id;
  return build_setup_readiness_snapshot(session_r->paths(), provider_id);
}

}  // namespace ava::app
