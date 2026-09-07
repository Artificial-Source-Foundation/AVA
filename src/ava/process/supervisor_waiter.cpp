#include "sys.h"
#include "ava/process/supervisor_internal.h"

#include <limits>
#include <memory>
#include <mutex>
#include <utility>

namespace ava::process::detail {

void notify_monitor_state(std::shared_ptr<SupervisorState> const& state) noexcept
{
  if (!state)
    return;
#if !defined(_WIN32)
  std::shared_ptr<MonitorWake> wake;
  {
    std::lock_guard lock(state->mutex);
    wake = state->monitor_wake;
  }
  signal_monitor_wake(wake);
#endif
  state->changed.notify_all();
}

ActiveWaiterRegistration::ActiveWaiterRegistration(std::shared_ptr<SupervisorState> state, std::uint64_t record) noexcept
    : state_(std::move(state)), record_(record)
{
}

ActiveWaiterRegistration::ActiveWaiterRegistration(ActiveWaiterRegistration&& other) noexcept
    : state_(std::move(other.state_)), record_(std::exchange(other.record_, 0))
{
}

ActiveWaiterRegistration& ActiveWaiterRegistration::operator=(ActiveWaiterRegistration&& other) noexcept
{
  if (this != &other)
  {
    release();
    state_ = std::move(other.state_);
    record_ = std::exchange(other.record_, 0);
  }
  return *this;
}

ActiveWaiterRegistration::~ActiveWaiterRegistration()
{
  release();
}

void ActiveWaiterRegistration::release() noexcept
{
  if (!state_)
    return;
  {
    std::lock_guard lock(state_->mutex);
    auto found = state_->records.find(record_);
    if (found != state_->records.end() && found->second->active_waiters > 0)
    {
      --found->second->active_waiters;
#if !defined(_WIN32)
      if (found->second->active_waiters == 0)
        reset_record_probe_schedule_locked(*found->second, Clock::now());
#endif
    }
  }
  notify_monitor_state(state_);
  state_.reset();
  record_ = 0;
}

ava::core::Result<ActiveWaiterRegistration> register_active_waiter(std::shared_ptr<SupervisorState> const& state, std::uint64_t record)
{
  {
    std::lock_guard lock(state->mutex);
    auto found = state->records.find(record);
    if (found != state->records.end())
    {
      if (found->second->active_waiters == std::numeric_limits<std::size_t>::max())
        return std::unexpected(process_error(ava::core::ErrorCategory::Io, "process record active-waiter capacity is exhausted"));
      ++found->second->active_waiters;
#if !defined(_WIN32)
      reset_record_probe_schedule_locked(*found->second, Clock::now());
#endif
    }
  }
  notify_monitor_state(state);
  return ActiveWaiterRegistration(state, record);
}

}  // namespace ava::process::detail
