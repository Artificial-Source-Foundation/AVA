#pragma once

#include "ava/debug/print_members_on.h"
#include "ava/core/result.h"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace ava::tools {

enum class SecureWorkspaceResolveMode
{
  Existing,
  AllowMissing,
};

struct SecureWorkspacePath
{
  std::filesystem::path absolute;
  std::filesystem::path relative;
  bool exists = false;
  bool regular_file = false;
  bool directory = false;
  AVA_DEBUG_PRINT_MEMBERS_OPT_OUT
};

class SecureWorkspaceHandle
{
 public:
  SecureWorkspaceHandle() = default;
  SecureWorkspaceHandle(SecureWorkspaceHandle const&) = delete;
  SecureWorkspaceHandle& operator=(SecureWorkspaceHandle const&) = delete;
  SecureWorkspaceHandle(SecureWorkspaceHandle&& other) noexcept;
  SecureWorkspaceHandle& operator=(SecureWorkspaceHandle&& other) noexcept;
  ~SecureWorkspaceHandle();

  [[nodiscard]] int fd() const noexcept;
  [[nodiscard]] std::uintmax_t size() const noexcept;
  [[nodiscard]] std::filesystem::path const& path() const noexcept;

  // Public for the platform implementation; callers receive handles only from
  // SecureWorkspace and cannot duplicate ownership by copy.
  SecureWorkspaceHandle(int fd, std::uintmax_t size, std::filesystem::path path);

 private:
  int fd_ = -1;
  std::uintmax_t size_ = 0;
  std::filesystem::path path_;
  AVA_DEBUG_PRINT_MEMBERS_OPT_OUT
};

enum class SecureWorkspaceNodeType
{
  RegularFile,
  Directory,
  Symlink,
  Other,
};

struct SecureWorkspaceDirectoryEntry
{
  std::string name;
  std::filesystem::path absolute_path;
  std::filesystem::path relative_path;
  SecureWorkspaceNodeType type = SecureWorkspaceNodeType::Other;
  std::uintmax_t size = 0;
  std::size_t depth = 0;
  AVA_DEBUG_PRINT_MEMBERS_OPT_OUT
};

enum class SecureWorkspaceWalkAction
{
  Continue,
  SkipDirectory,
  Stop,
};

using SecureWorkspaceWalkVisitor = std::function<ava::core::Result<SecureWorkspaceWalkAction>(SecureWorkspaceDirectoryEntry const& entry)>;

struct SecureWorkspaceWriteResult
{
  std::filesystem::path path;
  std::size_t bytes_written = 0;
  AVA_DEBUG_PRINT_MEMBERS_OPT_OUT
};

// A descriptor-anchored workspace. Every lookup is relative to the canonical
// root descriptor and rejects symlinks/magic links instead of checking a path
// and reopening it later.
class SecureWorkspace
{
 public:
  // A descriptor-anchored temporary write. Until commit succeeds, destruction
  // removes the temporary file through the retained parent descriptor.
  class StagedWrite
  {
   public:
    StagedWrite(StagedWrite const&) = delete;
    StagedWrite& operator=(StagedWrite const&) = delete;
    StagedWrite(StagedWrite&& other) noexcept;
    StagedWrite& operator=(StagedWrite&& other) noexcept;
    ~StagedWrite();

    [[nodiscard]] ava::core::VoidResult commit();
    [[nodiscard]] std::filesystem::path const& path() const noexcept;
    [[nodiscard]] std::size_t bytes_written() const noexcept;
    [[nodiscard]] bool target_changed() const noexcept;

   private:
    friend class SecureWorkspace;

    StagedWrite(int parent_fd, std::filesystem::path workspace_root, std::filesystem::path path, std::filesystem::path temp_name,
                std::filesystem::path target_name, std::size_t bytes_written);
    void cleanup() noexcept;

    int parent_fd_ = -1;
    std::filesystem::path workspace_root_;
    std::filesystem::path path_;
    std::filesystem::path temp_name_;
    std::filesystem::path target_name_;
    std::size_t bytes_written_ = 0;
    bool target_changed_ = false;
    AVA_DEBUG_PRINT_MEMBERS_OPT_OUT
  };

  SecureWorkspace(SecureWorkspace const&) = delete;
  SecureWorkspace& operator=(SecureWorkspace const&) = delete;
  SecureWorkspace(SecureWorkspace&& other) noexcept;
  SecureWorkspace& operator=(SecureWorkspace&& other) noexcept;
  ~SecureWorkspace();

  [[nodiscard]] static ava::core::Result<std::shared_ptr<SecureWorkspace>> open(std::filesystem::path const& root);

  [[nodiscard]] std::filesystem::path const& root() const noexcept;
  [[nodiscard]] ava::core::Result<SecureWorkspacePath> resolve(std::filesystem::path const& candidate, SecureWorkspaceResolveMode mode) const;
  [[nodiscard]] ava::core::Result<SecureWorkspaceHandle> open_regular_file(std::filesystem::path const& candidate) const;
  [[nodiscard]] ava::core::Result<SecureWorkspaceHandle> open_directory(std::filesystem::path const& candidate) const;
  [[nodiscard]] ava::core::Result<std::vector<SecureWorkspaceDirectoryEntry>> list_directory(std::filesystem::path const& candidate) const;
  [[nodiscard]] ava::core::VoidResult visit_tree(SecureWorkspaceWalkVisitor const& visitor) const;
  [[nodiscard]] ava::core::Result<StagedWrite> stage_write(std::filesystem::path const& candidate, std::string_view content,
                                                           std::function<bool()> const& cancel_requested = nullptr) const;
  [[nodiscard]] ava::core::Result<SecureWorkspaceWriteResult> write_file(std::filesystem::path const& candidate, std::string_view content,
                                                                         std::function<bool()> const& cancel_requested = nullptr) const;

 private:
  SecureWorkspace(int root_fd, std::filesystem::path root);

  int root_fd_ = -1;
  std::filesystem::path root_;
  AVA_DEBUG_PRINT_MEMBERS_OPT_OUT
};

}  // namespace ava::tools
