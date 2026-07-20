#include "sys.h"
#include "ava/diagnostics/safe_failure.h"

#include <string>

namespace ava::diagnostics {
namespace {

SafeFailure make_failure(ComponentClass component, FailureCategory category, FailureCode code, Retryability retryability, RecoveryHint recovery_hint) noexcept
{
  return SafeFailure{.component = component, .category = category, .code = code, .retryability = retryability, .recovery_hint = recovery_hint};
}

std::string_view component_label(ComponentClass component) noexcept
{
  switch (component)
  {
    case ComponentClass::Mcp:
      return "MCP";
    case ComponentClass::Plugin:
      return "Plugin";
    case ComponentClass::App:
      return "Application";
    case ComponentClass::Storage:
      return "Storage";
    case ComponentClass::Configuration:
      return "Configuration";
    case ComponentClass::Provider:
      return "Provider";
    case ComponentClass::Session:
      return "Session";
    case ComponentClass::Tool:
      return "Tool";
    case ComponentClass::Runtime:
      return "Runtime";
  }
  return "Integration";
}

std::string_view fixed_message(FailureCode code) noexcept
{
  switch (code)
  {
    case FailureCode::InvalidRequest:
      return "integration request was rejected";
    case FailureCode::IoFailure:
      return "integration transport is unavailable";
    case FailureCode::NotFound:
      return "integration executable or configuration was not found";
    case FailureCode::PermissionDenied:
      return "integration access was denied";
    case FailureCode::ExternalFailure:
      return "integration operation failed";
    case FailureCode::Canceled:
      return "integration operation was canceled";
    case FailureCode::InternalFailure:
      return "integration failed safely";
  }
  return "integration failed safely";
}

}  // namespace

std::string_view to_string(ComponentClass component) noexcept
{
  switch (component)
  {
    case ComponentClass::Mcp:
      return "mcp";
    case ComponentClass::Plugin:
      return "plugin";
    case ComponentClass::App:
      return "app";
    case ComponentClass::Storage:
      return "storage";
    case ComponentClass::Configuration:
      return "configuration";
    case ComponentClass::Provider:
      return "provider";
    case ComponentClass::Session:
      return "session";
    case ComponentClass::Tool:
      return "tool";
    case ComponentClass::Runtime:
      return "runtime";
  }
  return "plugin";
}

std::string_view to_string(FailureCategory category) noexcept
{
  switch (category)
  {
    case FailureCategory::Configuration:
      return "configuration";
    case FailureCategory::Transport:
      return "transport";
    case FailureCategory::Authorization:
      return "authorization";
    case FailureCategory::Protocol:
      return "protocol";
    case FailureCategory::Cancellation:
      return "cancellation";
    case FailureCategory::Internal:
      return "internal";
  }
  return "internal";
}

std::string_view to_string(FailureCode code) noexcept
{
  switch (code)
  {
    case FailureCode::InvalidRequest:
      return "invalid_request";
    case FailureCode::IoFailure:
      return "io_failure";
    case FailureCode::NotFound:
      return "not_found";
    case FailureCode::PermissionDenied:
      return "permission_denied";
    case FailureCode::ExternalFailure:
      return "external_failure";
    case FailureCode::Canceled:
      return "canceled";
    case FailureCode::InternalFailure:
      return "internal_failure";
  }
  return "internal_failure";
}

std::string_view to_string(Retryability retryability) noexcept
{
  switch (retryability)
  {
    case Retryability::Never:
      return "never";
    case Retryability::AfterUserAction:
      return "after_user_action";
    case Retryability::Transient:
      return "transient";
  }
  return "never";
}

std::string_view to_string(RecoveryHint recovery_hint) noexcept
{
  switch (recovery_hint)
  {
    case RecoveryHint::VerifyRequest:
      return "Verify the request before trying again.";
    case RecoveryHint::VerifyConfiguration:
      return "Verify the integration configuration before trying again.";
    case RecoveryHint::VerifyPermissions:
      return "Review integration permissions before trying again.";
    case RecoveryHint::RetryOperation:
      return "Retry the operation once; if it fails again, review the integration configuration.";
    case RecoveryHint::ContactSupport:
      return "If the failure continues, contact support with the stable failure code.";
  }
  return "If the failure continues, contact support with the stable failure code.";
}

SafeFailure safe_failure_from_error(ComponentClass component, ava::core::Error const& error) noexcept
{
  switch (error.category())
  {
    case ava::core::ErrorCategory::InvalidArgument:
      return make_failure(component, FailureCategory::Configuration, FailureCode::InvalidRequest, Retryability::AfterUserAction, RecoveryHint::VerifyRequest);
    case ava::core::ErrorCategory::Io:
      return make_failure(component, FailureCategory::Transport, FailureCode::IoFailure, Retryability::Transient, RecoveryHint::RetryOperation);
    case ava::core::ErrorCategory::NotFound:
      return make_failure(component, FailureCategory::Configuration, FailureCode::NotFound, Retryability::AfterUserAction, RecoveryHint::VerifyConfiguration);
    case ava::core::ErrorCategory::PermissionDenied:
      return make_failure(component, FailureCategory::Authorization, FailureCode::PermissionDenied, Retryability::AfterUserAction,
                          RecoveryHint::VerifyPermissions);
    case ava::core::ErrorCategory::Provider:
    case ava::core::ErrorCategory::Session:
    case ava::core::ErrorCategory::Tool:
      return external_failure(component);
    case ava::core::ErrorCategory::Unknown:
      return make_failure(component, FailureCategory::Internal, FailureCode::InternalFailure, Retryability::Never, RecoveryHint::ContactSupport);
  }
  return make_failure(component, FailureCategory::Internal, FailureCode::InternalFailure, Retryability::Never, RecoveryHint::ContactSupport);
}

SafeFailure external_failure(ComponentClass component) noexcept
{
  return make_failure(component, FailureCategory::Protocol, FailureCode::ExternalFailure, Retryability::AfterUserAction, RecoveryHint::VerifyConfiguration);
}

SafeFailure canceled_failure(ComponentClass component) noexcept
{
  return make_failure(component, FailureCategory::Cancellation, FailureCode::Canceled, Retryability::AfterUserAction, RecoveryHint::RetryOperation);
}

std::optional<ComponentClass> external_tool_component(std::string_view tool_name) noexcept
{
  if (tool_name.starts_with("mcp_"))
    return ComponentClass::Mcp;
  if (tool_name.starts_with("plugin_"))
    return ComponentClass::Plugin;
  return std::nullopt;
}

std::string serialize_safe_failure_json(SafeFailure const& failure)
{
  return "{\"component\":\"" + std::string(to_string(failure.component)) + "\",\"category\":\"" + std::string(to_string(failure.category)) + "\",\"code\":\"" +
         std::string(to_string(failure.code)) + "\",\"retryability\":\"" + std::string(to_string(failure.retryability)) + "\",\"recovery_hint\":\"" +
         std::string(to_string(failure.recovery_hint)) + "\"}";
}

std::string serialize_safe_failure_human(SafeFailure const& failure)
{
  return std::string(component_label(failure.component)) + " " + std::string(fixed_message(failure.code)) + " [" + std::string(to_string(failure.code)) +
         "]. " + std::string(to_string(failure.recovery_hint));
}

}  // namespace ava::diagnostics
