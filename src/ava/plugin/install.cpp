#include "sys.h"
#include "ava/plugin/install.h"
#include "ava/core/ids.h"
#include "ava/core/path.h"

#include <array>
#include <cerrno>
#include <cstring>
#include <filesystem>
#include <utility>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

namespace ava::plugin {
namespace {

class UniqueFileDescriptor
{
 public:
  explicit UniqueFileDescriptor(int fd) : fd_(fd) { }
  UniqueFileDescriptor(UniqueFileDescriptor const&) = delete;
  UniqueFileDescriptor& operator=(UniqueFileDescriptor const&) = delete;
  UniqueFileDescriptor(UniqueFileDescriptor&& other) noexcept : fd_(std::exchange(other.fd_, -1)) { }
  UniqueFileDescriptor& operator=(UniqueFileDescriptor&& other) noexcept
  {
    if (this != &other)
    {
      reset();
      fd_ = std::exchange(other.fd_, -1);
    }
    return *this;
  }
  ~UniqueFileDescriptor() { reset(); }

  [[nodiscard]] int get() const { return fd_; }

  AVA_DEBUG_PRINT_MEMBERS_OPT_OUT

 private:
  void reset()
  {
    if (fd_ >= 0)
      ::close(fd_);
    fd_ = -1;
  }

  int fd_ = -1;
};

struct OpenedInstallSourceFile
{
  UniqueFileDescriptor fd;
  mode_t mode = 0;

  AVA_DEBUG_PRINT_MEMBERS_OPT_OUT
};

bool path_is_within(std::filesystem::path const& base, std::filesystem::path const& target)
{
  if (target == base)
    return true;
  std::error_code relative_error;
  auto const relative = std::filesystem::relative(target, base, relative_error);
  if (relative_error || relative.empty())
    return false;
  return *relative.begin() != "..";
}

std::filesystem::path normalized_absolute(std::filesystem::path const& path)
{
  return ava::core::normalized_absolute_path(path);
}

ava::core::Error filesystem_errno_error(ava::core::ErrorCategory category, std::string message, std::filesystem::path const& path, int error_number)
{
  return ava::core::Error(category, std::move(message)).with_context("path", path.string()).with_context("cause", std::strerror(error_number));
}

mode_t safe_installed_file_mode(mode_t source_mode)
{
  mode_t mode = S_IRUSR | S_IWUSR;
  if ((source_mode & (S_IXUSR | S_IXGRP | S_IXOTH)) != 0)
    mode |= S_IXUSR;
  return mode;
}

ava::core::VoidResult set_safe_directory_permissions(std::filesystem::path const& path)
{
  std::error_code permission_error;
  std::filesystem::permissions(path, std::filesystem::perms::owner_all, std::filesystem::perm_options::replace, permission_error);
  if (permission_error)
  {
    return std::unexpected(ava::core::Error(ava::core::ErrorCategory::Io, "failed to set plugin install directory permissions")
                               .with_context("path", path.string())
                               .with_context("cause", permission_error.message()));
  }
  return {};
}

ava::core::Result<OpenedInstallSourceFile> open_install_source_file_no_follow(std::filesystem::path const& source)
{
  int fd = -1;
  do
  {
    fd = ::open(source.c_str(), O_RDONLY | O_CLOEXEC | O_NOFOLLOW);
  } while (fd < 0 && errno == EINTR);
  if (fd < 0)
  {
    auto const error_number = errno;
    auto const category = error_number == ELOOP ? ava::core::ErrorCategory::InvalidArgument : ava::core::ErrorCategory::Io;
    auto const message = error_number == ELOOP ? "plugin install source must not contain symlinks" : "failed to open plugin install source file";
    return std::unexpected(filesystem_errno_error(category, message, source, error_number));
  }

  UniqueFileDescriptor source_fd(fd);
  struct stat file_stat{};
  if (::fstat(source_fd.get(), &file_stat) != 0)
  {
    auto const error_number = errno;
    return std::unexpected(filesystem_errno_error(ava::core::ErrorCategory::Io, "failed to inspect plugin install source file", source, error_number));
  }
  if (!S_ISREG(file_stat.st_mode))
  {
    return std::unexpected(ava::core::Error(ava::core::ErrorCategory::InvalidArgument, "plugin install source must contain only directories and regular files")
                               .with_context("path", source.string()));
  }
  return OpenedInstallSourceFile{.fd = std::move(source_fd), .mode = file_stat.st_mode};
}

ava::core::Result<UniqueFileDescriptor> create_install_target_file_no_follow(std::filesystem::path const& target, mode_t mode)
{
  int fd = -1;
  do
  {
    fd = ::open(target.c_str(), O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC | O_NOFOLLOW, mode);
  } while (fd < 0 && errno == EINTR);
  if (fd < 0)
  {
    auto const error_number = errno;
    return std::unexpected(filesystem_errno_error(ava::core::ErrorCategory::Io, "failed to create plugin install file", target, error_number));
  }
  return UniqueFileDescriptor(fd);
}

ava::core::VoidResult copy_regular_file_no_follow(std::filesystem::path const& source, std::filesystem::path const& target)
{
  auto source_file = open_install_source_file_no_follow(source);
  if (!source_file)
    return std::unexpected(std::move(source_file.error()));

  auto const target_mode = safe_installed_file_mode(source_file->mode);
  auto target_file = create_install_target_file_no_follow(target, target_mode);
  if (!target_file)
    return std::unexpected(std::move(target_file.error()));

  std::array<char, 64 * 1024> buffer{};
  while (true)
  {
    ssize_t read_count = ::read(source_file->fd.get(), buffer.data(), buffer.size());
    if (read_count < 0 && errno == EINTR)
      continue;
    if (read_count < 0)
    {
      auto const error_number = errno;
      return std::unexpected(filesystem_errno_error(ava::core::ErrorCategory::Io, "failed to read plugin install source file", source, error_number));
    }
    if (read_count == 0)
      break;

    char const* cursor = buffer.data();
    auto remaining = static_cast<std::size_t>(read_count);
    while (remaining > 0)
    {
      ssize_t written = ::write(target_file->get(), cursor, remaining);
      if (written < 0 && errno == EINTR)
        continue;
      if (written < 0)
      {
        auto const error_number = errno;
        return std::unexpected(filesystem_errno_error(ava::core::ErrorCategory::Io, "failed to write plugin install file", target, error_number));
      }
      cursor += written;
      remaining -= static_cast<std::size_t>(written);
    }
  }

  if (::fchmod(target_file->get(), target_mode) != 0)
  {
    auto const error_number = errno;
    return std::unexpected(filesystem_errno_error(ava::core::ErrorCategory::Io, "failed to set plugin install file permissions", target, error_number));
  }
  return {};
}

ava::core::VoidResult ensure_global_plugin_install_root(std::filesystem::path const& root)
{
  std::error_code status_error;
  auto const status = std::filesystem::symlink_status(root, status_error);
  if (status_error && status_error != std::errc::no_such_file_or_directory)
  {
    return std::unexpected(ava::core::Error(ava::core::ErrorCategory::Io, "failed to inspect global plugin directory")
                               .with_context("path", root.string())
                               .with_context("cause", status_error.message()));
  }
  if (!status_error && std::filesystem::exists(status))
  {
    if (std::filesystem::is_symlink(status))
    {
      return std::unexpected(
          ava::core::Error(ava::core::ErrorCategory::InvalidArgument, "global plugin directory must not be a symlink").with_context("path", root.string()));
    }
    if (!std::filesystem::is_directory(status))
    {
      return std::unexpected(
          ava::core::Error(ava::core::ErrorCategory::InvalidArgument, "global plugin directory is not a directory").with_context("path", root.string()));
    }
    return set_safe_directory_permissions(root);
  }

  std::error_code create_error;
  std::filesystem::create_directories(root, create_error);
  if (create_error)
  {
    return std::unexpected(ava::core::Error(ava::core::ErrorCategory::Io, "failed to create global plugin directory")
                               .with_context("path", root.string())
                               .with_context("cause", create_error.message()));
  }
  return set_safe_directory_permissions(root);
}

ava::core::VoidResult ensure_install_source_is_separate(std::filesystem::path const& source_dir, std::filesystem::path const& global_root)
{
  auto const source = normalized_absolute(source_dir);
  auto const root = normalized_absolute(global_root);
  if (path_is_within(source, root))
  {
    return std::unexpected(ava::core::Error(ava::core::ErrorCategory::InvalidArgument, "plugin install source must not contain the global plugin directory")
                               .with_context("source", source_dir.string())
                               .with_context("global_plugins", global_root.string()));
  }
  return {};
}

ava::core::VoidResult ensure_destination_available(std::filesystem::path const& destination)
{
  std::error_code status_error;
  auto const status = std::filesystem::symlink_status(destination, status_error);
  if (status_error && status_error != std::errc::no_such_file_or_directory)
  {
    return std::unexpected(ava::core::Error(ava::core::ErrorCategory::Io, "failed to inspect plugin install destination")
                               .with_context("path", destination.string())
                               .with_context("cause", status_error.message()));
  }
  if (!status_error && std::filesystem::exists(status))
  {
    return std::unexpected(
        ava::core::Error(ava::core::ErrorCategory::InvalidArgument, "plugin install destination already exists").with_context("path", destination.string()));
  }
  return {};
}

ava::core::Result<std::filesystem::path> make_install_temp_dir(std::filesystem::path const& global_root, std::string const& plugin_id)
{
  for (int attempt = 0; attempt < 8; ++attempt)
  {
    auto const temp_dir = global_root / (plugin_id + ".installing-" + ava::core::make_id("plugin"));
    std::error_code create_error;
    if (std::filesystem::create_directory(temp_dir, create_error))
    {
      if (auto permissions = set_safe_directory_permissions(temp_dir); !permissions)
      {
        std::error_code remove_error;
        std::filesystem::remove_all(temp_dir, remove_error);
        return std::unexpected(std::move(permissions.error()));
      }
      return temp_dir;
    }
    if (create_error && create_error != std::errc::file_exists)
    {
      return std::unexpected(ava::core::Error(ava::core::ErrorCategory::Io, "failed to create plugin install staging directory")
                                 .with_context("path", temp_dir.string())
                                 .with_context("cause", create_error.message()));
    }
  }
  return std::unexpected(
      ava::core::Error(ava::core::ErrorCategory::Io, "failed to allocate plugin install staging directory").with_context("path", global_root.string()));
}

ava::core::VoidResult copy_plugin_directory_entry(std::filesystem::path const& source_dir, std::filesystem::path const& temp_dir,
                                                  std::filesystem::directory_entry const& entry)
{
  std::error_code status_error;
  auto const status = entry.symlink_status(status_error);
  if (status_error)
  {
    return std::unexpected(ava::core::Error(ava::core::ErrorCategory::Io, "failed to inspect plugin install source entry")
                               .with_context("path", entry.path().string())
                               .with_context("cause", status_error.message()));
  }
  if (std::filesystem::is_symlink(status))
  {
    return std::unexpected(ava::core::Error(ava::core::ErrorCategory::InvalidArgument, "plugin install source must not contain symlinks")
                               .with_context("path", entry.path().string()));
  }

  std::error_code relative_error;
  auto const relative = std::filesystem::relative(entry.path(), source_dir, relative_error);
  if (relative_error || relative.empty())
  {
    auto error = ava::core::Error(ava::core::ErrorCategory::Io, "failed to resolve plugin install source entry");
    error.with_context("path", entry.path().string());
    if (relative_error)
      error.with_context("cause", relative_error.message());
    return std::unexpected(std::move(error));
  }
  auto const target = temp_dir / relative;
  if (std::filesystem::is_directory(status))
  {
    std::error_code create_error;
    std::filesystem::create_directories(target, create_error);
    if (create_error)
    {
      return std::unexpected(ava::core::Error(ava::core::ErrorCategory::Io, "failed to create plugin install directory")
                                 .with_context("path", target.string())
                                 .with_context("cause", create_error.message()));
    }
    return set_safe_directory_permissions(target);
  }
  if (!std::filesystem::is_regular_file(status))
  {
    return std::unexpected(ava::core::Error(ava::core::ErrorCategory::InvalidArgument, "plugin install source must contain only directories and regular files")
                               .with_context("path", entry.path().string()));
  }

  std::error_code create_error;
  std::filesystem::create_directories(target.parent_path(), create_error);
  if (create_error)
  {
    return std::unexpected(ava::core::Error(ava::core::ErrorCategory::Io, "failed to create plugin install file parent")
                               .with_context("path", target.parent_path().string())
                               .with_context("cause", create_error.message()));
  }
  if (auto permissions = set_safe_directory_permissions(target.parent_path()); !permissions)
    return std::unexpected(std::move(permissions.error()));
  return copy_regular_file_no_follow(entry.path(), target);
}

ava::core::VoidResult copy_plugin_directory(std::filesystem::path const& source_dir, std::filesystem::path const& temp_dir)
{
  std::error_code iterate_error;
  std::filesystem::recursive_directory_iterator iterator(source_dir, std::filesystem::directory_options::none, iterate_error);
  std::filesystem::recursive_directory_iterator end;
  if (iterate_error)
  {
    return std::unexpected(ava::core::Error(ava::core::ErrorCategory::Io, "failed to inspect plugin install source directory")
                               .with_context("path", source_dir.string())
                               .with_context("cause", iterate_error.message()));
  }
  while (iterator != end)
  {
    auto const entry = *iterator;
    if (auto copied = copy_plugin_directory_entry(source_dir, temp_dir, entry); !copied)
      return copied;
    iterator.increment(iterate_error);
    if (iterate_error)
    {
      return std::unexpected(ava::core::Error(ava::core::ErrorCategory::Io, "failed to inspect plugin install source directory entry")
                                 .with_context("path", source_dir.string())
                                 .with_context("cause", iterate_error.message()));
    }
  }
  return {};
}

void cleanup_install_temp_dir(std::filesystem::path const& temp_dir)
{
  if (temp_dir.empty())
    return;
  std::error_code remove_error;
  std::filesystem::remove_all(temp_dir, remove_error);
}

ava::core::VoidResult ensure_removable_global_plugin(PluginStatus const& status, std::filesystem::path const& global_root)
{
  if (status.plugin.scope != PluginScope::Global)
  {
    return std::unexpected(ava::core::Error(ava::core::ErrorCategory::InvalidArgument, "only global installed plugins can be removed")
                               .with_context("plugin", status.plugin.manifest.id));
  }
  if (status.enabled)
  {
    return std::unexpected(
        ava::core::Error(ava::core::ErrorCategory::InvalidArgument, "disable plugin before removing it").with_context("plugin", status.plugin.manifest.id));
  }
  auto const root = normalized_absolute(global_root);
  auto const directory = normalized_absolute(status.plugin.manifest.directory);
  if (directory == root || !path_is_within(root, directory))
  {
    return std::unexpected(ava::core::Error(ava::core::ErrorCategory::InvalidArgument, "plugin directory is outside global plugin directory")
                               .with_context("plugin", status.plugin.manifest.id)
                               .with_context("path", status.plugin.manifest.directory.string()));
  }
  std::error_code status_error;
  auto const directory_status = std::filesystem::symlink_status(status.plugin.manifest.directory, status_error);
  if (status_error || std::filesystem::is_symlink(directory_status) || !std::filesystem::is_directory(directory_status))
  {
    auto error = ava::core::Error(status_error ? ava::core::ErrorCategory::Io : ava::core::ErrorCategory::InvalidArgument,
                                  "plugin directory is not a removable directory");
    error.with_context("plugin", status.plugin.manifest.id);
    error.with_context("path", status.plugin.manifest.directory.string());
    if (status_error)
      error.with_context("cause", status_error.message());
    return std::unexpected(std::move(error));
  }
  return {};
}

}  // namespace

ava::core::Result<PluginInstallSource> inspect_plugin_install_source(std::filesystem::path const& resolved_source)
{
  std::error_code status_error;
  auto const status = std::filesystem::symlink_status(resolved_source, status_error);
  if (status_error)
  {
    auto error = ava::core::Error(status_error == std::errc::no_such_file_or_directory ? ava::core::ErrorCategory::NotFound : ava::core::ErrorCategory::Io,
                                  "failed to inspect plugin install source");
    error.with_context("path", resolved_source.string());
    error.with_context("cause", status_error.message());
    return std::unexpected(std::move(error));
  }
  std::filesystem::path source_dir;
  if (std::filesystem::is_symlink(status))
  {
    return std::unexpected(ava::core::Error(ava::core::ErrorCategory::InvalidArgument, "plugin install source must not be a symlink")
                               .with_context("path", resolved_source.string()));
  }
  if (std::filesystem::is_directory(status))
  {
    source_dir = resolved_source;
  }
  else if (std::filesystem::is_regular_file(status) && resolved_source.filename() == "plugin.json")
  {
    auto const parent = resolved_source.parent_path();
    std::error_code parent_error;
    auto const parent_status = std::filesystem::symlink_status(parent, parent_error);
    if (parent_error || std::filesystem::is_symlink(parent_status) || !std::filesystem::is_directory(parent_status))
    {
      auto error = ava::core::Error(parent_error ? ava::core::ErrorCategory::Io : ava::core::ErrorCategory::InvalidArgument,
                                    "plugin install manifest parent must be a real directory");
      error.with_context("path", parent.string());
      if (parent_error)
        error.with_context("cause", parent_error.message());
      return std::unexpected(std::move(error));
    }
    source_dir = parent;
  }
  else
  {
    return std::unexpected(ava::core::Error(ava::core::ErrorCategory::InvalidArgument, "plugin install source must be a directory or plugin.json file")
                               .with_context("path", resolved_source.string()));
  }

  auto manifest = load_plugin_manifest(source_dir / "plugin.json");
  if (!manifest)
    return std::unexpected(std::move(manifest.error()));
  return PluginInstallSource(std::move(source_dir), std::move(*manifest));
}

ava::core::Result<PluginInstallResult> install_global_plugin(PluginInstallSource source, std::filesystem::path const& global_root)
{
  auto const source_dir = source.directory();
  auto const initial_manifest = source.manifest();

  if (auto ensured = ensure_global_plugin_install_root(global_root); !ensured)
    return std::unexpected(std::move(ensured.error()));
  if (auto separate = ensure_install_source_is_separate(source_dir, global_root); !separate)
    return std::unexpected(std::move(separate.error()));

  auto const destination = global_root / initial_manifest.id;
  if (auto available = ensure_destination_available(destination); !available)
    return std::unexpected(std::move(available.error()));

  auto temp_dir = make_install_temp_dir(global_root, initial_manifest.id);
  if (!temp_dir)
    return std::unexpected(std::move(temp_dir.error()));

  if (auto copied = copy_plugin_directory(source_dir, *temp_dir); !copied)
  {
    cleanup_install_temp_dir(*temp_dir);
    return std::unexpected(std::move(copied.error()));
  }

  auto installed_manifest = load_plugin_manifest(*temp_dir / "plugin.json");
  if (!installed_manifest)
  {
    cleanup_install_temp_dir(*temp_dir);
    return std::unexpected(std::move(installed_manifest.error()));
  }
  if (installed_manifest->id != initial_manifest.id)
  {
    cleanup_install_temp_dir(*temp_dir);
    return std::unexpected(ava::core::Error(ava::core::ErrorCategory::InvalidArgument, "installed plugin id changed during copy")
                               .with_context("expected", initial_manifest.id)
                               .with_context("actual", installed_manifest->id));
  }

  if (auto available = ensure_destination_available(destination); !available)
  {
    cleanup_install_temp_dir(*temp_dir);
    return std::unexpected(std::move(available.error()));
  }
  std::error_code rename_error;
  std::filesystem::rename(*temp_dir, destination, rename_error);
  if (rename_error)
  {
    cleanup_install_temp_dir(*temp_dir);
    return std::unexpected(ava::core::Error(ava::core::ErrorCategory::Io, "failed to install plugin directory")
                               .with_context("source", temp_dir->string())
                               .with_context("target", destination.string())
                               .with_context("cause", rename_error.message()));
  }

  auto final_manifest = load_plugin_manifest(destination / "plugin.json");
  if (!final_manifest)
    return std::unexpected(std::move(final_manifest.error()));
  return PluginInstallResult{
      .manifest = std::move(*final_manifest),
      .source_directory = source_dir,
      .destination_directory = destination,
  };
}

ava::core::Result<PluginRemovalResult> remove_global_plugin(PluginStatus status, std::filesystem::path const& global_root)
{
  if (auto root = ensure_global_plugin_install_root(global_root); !root)
    return std::unexpected(std::move(root.error()));
  if (auto removable = ensure_removable_global_plugin(status, global_root); !removable)
    return std::unexpected(std::move(removable.error()));

  auto const removed_dir = status.plugin.manifest.directory;
  auto const manifest = status.plugin.manifest;
  std::error_code remove_error;
  std::filesystem::remove_all(removed_dir, remove_error);
  if (remove_error)
  {
    return std::unexpected(ava::core::Error(ava::core::ErrorCategory::Io, "failed to remove plugin directory")
                               .with_context("plugin", manifest.id)
                               .with_context("path", removed_dir.string())
                               .with_context("cause", remove_error.message()));
  }
  return PluginRemovalResult{
      .manifest = manifest,
      .removed_directory = removed_dir,
  };
}

}  // namespace ava::plugin
