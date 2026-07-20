#pragma once

#include "ava/core/error.h"

#include <optional>
#include <string>
#include <string_view>
#include "debug.h"

namespace ava::diagnostics {

enum class ComponentClass
{
  Mcp,
  Plugin,
};

enum class FailureCategory
{
  Configuration,
  Transport,
  Authorization,
  Protocol,
  Cancellation,
  Internal,
};

enum class FailureCode
{
  InvalidRequest,
  IoFailure,
  NotFound,
  PermissionDenied,
  ExternalFailure,
  Canceled,
  InternalFailure,
};

enum class Retryability
{
  Never,
  AfterUserAction,
  Transient,
};

enum class RecoveryHint
{
  VerifyRequest,
  VerifyConfiguration,
  VerifyPermissions,
  RetryOperation,
  ContactSupport,
};

struct SafeFailure
{
  ComponentClass component = ComponentClass::Plugin;
  FailureCategory category = FailureCategory::Internal;
  FailureCode code = FailureCode::InternalFailure;
  Retryability retryability = Retryability::Never;
  RecoveryHint recovery_hint = RecoveryHint::ContactSupport;

  AVA_DEBUG_PRINT_MEMBERS_ON
};

[[nodiscard]] std::string_view to_string(ComponentClass component) noexcept;
[[nodiscard]] std::string_view to_string(FailureCategory category) noexcept;
[[nodiscard]] std::string_view to_string(FailureCode code) noexcept;
[[nodiscard]] std::string_view to_string(Retryability retryability) noexcept;
[[nodiscard]] std::string_view to_string(RecoveryHint recovery_hint) noexcept;

// This is the only adapter from the unrestricted core Error type. It is
// intentionally category-only: Error message and context data are untrusted at
// support/public boundaries.
[[nodiscard]] SafeFailure safe_failure_from_error(ComponentClass component, ava::core::Error const& error) noexcept;
[[nodiscard]] SafeFailure external_failure(ComponentClass component) noexcept;
[[nodiscard]] SafeFailure canceled_failure(ComponentClass component) noexcept;

// AVA-generated integration names are the compatibility proof used for
// historical failed tool results. Unknown and unrelated tool names are not
// rewritten; once an mcp_/plugin_ identity is proven, failed content is always
// replaced rather than parsed or preserved.
[[nodiscard]] std::optional<ComponentClass> external_tool_component(std::string_view tool_name) noexcept;

// Stable public serializers accept SafeFailure only. They have no extension
// point for caller-controlled strings, paths, identifiers, or remote payloads.
[[nodiscard]] std::string serialize_safe_failure_json(SafeFailure const& failure);
[[nodiscard]] std::string serialize_safe_failure_human(SafeFailure const& failure);

}  // namespace ava::diagnostics
