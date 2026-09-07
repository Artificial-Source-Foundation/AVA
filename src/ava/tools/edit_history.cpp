#include "sys.h"
#include "ava/tools/diff_utils.h"
#include "ava/tools/edit_history.h"
#include "ava/tools/file_io.h"
#include "ava/tools/mutation_queue.h"
#include "ava/tools/secure_workspace.h"
#include "ava/core/stat_time.h"

#include <algorithm>
#include <array>
#include <cerrno>
#include <fcntl.h>
#include <unistd.h>

namespace ava::tools {
namespace {
constexpr std::size_t kMaxFileBytes = std::size_t{1024} * 1024;
constexpr std::size_t kMaxJournalBytes = std::size_t{8} * 1024 * 1024;
constexpr std::size_t kMaxFiles = 32;

auto history_error(std::string message, std::filesystem::path const& path = {}) -> ava::core::Error
{
  auto error = ava::core::Error(ava::core::ErrorCategory::InvalidArgument, std::move(message));
  if (!path.empty())
  {
    error.with_context("path", path.generic_string());
  }
  return error;
}

auto same_identity(struct stat const& first, struct stat const& second) -> bool
{
  auto const first_time = ava::core::stat_modification_time(first);
  auto const second_time = ava::core::stat_modification_time(second);
  auto const first_change = ava::core::stat_change_time(first);
  auto const second_change = ava::core::stat_change_time(second);
  return first.st_dev == second.st_dev && first.st_ino == second.st_ino && first.st_size == second.st_size && first.st_mode == second.st_mode &&
         first.st_uid == second.st_uid && first.st_gid == second.st_gid && first.st_nlink == second.st_nlink && first_time.tv_sec == second_time.tv_sec &&
         first_time.tv_nsec == second_time.tv_nsec && first_change.tv_sec == second_change.tv_sec && first_change.tv_nsec == second_change.tv_nsec;
}

auto same_state(EditFileState const& first, EditFileState const& second) -> bool
{
  return first.exists == second.exists && (!first.exists || (same_identity(first.identity, second.identity) && first.content == second.content));
}

auto remove_unchanged_file(SecureWorkspace const& workspace, std::filesystem::path const& path, struct stat const& expected) -> ava::core::VoidResult
{
  auto parent = workspace.open_directory(path.parent_path());
  struct stat target{};
  if (!parent || ::fstatat(parent->fd(), path.filename().c_str(), &target, AT_SYMLINK_NOFOLLOW) != 0 || !same_identity(target, expected) ||
      ::unlinkat(parent->fd(), path.filename().c_str(), 0) != 0)
  {
    return std::unexpected(history_error("Undo could not remove a captured new file; review the workspace for partial restoration.", path));
  }
  return {};
}

auto history_workspace(ToolContext const& context) -> ava::core::Result<std::shared_ptr<SecureWorkspace>>
{
  if (context.secure_workspace)
  {
    return context.secure_workspace;
  }
  if (context.anchor_set)
  {
    auto anchor = context.anchor_set->find_anchor(context.workspace_dir);
    if (!anchor || !anchor->relative().empty())
    {
      return std::unexpected(history_error("undo requires the original workspace anchor"));
    }
    struct stat opened{};
    struct stat named{};
    if (::fstat(anchor->anchor().fd, &opened) != 0 || ::stat(context.workspace_dir.c_str(), &named) != 0 || opened.st_dev != named.st_dev ||
        opened.st_ino != named.st_ino)
    {
      return std::unexpected(history_error("undo workspace pathname no longer matches its original anchor"));
    }
    return SecureWorkspace::from_directory_fd(anchor->anchor().fd, anchor->anchor().root);
  }
  return SecureWorkspace::open(context.workspace_dir);
}

auto ensure_undo_permissions(ToolContext const& context, std::vector<std::filesystem::path> const& paths) -> ava::core::VoidResult
{
  for (auto const& path : paths)
  {
    auto permission = ensure_permission(context, ava::permissions::Operation::EditFile, path, "", "undo", "user-requested file undo");
    if (!permission)
    {
      return std::unexpected(std::move(permission.error()));
    }
  }
  return {};
}
}  // namespace

auto EditHistory::capture(ToolContext const& context, std::filesystem::path const& path) -> ava::core::Result<EditFileState>
{
  if (context.exact_file_access)
  {
    return std::unexpected(history_error("undo is unavailable for external file adapters"));
  }
  auto workspace = history_workspace(context);
  if (!workspace)
  {
    return std::unexpected(std::move(workspace.error()));
  }
  auto resolved = (*workspace)->resolve(path, SecureWorkspaceResolveMode::AllowMissing);
  if (!resolved)
  {
    return std::unexpected(std::move(resolved.error()));
  }
  if (!resolved->exists)
  {
    return EditFileState{};
  }
  auto readable = can_read_file_for_edit_snapshot(context, resolved->absolute);
  if (!readable || !*readable)
  {
    return std::unexpected(history_error("undo cannot capture a file without current read permission", path));
  }
  auto file = (*workspace)->open_regular_file(path);
  if (!file)
  {
    return std::unexpected(std::move(file.error()));
  }
  auto parent = (*workspace)->open_directory(resolved->absolute.parent_path());
  struct stat named{};
  if (!parent || ::fstatat(parent->fd(), resolved->absolute.filename().c_str(), &named, AT_SYMLINK_NOFOLLOW) != 0 || !S_ISREG(named.st_mode))
  {
    return std::unexpected(history_error("undo requires a regular file, not a symbolic link", path));
  }
  EditFileState state;
  state.exists = true;
  if (::fstat(file->fd(), &state.identity) != 0 || !same_identity(named, state.identity) || state.identity.st_nlink != 1 || file->size() > kMaxFileBytes)
  {
    return std::unexpected(history_error("undo requires a singly linked regular file of at most 1 MiB", path));
  }
  std::array<char, 8192> buffer{};
  while (true)
  {
    auto const count = ::read(file->fd(), buffer.data(), buffer.size());
    if (count == 0)
    {
      break;
    }
    if (count < 0)
    {
      if (errno == EINTR)
      {
        continue;
      }
      return std::unexpected(history_error("could not capture file for undo", path));
    }
    if (state.content.size() + static_cast<std::size_t>(count) > kMaxFileBytes)
    {
      return std::unexpected(history_error("file grew beyond undo limit", path));
    }
    state.content.append(buffer.data(), static_cast<std::size_t>(count));
  }
  struct stat after{};
  if (::fstat(file->fd(), &after) != 0 || !same_identity(state.identity, after))
  {
    return std::unexpected(history_error("file changed while capturing undo state", path));
  }
  if (state.content.contains('\0'))
  {
    return std::unexpected(history_error("undo currently supports text files only", path));
  }
  return state;
}

void EditHistory::select_turn(std::string_view turn)
{
  if (turn_ == turn)
  {
    return;
  }
  turn_ = turn;
  edits_.clear();
  unavailable_.clear();
  preview_generation_.reset();
  ++generation_;
}

void EditHistory::invalidate(std::string_view turn, std::string reason)
{
  std::scoped_lock lock(mutex_);
  select_turn(turn);
  unavailable_ = std::move(reason);
  preview_generation_.reset();
}

void EditHistory::record(ToolContext const& context, std::filesystem::path const& path, ava::core::Result<EditFileState> before,
                         std::string_view expected_after)
{
  auto after = capture(context, path);
  std::scoped_lock lock(mutex_);
  select_turn(context.edit_turn_id);
  preview_generation_.reset();
  ++generation_;
  if (!before || !after || after->content != expected_after ||
      (before->exists && (before->identity.st_uid != after->identity.st_uid || before->identity.st_gid != after->identity.st_gid ||
                          before->identity.st_mode != after->identity.st_mode)))
  {
    unavailable_ = "Undo unavailable: this turn contains an edit that could not be captured safely.";
    return;
  }
  auto existing = std::ranges::find(edits_, path, &Edit::path);
  if (existing != edits_.end())
  {
    if (!same_state(existing->after, *before))
    {
      unavailable_ = "Undo unavailable: a file changed outside AVA between edits in this turn.";
    }
    existing->after = std::move(*after);
  }
  else
  {
    edits_.push_back({.path = path, .before = std::move(*before), .after = std::move(*after)});
  }
  std::size_t bytes = 0;
  for (auto const& edit : edits_)
  {
    bytes += edit.before.content.size() + edit.after.content.size();
  }
  if (bytes > kMaxJournalBytes || edits_.size() > kMaxFiles)
  {
    edits_.clear();
    unavailable_ = "Undo unavailable: this turn exceeded the 32-file / 8 MiB undo budget.";
  }
}

auto EditHistory::validate(ToolContext const& context) const -> ava::core::VoidResult
{
  if (!unavailable_.empty())
  {
    return std::unexpected(history_error(unavailable_));
  }
  if (edits_.empty())
  {
    return std::unexpected(history_error("No captured editing turn to undo in this process."));
  }
  for (auto const& edit : edits_)
  {
    auto current = capture(context, edit.path);
    if (!current || !same_state(*current, edit.after))
    {
      return std::unexpected(history_error("Undo refused: file changed after AVA's edit; no files restored", edit.path));
    }
  }
  return {};
}

auto EditHistory::preview(ToolContext const& context) -> ava::core::Result<std::string>
{
  std::scoped_lock lock(mutex_);
  preview_generation_.reset();
  if (auto valid = validate(context); !valid)
  {
    return std::unexpected(std::move(valid.error()));
  }
  std::string text = "Undo preview: last AVA editing turn (" + std::to_string(edits_.size()) + " files)\n";
  for (auto const& edit : edits_)
  {
    auto diff = unified_diff(edit.after.content, edit.before.content, edit.path, edit.before.exists ? edit.path : std::filesystem::path("/dev/null"), 8192);
    text += "\n" + diff.text;
    if (diff.truncated)
    {
      text += "\n[diff preview truncated]\n";
    }
  }
  text += "\nOnly captured AVA file edits will be restored. Shell effects and conversation history remain.\nRun /undo --confirm to apply this preview.";
  preview_generation_ = generation_;
  return text;
}

auto EditHistory::undo(ToolContext const& context) -> ava::core::Result<std::string>
{
  std::scoped_lock lock(mutex_);
  if (!preview_generation_ || *preview_generation_ != generation_)
  {
    return std::unexpected(history_error("Run /undo to review the current changes before confirming."));
  }
  preview_generation_.reset();
  // Approval is still owned by the existing file permission backend. An undo
  // preview is never a reusable grant or a replacement for current policy.
  std::vector<std::filesystem::path> paths;
  paths.reserve(edits_.size());
  for (auto const& edit : edits_)
  {
    paths.push_back(edit.path);
  }
  if (auto permitted = ensure_undo_permissions(context, paths); !permitted)
  {
    return std::unexpected(std::move(permitted.error()));
  }
  auto queue = context.mutation_queue ? context.mutation_queue : default_mutation_queue();
  [[maybe_unused]] auto mutation_lock = queue->lock_paths(paths);
  if (auto valid = validate(context); !valid)
  {
    return std::unexpected(std::move(valid.error()));
  }
  auto workspace = history_workspace(context);
  if (!workspace)
  {
    return std::unexpected(std::move(workspace.error()));
  }
  std::vector<std::optional<SecureWorkspace::StagedWrite>> staged;
  for (auto const& edit : edits_)
  {
    if (!edit.before.exists)
    {
      staged.emplace_back();
      continue;
    }
    auto write = (*workspace)->stage_write(edit.path, edit.before.content, context.cancel_requested);
    if (!write)
    {
      return std::unexpected(std::move(write.error()));
    }
    staged.emplace_back(std::move(*write));
  }
  std::size_t restored = 0;
  for (std::size_t index = 0; index < edits_.size(); ++index)
  {
    auto const& edit = edits_.at(index);
    auto current = capture(context, edit.path);
    if (!current || !same_state(*current, edit.after) || detail::is_canceled(context))
    {
      unavailable_ = "Undo interrupted by a file change or cancellation after restoring " + std::to_string(restored) + " files. Review the workspace.";
      return std::unexpected(history_error(unavailable_, edit.path));
    }
    auto& staged_write = staged.at(index);
    if (staged_write)
    {
      if (auto applied = staged_write->commit_if_unchanged(edit.after.identity); !applied)
      {
        unavailable_ = "Undo could not finish; some files may have been restored. Review the workspace.";
        return std::unexpected(history_error(unavailable_ + " " + applied.error().format(), edit.path));
      }
    }
    else
    {
      if (auto removed = remove_unchanged_file(**workspace, edit.path, edit.after.identity); !removed)
      {
        unavailable_ = "Undo could not remove a captured new file; review the workspace for partial restoration.";
        return std::unexpected(history_error(unavailable_, edit.path));
      }
    }
    ++restored;
  }
  edits_.clear();
  ++generation_;
  return "Restored " + std::to_string(restored) + " files from the last AVA editing turn. Conversation history and shell effects were preserved.";
}

auto write_file_recorded(ToolContext const& context, std::filesystem::path const& path, std::string_view content) -> ava::core::Result<FileMutationResult>
{
  if (!context.edit_history || context.edit_turn_id.empty())
  {
    return detail::write_file_unlocked(context, path, content);
  }
  auto before = EditHistory::capture(context, path);
  auto written = detail::write_file_unlocked(context, path, content);
  if (written)
  {
    context.edit_history->record(context, path, std::move(before), content);
  }
  else
  {
    context.edit_history->invalidate(context.edit_turn_id, "Undo unavailable after an incomplete file write.");
  }
  return written;
}
}  // namespace ava::tools
