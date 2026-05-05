#pragma once

#include "ava/tools/file_tools.h"

#include "ava/core/result.h"

#include <cstddef>
#include <filesystem>
#include <string>
#include <string_view>

namespace ava::tools::detail {

[[nodiscard]] bool is_canceled(ToolContext const& context);
[[nodiscard]] ava::core::Error canceled_error(std::string_view operation, std::filesystem::path const& path);
[[nodiscard]] ava::core::VoidResult check_canceled(ToolContext const& context, std::string_view operation,
                                                   std::filesystem::path const& path = {});
[[nodiscard]] bool is_canceled_error(ava::core::Error const& error);
[[nodiscard]] ava::core::Result<std::string> read_all_text(ToolContext const& context,
                                                           std::filesystem::path const& path,
                                                           std::string_view operation);
[[nodiscard]] ava::core::Result<TextOutput> read_head_text(ToolContext const& context,
                                                           std::filesystem::path const& path, ReadOptions options);
[[nodiscard]] ava::core::Result<FileMutationResult> write_file_unlocked(ToolContext const& context,
                                                                        std::filesystem::path const& path,
                                                                        std::string_view content);

}  // namespace ava::tools::detail
