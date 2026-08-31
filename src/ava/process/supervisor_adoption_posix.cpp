#include "sys.h"
#include "ava/process/supervisor_internal.h"

#include <cerrno>
#include <chrono>
#include <exception>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <utility>
#if !defined(_WIN32)
#include <csignal>
#include <poll.h>
#include <sys/types.h>
#include <unistd.h>
#endif

namespace ava::process {
namespace {

using detail::Clock;
using namespace std::chrono_literals;

#if !defined(_WIN32)
ava::core::Result<int> read_gate_status(int descriptor, std::string_view member, ProcessDeadline deadline)
{
  auto ready = detail::wait_descriptor(descriptor, POLLIN, deadline);
  if (!ready)
    return std::unexpected(std::move(ready.error()));
  if (!*ready)
    return std::unexpected(
        detail::process_error(ava::core::ErrorCategory::Io, "secure-adoption child gate timed out").with_context("member", std::string(member)));
  int status = EIO;
  std::size_t offset = 0;
  while (offset < sizeof(status))
  {
    auto const result = ::read(descriptor, reinterpret_cast<unsigned char*>(&status) + offset, sizeof(status) - offset);
    if (result > 0)
    {
      offset += static_cast<std::size_t>(result);
      continue;
    }
    if (result < 0 && errno == EINTR)
      continue;
    return std::unexpected(detail::process_error(ava::core::ErrorCategory::Io, "secure-adoption child gate closed before acknowledgement")
                               .with_context("member", std::string(member)));
  }
  return status;
}
#endif

}  // namespace

AdoptionGate::AdoptionGate() noexcept = default;
AdoptionGate::AdoptionGate(std::unique_ptr<Impl> implementation) noexcept : implementation_(std::move(implementation))
{
}
AdoptionGate::AdoptionGate(AdoptionGate&&) noexcept = default;
AdoptionGate& AdoptionGate::operator=(AdoptionGate&& other) noexcept
{
  if (this != &other)
  {
    abandon();
    implementation_ = std::move(other.implementation_);
  }
  return *this;
}
AdoptionGate::~AdoptionGate()
{
  abandon();
}

void AdoptionGate::abandon() noexcept
{
  if (!implementation_)
    return;
  auto& gate = *implementation_;
#if !defined(_WIN32)
  if (gate.child_branch)
  {
    implementation_.reset();
    return;
  }
  gate.leader_control.write_end.reset();
  if (gate.sentinel_control)
    gate.sentinel_control->write_end.reset();
  if (!gate.registered && (gate.leader > 1 || gate.sentinel > 1))
  {
    bool const cleaned = detail::exact_provisional_cleanup(gate.leader, gate.sentinel, Clock::now() + 500ms);
    detail::finish_unregistered(gate.state, gate.record, TerminationReasonV1::LaunchFailed, cleaned ? CleanupStateV1::Complete : CleanupStateV1::Incomplete);
    gate.record = 0;
    implementation_.reset();
    return;
  }
#endif
  if (!gate.registered)
    detail::finish_unregistered(gate.state, gate.record, TerminationReasonV1::LaunchFailed);
  gate.record = 0;
  implementation_.reset();
}

bool AdoptionGate::valid() const noexcept
{
  return implementation_ && implementation_->state && implementation_->record != 0;
}

ava::core::Result<AdoptionForkBranchV1> AdoptionGate::fork_leader()
{
#if defined(_WIN32)
  return std::unexpected(detail::unsupported_error());
#else
  if (!valid() || implementation_->leader > 1 || implementation_->child_branch)
    return std::unexpected(detail::invalid_error("secure adoption gate cannot fork another leader"));
  auto& gate = *implementation_;
  if (!detail::EnvironmentAccess::matches_secure_adoption(gate.environment, gate.role))
  {
    detail::finish_unregistered(gate.state, gate.record, TerminationReasonV1::LaunchFailed);
    gate.record = 0;
    return std::unexpected(detail::invalid_error("secure-adoption leader requires its retained exact-environment binding"));
  }
  TerminationReasonV1 stopped_reason = TerminationReasonV1::LaunchFailed;
  {
    std::lock_guard lock(gate.state->mutex);
    auto found = gate.state->records.find(gate.record);
    bool const launchable = found != gate.state->records.end() && found->second->state == ProcessStateV1::Launching && !found->second->reason &&
                            !gate.state->shutting_down && Clock::now() < gate.startup_deadline;
    if (!launchable)
    {
      if (found != gate.state->records.end() && found->second->reason)
        stopped_reason = *found->second->reason;
      else if (gate.state->shutting_down)
        stopped_reason = TerminationReasonV1::ApplicationShutdown;
      return std::unexpected(detail::canceled_launch_error("secure-adoption leader launch", stopped_reason));
    }
  }
  // Revalidate the retained role/profile capability at the final pre-fork
  // boundary. No adopted child is created from an invalid or mismatched gate.
  if (!detail::EnvironmentAccess::matches_secure_adoption(gate.environment, gate.role))
  {
    detail::finish_unregistered(gate.state, gate.record, TerminationReasonV1::LaunchFailed);
    gate.record = 0;
    return std::unexpected(detail::invalid_error("secure-adoption leader lost its exact-environment binding"));
  }
  pid_t const process = ::fork();
  if (process < 0)
    return std::unexpected(detail::io_error("failed to fork the secure-adoption leader", errno));
  if (process == 0)
  {
    gate.child_branch = true;
    gate.leader = -1;
    gate.leader_status.read_end.reset();
    gate.leader_control.write_end.reset();
    int status = 0;
    if (!detail::reset_child_signal_state() || ::setpgid(0, 0) != 0)
      status = errno == 0 ? EIO : errno;
    static_cast<void>(detail::child_write_all(gate.leader_status.write_end.get(), &status, sizeof(status)));
    gate.leader_status.write_end.reset();
    if (status != 0)
      _exit(127);
    char release = '\0';
    if (detail::child_read_retry(gate.leader_control.read_end.get(), &release, 1) != 1 || release != 'G')
      _exit(127);
    gate.leader_control.read_end.reset();
    return AdoptionForkBranchV1::Child;
  }
  gate.leader = process;
  gate.leader_status.write_end.reset();
  gate.leader_control.read_end.reset();
  return AdoptionForkBranchV1::Parent;
#endif
}

ava::core::VoidResult AdoptionGate::fork_sentinel()
{
#if defined(_WIN32)
  return std::unexpected(detail::unsupported_error());
#else
  if (!valid() || implementation_->child_branch)
    return std::unexpected(detail::invalid_error("secure adoption sentinel requires one valid parent gate"));
  if (implementation_->role != ProcessRoleV1::Bash || !detail::EnvironmentAccess::matches_secure_adoption(implementation_->environment, implementation_->role))
  {
    abandon();
    return std::unexpected(detail::invalid_error("secure adoption sentinel is unavailable for this closed launch capability"));
  }
  if (implementation_->leader <= 1 || implementation_->sentinel > 1)
    return std::unexpected(detail::invalid_error("secure adoption sentinel requires exactly one gated parent leader"));
  {
    std::lock_guard lock(implementation_->state->mutex);
    auto found = implementation_->state->records.find(implementation_->record);
    bool const launchable = found != implementation_->state->records.end() && found->second->state == ProcessStateV1::Launching && !found->second->reason &&
                            !implementation_->state->shutting_down && Clock::now() < implementation_->startup_deadline;
    if (!launchable)
      return std::unexpected(detail::process_error(ava::core::ErrorCategory::Io, "secure-adoption sentinel was stopped before fork"));
  }
  auto status_pipe = detail::make_cloexec_pipe();
  if (!status_pipe)
    return std::unexpected(std::move(status_pipe.error()));
  auto control_pipe = detail::make_cloexec_pipe();
  if (!control_pipe)
    return std::unexpected(std::move(control_pipe.error()));
  implementation_->sentinel_status = std::move(*status_pipe);
  implementation_->sentinel_control = std::move(*control_pipe);

  auto& gate = *implementation_;
  pid_t const process = ::fork();
  if (process < 0)
    return std::unexpected(detail::io_error("failed to fork the secure-adoption sentinel", errno));
  if (process == 0)
  {
    gate.child_branch = true;
    gate.leader_status.read_end.reset();
    gate.leader_status.write_end.reset();
    gate.leader_control.read_end.reset();
    gate.leader_control.write_end.reset();
    gate.sentinel_status->read_end.reset();
    gate.sentinel_control->write_end.reset();
    int status = 0;
    if (!detail::reset_child_signal_state() || ::setpgid(0, gate.leader) != 0)
      status = errno == 0 ? EIO : errno;
    static_cast<void>(detail::child_write_all(gate.sentinel_status->write_end.get(), &status, sizeof(status)));
    gate.sentinel_status->write_end.reset();
    if (status != 0)
      _exit(127);
    char release = '\0';
    if (detail::child_read_retry(gate.sentinel_control->read_end.get(), &release, 1) != 1 || release != 'G')
      _exit(127);
    gate.sentinel_control->read_end.reset();
    while (true)
      static_cast<void>(::pause());
  }
  gate.sentinel = process;
  gate.sentinel_status->write_end.reset();
  gate.sentinel_control->read_end.reset();
  return {};
#endif
}

ava::core::Result<AdoptionGate> Supervisor::begin_secure_adoption(Reservation&& reservation, ExactEnvironmentV1 environment)
{
  auto state = implementation_->state;
  auto consumed = consume_reservation(reservation);
  if (!consumed)
    return std::unexpected(std::move(consumed.error()));
  auto const identity = *consumed;
  auto const role = detail::record_role(state, identity);
  if (!role || !detail::EnvironmentAccess::matches_secure_adoption(environment, *role))
  {
    detail::finish_unregistered(state, identity, TerminationReasonV1::LaunchFailed);
    return std::unexpected(detail::invalid_error("reserved secure adoption requires its matching exact-environment capability"));
  }
#if defined(_WIN32)
  detail::finish_unregistered(state, identity, TerminationReasonV1::LaunchFailed);
  return std::unexpected(detail::unsupported_error());
#else
  auto leader_status = detail::make_cloexec_pipe();
  if (!leader_status)
  {
    detail::finish_unregistered(state, identity, TerminationReasonV1::LaunchFailed);
    return std::unexpected(std::move(leader_status.error()));
  }
  auto leader_control = detail::make_cloexec_pipe();
  if (!leader_control)
  {
    detail::finish_unregistered(state, identity, TerminationReasonV1::LaunchFailed);
    return std::unexpected(std::move(leader_control.error()));
  }
  auto const startup_deadline = detail::startup_deadline_for_record(state, identity);
  std::unique_ptr<AdoptionGate::Impl> implementation;
  try
  {
    implementation = std::make_unique<AdoptionGate::Impl>();
    implementation->handle = std::make_shared<detail::HandleState>();
  }
  catch (std::exception const& error)
  {
    detail::finish_unregistered(state, identity, TerminationReasonV1::LaunchFailed);
    return std::unexpected(
        detail::process_error(ava::core::ErrorCategory::Io, "failed to allocate secure-adoption capabilities").with_context("cause", error.what()));
  }
  catch (...)
  {
    detail::finish_unregistered(state, identity, TerminationReasonV1::LaunchFailed);
    return std::unexpected(detail::process_error(ava::core::ErrorCategory::Io, "failed to allocate secure-adoption capabilities"));
  }
  implementation->state = state;
  implementation->record = identity;
  implementation->role = *role;
  implementation->environment = std::move(environment);
  implementation->startup_deadline = startup_deadline;
  implementation->leader_status = std::move(*leader_status);
  implementation->leader_control = std::move(*leader_control);
  return AdoptionGate(std::move(implementation));
#endif
}

ava::core::Result<ProcessHandle> Supervisor::adopt(AdoptionGate&& gate)
{
#if defined(_WIN32)
  static_cast<void>(gate);
  return std::unexpected(detail::unsupported_error());
#else
  auto state = implementation_->state;
  if (!gate.valid() || gate.implementation_->state.get() != state.get() || gate.implementation_->child_branch || gate.implementation_->leader <= 1)
    return std::unexpected(detail::invalid_error("secure adoption gate does not contain this supervisor's exact gated leader"));
  auto& ticket = *gate.implementation_;
  auto const identity = ticket.record;
  auto leader_status = read_gate_status(ticket.leader_status.read_end.get(), "leader", ticket.startup_deadline);
  if (!leader_status || *leader_status != 0)
    return std::unexpected(leader_status ? detail::io_error("secure-adoption leader setup failed", *leader_status) : std::move(leader_status.error()));

  bool leader_group_set = false;
  while (true)
  {
    if (::setpgid(ticket.leader, ticket.leader) == 0)
    {
      leader_group_set = true;
      break;
    }
    if (errno != EINTR)
      break;
  }
  int const leader_group_error = errno;
  pid_t const parent_group = ::getpgrp();
  pid_t const observed_leader_group = ::getpgid(ticket.leader);
  if (!leader_group_set || observed_leader_group != ticket.leader || parent_group <= 0 || observed_leader_group == parent_group)
    return std::unexpected(detail::io_error("failed to prove the secure-adoption leader process group", leader_group_error));

  if (ticket.sentinel > 1)
  {
    auto sentinel_status = read_gate_status(ticket.sentinel_status->read_end.get(), "sentinel", ticket.startup_deadline);
    if (!sentinel_status || *sentinel_status != 0)
      return std::unexpected(sentinel_status ? detail::io_error("secure-adoption sentinel setup failed", *sentinel_status)
                                             : std::move(sentinel_status.error()));
    bool sentinel_group_set = false;
    while (true)
    {
      if (::setpgid(ticket.sentinel, ticket.leader) == 0)
      {
        sentinel_group_set = true;
        break;
      }
      if (errno != EINTR)
        break;
    }
    int const sentinel_group_error = errno;
    if (!sentinel_group_set || ::getpgid(ticket.sentinel) != ticket.leader)
      return std::unexpected(detail::io_error("failed to prove the exact secure-adoption sentinel membership", sentinel_group_error));
  }

  if (auto monitor = detail::ensure_monitor_started(state); !monitor)
    return std::unexpected(std::move(monitor.error()));

  auto handle_state = ticket.handle;
  {
    std::lock_guard lock(state->mutex);
    auto found = state->records.find(identity);
    if (found == state->records.end() || found->second->state == ProcessStateV1::Finished)
      return std::unexpected(detail::process_error(ava::core::ErrorCategory::Io, "secure-adoption reservation ended before registry commit"));
    auto& record = *found->second;
    handle_state->supervisor = state;
    handle_state->record = identity;
    record.handle = handle_state;
    record.registered = true;
    record.group_verified = true;
    record.startup_handshake_complete = true;
    record.leader = ticket.leader;
    record.sentinel = ticket.sentinel;
    record.cleanup = CleanupStateV1::Pending;
    record.state = record.reason ? ProcessStateV1::StopRequested : ProcessStateV1::Launching;
    ticket.registered = true;
  }
  state->changed.notify_all();

  if (auto hook = detail::invoke_after_fork_before_release_hook(state); !hook)
  {
    ticket.leader_control.write_end.reset();
    if (ticket.sentinel_control)
      ticket.sentinel_control->write_end.reset();
    auto const cleanup_deadline = detail::fail_registered_launch(state, identity, TerminationReasonV1::LaunchFailed);
    state->changed.notify_all();
    detail::await_internal_settlement(handle_state, cleanup_deadline);
    ticket.record = 0;
    return std::unexpected(std::move(hook.error()));
  }

  detail::GateReleaseDecision release_decision;
  {
    std::lock_guard lock(state->mutex);
    // One commit linearizes adoption before either exact child is released.
    release_decision = detail::commit_gate_release_locked(*state, identity, ticket.startup_deadline);
  }
  state->changed.notify_all();
  if (!release_decision.committed)
  {
    ticket.leader_control.write_end.reset();
    if (ticket.sentinel_control)
      ticket.sentinel_control->write_end.reset();
    detail::await_internal_settlement(handle_state, release_decision.cleanup_deadline);
    ticket.record = 0;
    return std::unexpected(detail::canceled_launch_error("secure adoption", release_decision.reason));
  }

  char const release = 'G';
  bool released = true;
  if (ticket.sentinel_control)
    released = detail::write_without_sigpipe(ticket.sentinel_control->write_end.get(), &release, 1);
  released = detail::write_without_sigpipe(ticket.leader_control.write_end.get(), &release, 1) && released;
  ticket.sentinel_control.reset();
  ticket.leader_control.write_end.reset();
  if (!released)
  {
    auto const cleanup_deadline = detail::fail_registered_launch(state, identity, TerminationReasonV1::LaunchFailed);
    state->changed.notify_all();
    detail::await_internal_settlement(handle_state, cleanup_deadline);
    ticket.record = 0;
    return std::unexpected(detail::process_error(ava::core::ErrorCategory::Io, "failed to release a committed secure-adoption gate"));
  }

  ticket.record = 0;
  return ProcessHandle(std::move(handle_state));
#endif
}

}  // namespace ava::process
