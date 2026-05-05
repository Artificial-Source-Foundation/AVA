#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>

#include "ava/app/runtime.h"
#include "ava/session/stats.h"

namespace ava::app::line_shell::detail {

[[nodiscard]] std::string git_branch_for_workspace(std::filesystem::path const& workspace);
[[nodiscard]] bool is_compact_command(std::string_view line) noexcept;
[[nodiscard]] std::optional<long long> compact_token_total(ava::session::SessionStats const& stats);
[[nodiscard]] std::string format_compact_token_count(long long value);
[[nodiscard]] std::optional<std::string> format_context_window_percent(long long tokens,
                                                                       std::optional<long long> context_window_tokens);
[[nodiscard]] std::optional<std::string> compact_token_status(ava::session::SessionStats const& stats,
                                                              std::optional<long long> context_window_tokens);
[[nodiscard]] std::optional<std::string> token_status_for_session(ava::app::RuntimeSession const& session);

}  // namespace ava::app::line_shell::detail
