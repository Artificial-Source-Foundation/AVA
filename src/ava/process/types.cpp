#include "sys.h"
#include "ava/process/types.h"

#include <string_view>

namespace ava::process {
namespace {

template <typename Enum>
std::string_view invalid_enum(Enum) noexcept
{
  return "invalid";
}

}  // namespace

std::string_view to_string(ProcessRoleV1 value) noexcept
{
  switch (value)
  {
    case ProcessRoleV1::Curl:
      return "curl";
    case ProcessRoleV1::Bash:
      return "bash";
    case ProcessRoleV1::Plugin:
      return "plugin";
    case ProcessRoleV1::Mcp:
      return "mcp";
    case ProcessRoleV1::Lsp:
      return "lsp";
    case ProcessRoleV1::Mermaid:
      return "mermaid";
    case ProcessRoleV1::BrowserOpener:
      return "browser_opener";
    case ProcessRoleV1::ClipboardHelper:
      return "clipboard_helper";
    case ProcessRoleV1::ExternalEditor:
      return "external_editor";
  }
  return invalid_enum(value);
}

std::string_view to_string(ProcessStateV1 value) noexcept
{
  switch (value)
  {
    case ProcessStateV1::Reserved:
      return "reserved";
    case ProcessStateV1::Launching:
      return "launching";
    case ProcessStateV1::Running:
      return "running";
    case ProcessStateV1::StopRequested:
      return "stop_requested";
    case ProcessStateV1::Reaping:
      return "reaping";
    case ProcessStateV1::Finished:
      return "finished";
  }
  return invalid_enum(value);
}

std::string_view to_string(TerminationReasonV1 value) noexcept
{
  switch (value)
  {
    case TerminationReasonV1::NaturalExit:
      return "natural_exit";
    case TerminationReasonV1::LaunchFailed:
      return "launch_failed";
    case TerminationReasonV1::ExecFailed:
      return "exec_failed";
    case TerminationReasonV1::Canceled:
      return "canceled";
    case TerminationReasonV1::DeadlineExpired:
      return "deadline_expired";
    case TerminationReasonV1::OwnerShutdown:
      return "owner_shutdown";
    case TerminationReasonV1::ApplicationShutdown:
      return "application_shutdown";
    case TerminationReasonV1::OutputLimit:
      return "output_limit";
    case TerminationReasonV1::ProtocolFailure:
      return "protocol_failure";
    case TerminationReasonV1::UnsupportedSuspension:
      return "unsupported_suspension";
  }
  return invalid_enum(value);
}

std::string_view to_string(CleanupStateV1 value) noexcept
{
  switch (value)
  {
    case CleanupStateV1::NotRequired:
      return "not_required";
    case CleanupStateV1::Pending:
      return "pending";
    case CleanupStateV1::Complete:
      return "complete";
    case CleanupStateV1::Incomplete:
      return "incomplete";
  }
  return invalid_enum(value);
}

std::string_view to_string(ExitKindV1 value) noexcept
{
  switch (value)
  {
    case ExitKindV1::None:
      return "none";
    case ExitKindV1::Exited:
      return "exited";
    case ExitKindV1::Signaled:
      return "signaled";
    case ExitKindV1::LaunchError:
      return "launch_error";
    case ExitKindV1::CleanupIncomplete:
      return "cleanup_incomplete";
  }
  return invalid_enum(value);
}

std::string_view to_string(StreamModeV1 value) noexcept
{
  switch (value)
  {
    case StreamModeV1::Capture:
      return "capture";
    case StreamModeV1::Inherit:
      return "inherit";
    case StreamModeV1::Discard:
      return "discard";
  }
  return invalid_enum(value);
}

std::string_view to_string(StreamKindV1 value) noexcept
{
  switch (value)
  {
    case StreamKindV1::StandardInput:
      return "stdin";
    case StreamKindV1::StandardOutput:
      return "stdout";
    case StreamKindV1::StandardError:
      return "stderr";
  }
  return invalid_enum(value);
}

std::string_view to_string(ChildMemberV1 value) noexcept
{
  switch (value)
  {
    case ChildMemberV1::Leader:
      return "leader";
    case ChildMemberV1::Sentinel:
      return "sentinel";
  }
  return invalid_enum(value);
}

std::string_view to_string(PipeInterestV1 value) noexcept
{
  switch (value)
  {
    case PipeInterestV1::Readable:
      return "readable";
    case PipeInterestV1::Writable:
      return "writable";
  }
  return invalid_enum(value);
}

PlatformSupportV1 platform_support_v1() noexcept
{
#if defined(_WIN32)
  return PlatformSupportV1::Unsupported;
#else
  return PlatformSupportV1::Posix;
#endif
}

}  // namespace ava::process
