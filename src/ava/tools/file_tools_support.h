#pragma once

#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <string_view>

#include "ava/core/error.h"
#include "ava/core/result.h"
#include "ava/tools/file_tools.h"
#include "ava/tools/mutation_queue.h"

namespace ava::tools::detail {

inline constexpr std::size_t kMaxPermissionDiffBytes = 32 * 1024;

struct PermissionDiffPreview {
  std::string text;
  bool truncated = false;
};

[[nodiscard]] bool is_canceled(ToolContext const& context);
[[nodiscard]] ava::core::Error canceled_error(std::string_view operation, std::filesystem::path const& path);
[[nodiscard]] ava::core::VoidResult check_canceled(ToolContext const& context, std::string_view operation,
                                                   std::filesystem::path const& path = {});
[[nodiscard]] bool is_canceled_error(ava::core::Error const& error);
[[nodiscard]] ava::core::Result<std::string> read_all_text(ToolContext const& context,
                                                           std::filesystem::path const& path,
                                                           std::string_view operation);
[[nodiscard]] ava::core::Result<TextOutput> read_head_text(ToolContext const& context,
                                                           std::filesystem::path const& path, std::size_t max_bytes);
[[nodiscard]] std::filesystem::path write_parent_path(std::filesystem::path const& path);
[[nodiscard]] std::filesystem::path unique_write_temp_path(std::filesystem::path const& target);
[[nodiscard]] ava::core::Error io_error(std::string message, std::filesystem::path const& path, std::string cause);
[[nodiscard]] ava::core::Error staged_io_error(std::string message, std::filesystem::path const& target_path,
                                               std::filesystem::path const& temp_path, std::string cause);
[[nodiscard]] std::string errno_cause(int value);
[[nodiscard]] std::shared_ptr<MutationQueue> effective_mutation_queue(ToolContext const& context);
[[nodiscard]] ava::core::Result<std::optional<PermissionDiffPreview>> write_permission_diff_preview(
    ToolContext const& context, std::filesystem::path const& path, std::string_view content);
[[nodiscard]] ava::core::Result<FileMutationResult> write_file_unlocked(ToolContext const& context,
                                                                        std::filesystem::path const& path,
                                                                        std::string_view content);

}  // namespace ava::tools::detail
