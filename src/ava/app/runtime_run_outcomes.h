#pragma once

#include <optional>

namespace ava::core {
class Error;
enum class RuntimeTerminalOutcome : int;
}  // namespace ava::core

namespace ava::diagnostics {
enum class RuntimeFailureClass : int;
}  // namespace ava::diagnostics

namespace ava::app {

enum class StopReason : int;

[[nodiscard]] bool is_agent_loop_canceled_error(ava::core::Error const& error);
[[nodiscard]] StopReason outcome_reason_for_error(ava::core::Error const& error);
[[nodiscard]] std::optional<ava::diagnostics::RuntimeFailureClass> diagnostic_failure_class(ava::core::Error const& error) noexcept;
[[nodiscard]] ava::core::RuntimeTerminalOutcome runtime_outcome_for_stop_reason(StopReason reason) noexcept;
[[nodiscard]] StopReason stop_reason_for_runtime_outcome(ava::core::RuntimeTerminalOutcome outcome) noexcept;

}  // namespace ava::app
