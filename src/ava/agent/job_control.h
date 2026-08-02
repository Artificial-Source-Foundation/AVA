#pragma once

#include "ava/agent/subagent_coordinator.h"
#include "ava/core/error.h"

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

namespace ava::agent {

inline constexpr std::size_t kMaxPublicJobIdBytes = 96;
inline constexpr std::size_t kMaxPublicJobListEntries = 64;
inline constexpr long long kDefaultPublicJobWaitTimeoutMs = 1000;
inline constexpr long long kMaxPublicJobWaitTimeoutMs = 30000;

enum class PublicJobContent
{
  OmitTerminalContent,
  IncludeTerminalResult,
};

// Public, protocol-neutral job DTO serialization. These functions are the only
// model-tool/RPC serialization boundary for coordinator state. Interactive
// /jobs human text is formatted separately in command_jobs and must not change
// these schemas. They never expose paths, coordinator diagnostics, prompts,
// provider/tool context, or process-local display_title/display_subagent_type.
[[nodiscard]] std::string public_job_snapshot_json(SubagentCoordinatorJobSnapshot const& snapshot,
                                                   PublicJobContent content = PublicJobContent::OmitTerminalContent);
[[nodiscard]] std::string public_job_list_json(std::vector<SubagentCoordinatorJobSnapshot> const& snapshots);
[[nodiscard]] std::string format_public_job_snapshot(SubagentCoordinatorJobSnapshot const& snapshot,
                                                     PublicJobContent content = PublicJobContent::OmitTerminalContent);
[[nodiscard]] std::string format_public_job_list(std::vector<SubagentCoordinatorJobSnapshot> const& snapshots);

// Stable failure data safe for process-local job snapshots and the durable child session. The source
// error's formatted context is deliberately not retained.
[[nodiscard]] std::string safe_subagent_error_category(ava::core::Error const& error);
[[nodiscard]] std::string safe_subagent_error_message(ava::core::Error const& error);

}  // namespace ava::agent
