#ifndef AVA_TOOLS_EDIT_HISTORY_H
#define AVA_TOOLS_EDIT_HISTORY_H

#include "ava/debug/print_members_on.h"
#include "ava/core/result.h"

#include <cstdint>
#include <filesystem>
#include <mutex>
#include <optional>
#include <string>
#include <vector>
#include <sys/stat.h>

namespace ava::tools {
struct ToolContext;
struct FileMutationResult;

struct EditFileState
{
  bool exists = false;
  std::string content;
  struct stat identity{};
  AVA_DEBUG_PRINT_MEMBERS_OPT_OUT
};

// Session-owned, process-local last editing turn. Copies in tool workers share
// this bounded journal. File contents must never enter generated debug output.
class EditHistory
{
 public:
  [[nodiscard]] static auto capture(ToolContext const& context, std::filesystem::path const& path) -> ava::core::Result<EditFileState>;
  void record(ToolContext const& context, std::filesystem::path const& path, ava::core::Result<EditFileState> before, std::string_view expected_after);
  void invalidate(std::string_view turn, std::string reason);
  [[nodiscard]] auto preview(ToolContext const& context) -> ava::core::Result<std::string>;
  [[nodiscard]] auto undo(ToolContext const& context) -> ava::core::Result<std::string>;
  AVA_DEBUG_PRINT_MEMBERS_OPT_OUT

 private:
  struct Edit
  {
    std::filesystem::path path;
    EditFileState before;
    EditFileState after;
    AVA_DEBUG_PRINT_MEMBERS_OPT_OUT
  };
  void select_turn(std::string_view turn);
  [[nodiscard]] auto validate(ToolContext const& context) const -> ava::core::VoidResult;
  mutable std::mutex mutex_;
  std::string turn_;
  std::string unavailable_;
  std::vector<Edit> edits_;
  std::uint64_t generation_ = 0;
  std::optional<std::uint64_t> preview_generation_;
};

[[nodiscard]] auto write_file_recorded(ToolContext const& context, std::filesystem::path const& path, std::string_view content)
    -> ava::core::Result<FileMutationResult>;
}  // namespace ava::tools
#endif
