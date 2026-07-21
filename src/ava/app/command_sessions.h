#pragma once

#include "ava/app/commands.h"

#include <cstddef>
#include <functional>
#include <string_view>

namespace ava::app {

// `/import` is intentionally bounded independently of legacy runtime reads:
// files are at most 8 MiB, records are strictly less than 1 MiB, and archives
// contain at most 16,384 entries.
inline constexpr std::size_t kMaxSessionImportFileBytes = 8U * 1024U * 1024U;
inline constexpr std::size_t kMaxSessionImportLineBytes = 1024U * 1024U;
inline constexpr std::size_t kMaxSessionImportEntries = 16384;

// Narrow deterministic race seam for descriptor-anchoring tests only.
void set_after_session_import_open_for_test(std::function<void()> hook);

[[nodiscard]] ava::core::Result<CommandResult> run_sessions_command(runtime::Session& session, std::string_view query = {});
[[nodiscard]] ava::core::Result<CommandResult> run_fork_command(runtime::Session& session, std::string_view name = {});
[[nodiscard]] ava::core::Result<CommandResult> run_clone_command(runtime::Session& session, std::string_view name = {});
[[nodiscard]] ava::core::Result<CommandResult> run_new_session_command(runtime::Session& session, std::string_view name = {});
[[nodiscard]] ava::core::Result<CommandResult> run_resume_command(runtime::Session& session, std::string_view session_id);
[[nodiscard]] ava::core::Result<CommandResult> run_name_command(runtime::Session& session, std::string_view name);
[[nodiscard]] ava::core::Result<CommandResult> run_labels_command(runtime::Session& session, std::string_view labels);
[[nodiscard]] ava::core::Result<CommandResult> run_mode_command(runtime::Session& session);
[[nodiscard]] ava::core::Result<CommandResult> run_context_command(runtime::Session& session, std::string_view query = {});
[[nodiscard]] ava::core::Result<CommandResult> run_stats_command(runtime::Session& session);
[[nodiscard]] ava::core::Result<CommandResult> run_compact_command(runtime::Session& session, CommandRequest const& request);
[[nodiscard]] ava::core::Result<CommandResult> run_import_command(runtime::Session& session, std::string_view argument);
[[nodiscard]] ava::core::Result<CommandResult> run_export_command(runtime::Session& session, CommandRequest const& request);
// Explicit fail-closed recovery after a verified terminal/drained append failure.
[[nodiscard]] ava::core::Result<CommandResult> run_recover_persistence_command(runtime::Session& session);

}  // namespace ava::app
