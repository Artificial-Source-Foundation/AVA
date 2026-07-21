#pragma once

#include "ava/debug/print_members_on.h"
#include "ava/core/result.h"

#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace ava::agent {

inline constexpr long long kSubagentJobContractVersion = 1;

enum class SubagentJobMode
{
  Foreground,
  Background,
};

enum class SubagentExecutionState
{
  Starting,
  Running,
  Completed,
  Failed,
  Canceled,
  Interrupted,
};

enum class SubagentDeliveryState
{
  Direct,
  Pending,
  Attempting,
  Acknowledged,
};

enum class SubagentTerminalState
{
  Completed,
  Failed,
  Canceled,
};

struct SubagentJobIdentity
{
  std::string job_id = {};
  std::string task_id = {};
  std::string parent_session_id = {};
  std::string child_session_id = {};
  std::string delivery_id = {};

  AVA_DEBUG_PRINT_MEMBERS_OPT_OUT
};

struct SubagentDeliveryAttemptSnapshot
{
  std::string attempt_id = {};
  std::string prompt_fingerprint = {};
  std::string attempted_at = {};

  AVA_DEBUG_PRINT_MEMBERS_OPT_OUT
};

// This is the stable backend DTO projected from the durable journal. Prompts,
// credentials, and filesystem paths do not belong in the journal contract;
// terminal delivery text is explicitly bounded and control-safe.
struct SubagentJobSnapshot
{
  long long schema_version = kSubagentJobContractVersion;
  SubagentJobIdentity identity = {};
  SubagentJobMode mode = SubagentJobMode::Foreground;
  SubagentExecutionState execution = SubagentExecutionState::Starting;
  SubagentDeliveryState delivery = SubagentDeliveryState::Direct;
  bool was_promoted = false;
  bool cancel_requested = false;
  std::size_t delivery_attempts = 0;
  std::vector<SubagentDeliveryAttemptSnapshot> delivery_attempt_history = {};
  std::string started_at = {};
  std::string updated_at = {};
  std::optional<std::string> promoted_at = std::nullopt;
  std::optional<std::string> cancel_requested_at = std::nullopt;
  std::optional<std::string> terminal_at = std::nullopt;
  std::optional<std::string> delivery_pending_at = std::nullopt;
  std::optional<std::string> last_delivery_attempt_at = std::nullopt;
  std::optional<std::string> delivery_acknowledged_at = std::nullopt;
  std::optional<std::string> summary = std::nullopt;
  std::optional<std::string> error_category = std::nullopt;
  std::optional<std::string> error = std::nullopt;
  std::optional<std::string> stop_reason = std::nullopt;
  bool summary_truncated = false;
  bool error_truncated = false;
  bool stop_reason_truncated = false;
  std::size_t provider_iterations = 0;
  std::size_t tool_calls = 0;
  std::size_t tool_iterations = 0;
  std::optional<std::string> acknowledged_attempt_id = std::nullopt;
  std::optional<std::string> committed_turn_id = std::nullopt;

  AVA_DEBUG_PRINT_MEMBERS_OPT_OUT
};

struct SubagentJobProjection
{
  long long schema_version = kSubagentJobContractVersion;
  std::vector<SubagentJobSnapshot> jobs;
  std::vector<std::string> pending_delivery_ids;

  [[nodiscard]] SubagentJobSnapshot const* find(std::string_view job_id) const noexcept;

  AVA_DEBUG_PRINT_MEMBERS_OPT_OUT
};

[[nodiscard]] std::string_view to_string(SubagentJobMode value) noexcept;
[[nodiscard]] std::string_view to_string(SubagentExecutionState value) noexcept;
[[nodiscard]] std::string_view to_string(SubagentDeliveryState value) noexcept;
[[nodiscard]] std::string_view to_string(SubagentTerminalState value) noexcept;
[[nodiscard]] ava::core::Result<SubagentJobMode> parse_subagent_job_mode(std::string_view value);
[[nodiscard]] ava::core::Result<SubagentTerminalState> parse_subagent_terminal_state(std::string_view value);

}  // namespace ava::agent
