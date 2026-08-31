#pragma once

#include "ava/debug/print_members_on.h"
#include "ava/process/types.h"
#include "ava/core/error.h"

#include <cstddef>
#include <cstdint>
#include <string_view>
#include <type_traits>

namespace ava::process::detail {

inline constexpr std::uint32_t kLaunchFrameMagicV1 = 0x41564c31U;
inline constexpr std::uint16_t kLaunchFrameVersionV1 = 1;

enum class LaunchFrameKindV1 : std::uint8_t
{
  LeaderReady = 1,
  LaunchFailed = 2,
  BashContainmentApplied = 3,
  ExecAttempt = 4,
  ExecFailed = 5,
};

enum class LaunchFailureStageV1 : std::uint8_t
{
  None = 0,
  SignalReset = 1,
  ProcessGroup = 2,
  Gate = 3,
  Streams = 4,
  WorkingDirectory = 5,
  Containment = 6,
  DescriptorValidation = 7,
};

// The fixed integer representation avoids ABI padding ambiguity and allows one
// atomic async-signal-safe pipe write for every child event.
struct LaunchFrameV1
{
  std::uint32_t magic = kLaunchFrameMagicV1;
  std::uint16_t version = kLaunchFrameVersionV1;
  std::uint8_t kind = 0;
  std::uint8_t stage = 0;
  std::int32_t error_number = 0;
  std::uint32_t reserved = 0;

  AVA_DEBUG_PRINT_MEMBERS_OPT_OUT
};

static_assert(std::is_trivially_copyable_v<LaunchFrameV1>);
static_assert(std::is_standard_layout_v<LaunchFrameV1>);
static_assert(offsetof(LaunchFrameV1, magic) == 0);
static_assert(offsetof(LaunchFrameV1, version) == 4);
static_assert(offsetof(LaunchFrameV1, kind) == 6);
static_assert(offsetof(LaunchFrameV1, stage) == 7);
static_assert(offsetof(LaunchFrameV1, error_number) == 8);
static_assert(offsetof(LaunchFrameV1, reserved) == 12);
static_assert(sizeof(LaunchFrameV1) == 16);
static_assert(sizeof(LaunchFrameV1) < 512);  // POSIX guarantees PIPE_BUF >= 512.

enum class LaunchProtocolDispositionV1 : std::uint8_t
{
  LeaderReady,
  ExecConfirmed,
  LaunchFailed,
  ExecFailed,
};

enum class LaunchProtocolProblemV1 : std::uint8_t
{
  None,
  ChildReported,
  EndBeforeAttempt,
  MalformedFrame,
  TruncatedFrame,
  OutOfOrderFrame,
  TimedOut,
  ReadFailed,
  ContinuationFailed,
};

struct LaunchProtocolOutcomeV1
{
  LaunchProtocolDispositionV1 disposition = LaunchProtocolDispositionV1::LaunchFailed;
  LaunchProtocolProblemV1 problem = LaunchProtocolProblemV1::None;
  LaunchFailureStageV1 stage = LaunchFailureStageV1::None;
  int error_number = 0;

  [[nodiscard]] bool succeeded() const noexcept
  {
    return disposition == LaunchProtocolDispositionV1::LeaderReady || disposition == LaunchProtocolDispositionV1::ExecConfirmed;
  }

  AVA_DEBUG_PRINT_MEMBERS_OPT_OUT
};

[[nodiscard]] bool child_write_leader_ready(int descriptor) noexcept;
[[nodiscard]] bool child_write_launch_failed(int descriptor, LaunchFailureStageV1 stage, int error_number) noexcept;
[[nodiscard]] bool child_write_bash_containment_applied(int descriptor) noexcept;
[[nodiscard]] bool child_write_exec_attempt(int descriptor) noexcept;
[[nodiscard]] bool child_write_exec_failed(int descriptor, int error_number) noexcept;

[[nodiscard]] LaunchProtocolOutcomeV1 await_launch_leader_ready(int descriptor, ProcessDeadline deadline) noexcept;
[[nodiscard]] LaunchProtocolOutcomeV1 await_launch_exec_confirmation(int descriptor, ProcessDeadline deadline, bool containment_required,
                                                                     int containment_continuation_descriptor = -1) noexcept;
[[nodiscard]] ava::core::Error launch_protocol_error(LaunchProtocolOutcomeV1 const& outcome, std::string_view operation);

}  // namespace ava::process::detail
