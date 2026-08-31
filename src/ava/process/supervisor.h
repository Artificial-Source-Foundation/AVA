#pragma once

#include "ava/debug/print_members_on.h"
#include "ava/process/owner.h"
#include "ava/process/types.h"
#include "ava/core/result.h"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>

namespace ava::process {

namespace detail {
struct HandleState;
struct SupervisorState;
}  // namespace detail

namespace testing {
class SupervisorTestAccess;
}  // namespace testing

class Supervisor;

// Native-handle-neutral ownership wrapper for one nonblocking parent pipe end.
class PipeEndpoint
{
 public:
  PipeEndpoint() noexcept;
  PipeEndpoint(PipeEndpoint const&) = delete;
  PipeEndpoint& operator=(PipeEndpoint const&) = delete;
  PipeEndpoint(PipeEndpoint&&) noexcept;
  PipeEndpoint& operator=(PipeEndpoint&&) noexcept;
  ~PipeEndpoint();

  [[nodiscard]] bool valid() const noexcept;
  void close() noexcept;
  [[nodiscard]] ava::core::Result<PipeIoResultV1> read(std::span<std::byte> destination);
  [[nodiscard]] ava::core::Result<PipeIoResultV1> write(std::span<std::byte const> source);
  [[nodiscard]] ava::core::Result<bool> wait_readable(ProcessDeadline deadline) const;
  [[nodiscard]] ava::core::Result<bool> wait_writable(ProcessDeadline deadline) const;

  AVA_DEBUG_PRINT_MEMBERS_OPT_OUT

 private:
  struct Impl;
  explicit PipeEndpoint(std::unique_ptr<Impl> implementation) noexcept;

  std::unique_ptr<Impl> implementation_;

  friend class Supervisor;
};

class Reservation
{
 public:
  Reservation() noexcept;
  Reservation(Reservation const&) = delete;
  Reservation& operator=(Reservation const&) = delete;
  Reservation(Reservation&&) noexcept;
  Reservation& operator=(Reservation&&) noexcept;
  ~Reservation();

  [[nodiscard]] bool valid() const noexcept;

  AVA_DEBUG_PRINT_MEMBERS_OPT_OUT

 private:
  Reservation(std::shared_ptr<detail::SupervisorState> state, std::uint64_t record) noexcept;
  void abandon() noexcept;

  // Shared ownership keeps the reservation's release route valid until its
  // owning Supervisor has completed destruction.
  std::shared_ptr<detail::SupervisorState> state_;
  std::uint64_t record_ = 0;

  friend class Supervisor;
};

class ProcessHandle
{
 public:
  ProcessHandle() noexcept;
  ProcessHandle(ProcessHandle const&) = delete;
  ProcessHandle& operator=(ProcessHandle const&) = delete;
  ProcessHandle(ProcessHandle&&) noexcept;
  ProcessHandle& operator=(ProcessHandle&&) noexcept;
  ~ProcessHandle();

  [[nodiscard]] bool valid() const noexcept;

  AVA_DEBUG_PRINT_MEMBERS_OPT_OUT

 private:
  explicit ProcessHandle(std::shared_ptr<detail::HandleState> state) noexcept;

  // The monitor and caller share only this bounded, PID-free final-value cell.
  std::shared_ptr<detail::HandleState> state_;

  friend class Supervisor;
};

struct SpawnResultV1
{
  ProcessHandle handle;
  std::optional<PipeEndpoint> standard_input;
  std::optional<PipeEndpoint> standard_output;
  std::optional<PipeEndpoint> standard_error;

  AVA_DEBUG_PRINT_MEMBERS_OPT_OUT
};

class AdoptionGate
{
 public:
  AdoptionGate() noexcept;
  AdoptionGate(AdoptionGate const&) = delete;
  AdoptionGate& operator=(AdoptionGate const&) = delete;
  AdoptionGate(AdoptionGate&&) noexcept;
  AdoptionGate& operator=(AdoptionGate&&) noexcept;
  ~AdoptionGate();

  [[nodiscard]] bool valid() const noexcept;

  // In the child branch this call has already reset signals, established a
  // private group, acknowledged the parent, and waited for registration. The
  // caller must then execute only its reviewed async-signal-safe child path and
  // finish with exec or _exit; it must not return to application code.
  [[nodiscard]] ava::core::Result<AdoptionForkBranchV1> fork_leader();

  // Adds the one optional AVA-owned sentinel as an exact direct child. The
  // sentinel joins the gated leader group and remains blocked until adoption.
  [[nodiscard]] ava::core::VoidResult fork_sentinel();

  AVA_DEBUG_PRINT_MEMBERS_OPT_OUT

 private:
  struct Impl;
  explicit AdoptionGate(std::unique_ptr<Impl> implementation) noexcept;
  void abandon() noexcept;

  std::unique_ptr<Impl> implementation_;

  friend class Supervisor;
};

class Supervisor
{
 public:
  Supervisor();
  Supervisor(Supervisor const&) = delete;
  Supervisor& operator=(Supervisor const&) = delete;
  Supervisor(Supervisor&&) = delete;
  Supervisor& operator=(Supervisor&&) = delete;
  ~Supervisor() noexcept;

  [[nodiscard]] ava::core::Result<Reservation> reserve(OwnerPathV1 const& owner, ProcessRoleV1 role, LifecyclePolicyV1 policy = {});
  [[nodiscard]] ava::core::Result<SpawnResultV1> spawn(Reservation&& reservation, SpawnSpecV1 specification);

  [[nodiscard]] ava::core::Result<AdoptionGate> begin_secure_adoption(Reservation&& reservation);
  [[nodiscard]] ava::core::Result<ProcessHandle> adopt(AdoptionGate&& gate);

  void stop_accepting() noexcept;
  [[nodiscard]] ava::core::Result<StopResultV1> request_stop(ProcessHandle const& handle, TerminationReasonV1 reason, ProcessDeadline deadline);
  [[nodiscard]] ava::core::Result<StopResultV1> request_stop(OwnerPathV1 const& owner_prefix, TerminationReasonV1 reason, ProcessDeadline deadline);
  [[nodiscard]] ava::core::Result<ExitStatusV1> wait(ProcessHandle const& handle, ProcessDeadline deadline) const;
  [[nodiscard]] ava::core::VoidResult account_output(ProcessHandle const& handle, StreamKindV1 stream, std::uint64_t bytes, bool truncated);

  [[nodiscard]] ProcessSnapshotV1 snapshot() const;
  [[nodiscard]] ShutdownResultV1 shutdown(ProcessDeadline deadline) noexcept;

  AVA_DEBUG_PRINT_MEMBERS_OPT_OUT

 private:
  [[nodiscard]] ava::core::Result<std::uint64_t> consume_reservation(Reservation& reservation);

  struct Impl;
  std::unique_ptr<Impl> implementation_;

  friend class testing::SupervisorTestAccess;
};

}  // namespace ava::process
