#include "sys.h"
#include "ava/app/session_run_controller.h"
#include "ava/core/error.h"

#include <condition_variable>
#include <deque>
#include <exception>
#include <utility>
#include "debug.h"

namespace ava::app {
namespace {

ava::core::Error transition_error(RunPhase from, RunPhase to)
{
  auto error = ava::core::Error(ava::core::ErrorCategory::InvalidArgument, "invalid run phase transition");
  error.with_context("from", std::string(to_string(from)));
  error.with_context("to", std::string(to_string(to)));
  return error;
}

ava::core::Error inactive_error()
{
  return ava::core::Error(ava::core::ErrorCategory::InvalidArgument, "session has no active run");
}

ava::core::Error stop_requested_error(StopReason reason)
{
  auto error = ava::core::Error(ava::core::ErrorCategory::Unknown, reason == StopReason::UserCanceled ? "agent loop canceled" : "run stop requested");
  error.with_context("stop_reason", std::string(to_string(reason)));
  return error;
}

ava::core::Error stale_error()
{
  return ava::core::Error(ava::core::ErrorCategory::InvalidArgument, "stale run route");
}

ava::core::Error queue_limit_error(std::string_view queue, std::size_t limit)
{
  auto error = ava::core::Error(ava::core::ErrorCategory::InvalidArgument, "session run queue limit exceeded");
  error.with_context("queue", std::string(queue));
  error.with_context("limit", std::to_string(limit));
  return error;
}

ava::core::Error reentrant_append_error()
{
  return ava::core::Error(ava::core::ErrorCategory::InvalidArgument, "reentrant session append is not allowed");
}

bool legal_transition(RunPhase from, RunPhase to)
{
  if (from == to)
    return true;
  switch (from)
  {
    case RunPhase::Admitted:
      return to == RunPhase::BuildingContext || to == RunPhase::Canceling || to == RunPhase::Failed;
    case RunPhase::BuildingContext:
      return to == RunPhase::AwaitingProvider || to == RunPhase::Compacting || to == RunPhase::Canceling || to == RunPhase::Failed;
    case RunPhase::AwaitingProvider:
      return to == RunPhase::PersistingAssistant || to == RunPhase::PreparingTools || to == RunPhase::Compacting || to == RunPhase::Completing ||
             to == RunPhase::Canceling || to == RunPhase::Failed;
    case RunPhase::PersistingAssistant:
      return to == RunPhase::PreparingTools || to == RunPhase::AwaitingProvider || to == RunPhase::Completing || to == RunPhase::Canceling ||
             to == RunPhase::Failed;
    case RunPhase::PreparingTools:
      return to == RunPhase::ExecutingTools || to == RunPhase::Canceling || to == RunPhase::Failed;
    case RunPhase::ExecutingTools:
      return to == RunPhase::SettlingTools || to == RunPhase::Canceling || to == RunPhase::Failed;
    case RunPhase::SettlingTools:
      return to == RunPhase::AwaitingProvider || to == RunPhase::Completing || to == RunPhase::Canceling || to == RunPhase::Failed;
    case RunPhase::Compacting:
      return to == RunPhase::BuildingContext || to == RunPhase::AwaitingProvider || to == RunPhase::Canceling || to == RunPhase::Failed;
    case RunPhase::Completing:
    case RunPhase::Canceling:
    case RunPhase::Failed:
      return false;
  }
  AI_NEVER_REACHED
}

thread_local void const* append_in_progress = nullptr;

class AppendMarker final
{
 public:
  explicit AppendMarker(void const* state) : previous_(std::exchange(append_in_progress, state)) { }
  ~AppendMarker() { append_in_progress = previous_; }
  AppendMarker(AppendMarker const&) = delete;
  AppendMarker& operator=(AppendMarker const&) = delete;

 private:
  void const* previous_;
};

ava::core::Error append_exception_error(std::string_view cause)
{
  auto error = ava::core::Error(ava::core::ErrorCategory::Io, "session append threw an unexpected exception");
  error.with_context("cause", std::string(cause));
  return error;
}

}  // namespace

struct ActiveRunGuard::State
{
  struct AppendItem
  {
    enum class Completion
    {
      Pending,
      Succeeded,
      PersistenceFailed,
      Closing,
    };

    ava::session::SessionEntry entry;
    std::size_t bytes = 0;
    Completion completion = Completion::Pending;
    std::optional<ava::core::Error> error;
    std::condition_variable ready;
  };

  mutable std::mutex mutex;
  std::mutex append_mutex;
  std::condition_variable appends_drained;
  std::condition_variable outcome_changed;
  std::stop_source stop_source;
  bool active = false;
  bool closing = false;
  bool shutting_down = false;
  std::uint64_t generation = 0;
  std::string run_id;
  RunPhase phase = RunPhase::Admitted;
  std::optional<StopReason> requested_stop;
  std::optional<RunOutcome> outcome;
  // Retain terminal outcomes by correlation so an A waiter cannot observe a
  // newly admitted B and lose A's result before it wakes.
  std::unordered_map<std::string, RunOutcome> outcomes;
  std::deque<std::string> outcome_order;
  std::optional<ava::core::Error> persistence_failure;
  std::uint64_t persistence_failure_generation = 0;
  std::shared_ptr<ava::session::SessionAppendTarget> append_target;
  std::deque<RunCommand> commands;
  std::deque<std::shared_ptr<AppendItem>> appends;
  std::size_t append_bytes = 0;
};

SessionRunController::SessionRunController(std::shared_ptr<ava::session::SessionAppendTarget> append_target) : state_(std::make_shared<ActiveRunGuard::State>())
{
  state_->append_target = std::move(append_target);
}

SessionRunController::~SessionRunController()
{
  shutdown();
}

void SessionRunController::shutdown() noexcept
{
  auto state = state_;
  if (!state)
    return;
  try
  {
    std::stop_source source;
    {
      std::lock_guard lock(state->mutex);
      if (!state->shutting_down)
      {
        state->shutting_down = true;
        state->closing = true;
        state->requested_stop = StopReason::UserCanceled;
        source = state->stop_source;
      }
    }
    static_cast<void>(source.request_stop());

    // The active driver completes before shutdown takes this lock. Once
    // serialized, revoke and move authority before any caller-side Error is
    // synthesized. Closing completion itself is nonallocating.
    std::shared_ptr<ava::session::SessionAppendTarget> released_target;
    {
      std::lock_guard driver_lock(state->append_mutex);
      std::lock_guard lock(state->mutex);
      released_target = std::move(state->append_target);
      while (!state->appends.empty())
      {
        auto pending = state->appends.front();
        state->appends.pop_front();
        state->append_bytes -= pending->bytes;
        pending->completion = ActiveRunGuard::State::AppendItem::Completion::Closing;
        pending->ready.notify_all();
      }
      state->appends_drained.notify_all();
    }
    released_target.reset();
  }
  catch (...)
  {
    // Destructors must not throw. Best-effort revocation is nonallocating and
    // active drivers retain their own temporary target until completion.
    try
    {
      std::lock_guard lock(state->mutex);
      state->shutting_down = true;
      state->closing = true;
      state->append_target.reset();
    }
    catch (...)
    {
    }
  }
}

AdmissionDisposition SessionRunController::inspect_admission(RunRequest const& request) const
{
  std::lock_guard lock(state_->mutex);
  if (state_->shutting_down || !state_->append_target)
    return AdmissionDisposition::RejectClosing;
  if (state_->persistence_failure)
    return AdmissionDisposition::RejectPersistenceFailure;
  if (!state_->active)
    return AdmissionDisposition::Admit;
  return request.request_id == state_->run_id ? AdmissionDisposition::JoinExistingOutcome : AdmissionDisposition::RejectDifferentRequest;
}

ava::core::Result<ActiveRunGuard> SessionRunController::admit(RunRequest request)
{
  if (request.request_id.empty())
    return std::unexpected(ava::core::Error(ava::core::ErrorCategory::InvalidArgument, "run request id is required"));
  std::lock_guard lock(state_->mutex);
  if (state_->shutting_down || !state_->append_target)
    return std::unexpected(ava::core::Error(ava::core::ErrorCategory::InvalidArgument, "runtime session is closing"));
  if (state_->persistence_failure)
    return std::unexpected(*state_->persistence_failure);
  if (state_->active)
  {
    auto error = ava::core::Error(ava::core::ErrorCategory::InvalidArgument, "session already has an active run");
    error.with_context("active_run_id", state_->run_id);
    error.with_context("admission", request.request_id == state_->run_id ? "join_existing_outcome" : "rejected_different_prompt");
    return std::unexpected(std::move(error));
  }
  state_->stop_source = std::stop_source{};
  state_->active = true;
  state_->closing = false;
  ++state_->generation;
  state_->run_id = std::move(request.request_id);
  state_->phase = RunPhase::Admitted;
  state_->requested_stop.reset();
  state_->outcome.reset();
  state_->commands.clear();
  return ActiveRunGuard(state_, state_->generation);
}

ava::core::Result<RunOutcome> SessionRunController::wait_outcome(std::string_view correlation_id)
{
  std::unique_lock lock(state_->mutex);
  if (correlation_id.empty())
    return std::unexpected(stale_error());
  if (auto completed = state_->outcomes.find(std::string(correlation_id)); completed != state_->outcomes.end())
    return completed->second;
  if (!state_->active || correlation_id != state_->run_id)
    return std::unexpected(stale_error());
  state_->outcome_changed.wait(lock, [&] { return state_->outcomes.contains(std::string(correlation_id)); });
  auto completed = state_->outcomes.find(std::string(correlation_id));
  if (completed == state_->outcomes.end())
    return std::unexpected(stale_error());
  return completed->second;
}

ava::core::VoidResult SessionRunController::wake(RunCommand command)
{
  if (command.message.size() > kMaxSessionRunCommandBytes)
    return std::unexpected(queue_limit_error("commands", kMaxSessionRunCommandBytes));
  std::lock_guard lock(state_->mutex);
  if (!state_->active || state_->closing)
    return std::unexpected(inactive_error());
  if (!command.correlation_id.empty() && command.correlation_id != state_->run_id)
    return std::unexpected(stale_error());
  if (state_->commands.size() >= kMaxSessionRunCommands)
    return std::unexpected(queue_limit_error("commands", kMaxSessionRunCommands));
  if (command.correlation_id.empty())
    command.correlation_id = state_->run_id;
  state_->commands.push_back(std::move(command));
  return {};
}

ava::core::Result<std::deque<RunCommand>> SessionRunController::take_commands(std::string_view correlation_id, RunCommand::Kind kind)
{
  std::lock_guard lock(state_->mutex);
  if (!state_->active)
    return std::unexpected(inactive_error());
  std::deque<RunCommand> selected, remaining;
  while (!state_->commands.empty())
  {
    auto command = std::move(state_->commands.front());
    state_->commands.pop_front();
    if ((correlation_id.empty() || command.correlation_id == correlation_id) && command.kind == kind)
      selected.push_back(std::move(command));
    else
      remaining.push_back(std::move(command));
  }
  state_->commands = std::move(remaining);
  return selected;
}

ava::core::Result<bool> SessionRunController::request_stop(StopReason reason)
{
  std::stop_source source;
  {
    std::lock_guard lock(state_->mutex);
    if (!state_->active || state_->closing || state_->phase == RunPhase::Completing || state_->requested_stop)
      return false;
    state_->requested_stop = reason;
    source = state_->stop_source;
  }
  // Stop callbacks may reenter the controller, so they must run lock-free.
  static_cast<void>(source.request_stop());
  return true;
}

RunSnapshot SessionRunController::snapshot() const
{
  std::lock_guard lock(state_->mutex);
  return {.active = state_->active,
          .run_id = state_->run_id,
          .phase = state_->phase,
          .stop_requested = state_->stop_source.stop_requested(),
          .queued_commands = state_->commands.size(),
          .queued_appends = state_->appends.size(),
          .outcome = state_->outcome};
}

ava::core::VoidResult SessionRunController::append_for_generation(std::shared_ptr<ActiveRunGuard::State> const& state, std::uint64_t generation,
                                                                  ava::session::SessionEntry entry, bool owner_route)
{
  if (!state)
    return std::unexpected(inactive_error());
  if (append_in_progress == state.get())
    return std::unexpected(reentrant_append_error());
  auto item = std::make_shared<ActiveRunGuard::State::AppendItem>();
  item->bytes = entry.id.size() + entry.parent_id.size() + entry.timestamp.size() + entry.data_json.size() + 32;
  item->entry = std::move(entry);
  {
    std::lock_guard lock(state->mutex);
    if (state->persistence_failure)
      return std::unexpected(*state->persistence_failure);
    // The stable owner route is valid between runs as well as during one. Only
    // shutdown and an explicitly latched persistence failure revoke it.
    if (state->shutting_down || !state->append_target)
      return std::unexpected(inactive_error());
    if (!owner_route && (!state->active || state->closing || state->generation != generation))
      return std::unexpected(stale_error());
    if (state->appends.size() >= kMaxSessionAppendQueueEntries)
      return std::unexpected(queue_limit_error("appends", kMaxSessionAppendQueueEntries));
    if (item->bytes > kMaxSessionAppendQueueBytes || state->append_bytes > kMaxSessionAppendQueueBytes - item->bytes)
      return std::unexpected(queue_limit_error("append_bytes", kMaxSessionAppendQueueBytes));
    state->append_bytes += item->bytes;
    state->appends.push_back(item);
  }

  std::unique_lock driver_lock(state->append_mutex);
  while (true)
  {
    std::shared_ptr<ActiveRunGuard::State::AppendItem> head;
    {
      std::unique_lock lock(state->mutex);
      if (item->completion != ActiveRunGuard::State::AppendItem::Completion::Pending)
      {
        if (item->completion == ActiveRunGuard::State::AppendItem::Completion::PersistenceFailed && item->error)
          return std::unexpected(*item->error);
        if (item->completion == ActiveRunGuard::State::AppendItem::Completion::Closing)
          return std::unexpected(inactive_error());
        return {};
      }
      if (state->appends.empty())
        continue;
      head = state->appends.front();
    }
    ava::core::VoidResult persisted;
    try
    {
      std::shared_ptr<ava::session::SessionAppendTarget> target;
      {
        std::lock_guard lock(state->mutex);
        target = state->append_target;
      }
      if (!target)
      {
        persisted = std::unexpected(inactive_error());
      }
      else
      {
        AppendMarker marker(state.get());
        persisted = target->append(head->entry);
      }
    }
    catch (std::exception const& exception)
    {
      persisted = std::unexpected(append_exception_error(exception.what()));
    }
    catch (...)
    {
      persisted = std::unexpected(append_exception_error("non-standard exception"));
    }

    std::stop_source source;
    bool request_stop = false;
    {
      std::lock_guard lock(state->mutex);
      // This driver is the only consumer, so the head cannot change here.
      if (!state->appends.empty() && state->appends.front() == head)
      {
        state->appends.pop_front();
        state->append_bytes -= head->bytes;
      }
      if (!persisted)
      {
        auto error = persisted.error();
        state->persistence_failure = error;
        ++state->persistence_failure_generation;
        state->requested_stop = StopReason::PersistenceError;
        source = state->stop_source;
        request_stop = true;
        head->error = error;
        head->completion = ActiveRunGuard::State::AppendItem::Completion::PersistenceFailed;
        head->ready.notify_all();
        while (!state->appends.empty())
        {
          auto pending = state->appends.front();
          state->appends.pop_front();
          state->append_bytes -= pending->bytes;
          pending->error = error;
          pending->completion = ActiveRunGuard::State::AppendItem::Completion::PersistenceFailed;
          pending->ready.notify_all();
        }
        state->appends_drained.notify_all();
      }
      else
      {
        head->completion = ActiveRunGuard::State::AppendItem::Completion::Succeeded;
        head->ready.notify_all();
        if (state->appends.empty())
          state->appends_drained.notify_all();
      }
    }
    if (request_stop)
      source.request_stop();
  }
}

ava::core::VoidResult SessionRunController::append(ava::session::SessionEntry entry)
{
  return append_for_generation(state_, 0, std::move(entry), true);
}

std::function<ava::core::VoidResult(ava::session::SessionEntry)> SessionRunController::owner_append_route() const
{
  auto state = state_;
  return [state = std::move(state)](ava::session::SessionEntry entry) { return append_for_generation(state, 0, std::move(entry), true); };
}

ava::core::VoidResult SessionRunController::reset_persistence_failure()
{
  auto state = state_;
  std::unique_lock driver_lock(state->append_mutex);

  std::shared_ptr<ava::session::SessionAppendTarget> target;
  std::uint64_t failure_generation = 0;
  {
    std::lock_guard lock(state->mutex);
    if (state->active || !state->appends.empty())
      return std::unexpected(ava::core::Error(ava::core::ErrorCategory::InvalidArgument, "cannot recover append failure during active run"));
    if (state->shutting_down || !state->append_target)
      return std::unexpected(ava::core::Error(ava::core::ErrorCategory::InvalidArgument, "cannot recover persistence for a closing runtime session"));
    if (!state->persistence_failure)
      return std::unexpected(ava::core::Error(ava::core::ErrorCategory::InvalidArgument, "session has no latched persistence failure to recover"));
    target = state->append_target;
    failure_generation = state->persistence_failure_generation;
  }

  // Recovery can scan, quarantine, sync, and truncate. Never hold the state
  // mutex across it; append_mutex alone excludes append drivers and serializes
  // shutdown's authority release.
  auto recovered = target->recover();
  if (!recovered)
    return std::unexpected(std::move(recovered.error()));

  {
    std::lock_guard lock(state->mutex);
    if (state->active || !state->appends.empty() || state->shutting_down || state->append_target != target || !state->persistence_failure ||
        state->persistence_failure_generation != failure_generation)
    {
      return std::unexpected(
          ava::core::Error(ava::core::ErrorCategory::InvalidArgument, "persistence recovery completed after runtime session authority changed"));
    }
    state->persistence_failure.reset();
  }
  return {};
}

ActiveRunGuard::ActiveRunGuard(std::shared_ptr<State> state, std::uint64_t generation) : state_(std::move(state)), generation_(generation)
{
}
ActiveRunGuard::~ActiveRunGuard()
{
  release();
}
ActiveRunGuard::ActiveRunGuard(ActiveRunGuard&& other) noexcept : state_(std::move(other.state_)), generation_(std::exchange(other.generation_, 0))
{
}
ActiveRunGuard& ActiveRunGuard::operator=(ActiveRunGuard&& other) noexcept
{
  if (this != &other)
  {
    release();
    state_ = std::move(other.state_);
    generation_ = std::exchange(other.generation_, 0);
  }
  return *this;
}
std::stop_token ActiveRunGuard::stop_token() const noexcept
{
  return state_ ? state_->stop_source.get_token() : std::stop_token{};
}
bool ActiveRunGuard::stop_requested() const noexcept
{
  return state_ && state_->stop_source.stop_requested();
}

ava::core::VoidResult ActiveRunGuard::transition(RunPhase next)
{
  if (!state_)
    return std::unexpected(inactive_error());
  std::lock_guard lock(state_->mutex);
  if (!state_->active || state_->generation != generation_)
    return std::unexpected(stale_error());
  if (next == RunPhase::Completing && state_->requested_stop)
    return std::unexpected(stop_requested_error(*state_->requested_stop));
  if (!legal_transition(state_->phase, next))
    return std::unexpected(transition_error(state_->phase, next));
  state_->phase = next;
  return {};
}

ava::core::Result<RunOutcome> ActiveRunGuard::complete(RunOutcome outcome)
{
  if (!state_)
    return std::unexpected(inactive_error());
  std::unique_lock lock(state_->mutex);
  if (!state_->active || state_->generation != generation_ || state_->outcome)
    return std::unexpected(stale_error());
  if (outcome.run_id.empty())
    outcome.run_id = state_->run_id;
  if (outcome.run_id != state_->run_id)
    return std::unexpected(stale_error());
  if (state_->requested_stop && state_->phase != RunPhase::Completing)
  {
    outcome.reason = *state_->requested_stop;
    outcome.error.reset();
  }
  if (outcome.reason == StopReason::Completed)
  {
    if (state_->phase != RunPhase::Completing)
      return std::unexpected(transition_error(state_->phase, RunPhase::Completing));
  }
  else if (outcome.reason == StopReason::UserCanceled || outcome.reason == StopReason::Deadline)
    state_->phase = RunPhase::Canceling;
  else
    state_->phase = RunPhase::Failed;
  state_->closing = true;
  state_->appends_drained.wait(lock, [&] { return state_->appends.empty(); });
  if (state_->persistence_failure)
  {
    outcome.reason = StopReason::PersistenceError;
    outcome.error = *state_->persistence_failure;
    state_->phase = RunPhase::Failed;
  }
  state_->outcome = outcome;
  if (state_->outcomes.emplace(outcome.run_id, outcome).second)
    state_->outcome_order.push_back(outcome.run_id);
  else
    state_->outcomes.insert_or_assign(outcome.run_id, outcome);
  while (state_->outcome_order.size() > kMaxRetainedRunOutcomes)
  {
    state_->outcomes.erase(state_->outcome_order.front());
    state_->outcome_order.pop_front();
  }
  state_->active = false;
  state_->commands.clear();
  state_->outcome_changed.notify_all();
  lock.unlock();
  state_.reset();
  generation_ = 0;
  return outcome;
}

bool ActiveRunGuard::active() const noexcept
{
  if (!state_)
    return false;
  std::lock_guard lock(state_->mutex);
  return state_->active && state_->generation == generation_;
}

std::function<ava::core::VoidResult(ava::session::SessionEntry)> ActiveRunGuard::append_route() const
{
  auto state = state_;
  auto generation = generation_;
  return [state = std::move(state), generation](ava::session::SessionEntry entry) {
    return SessionRunController::append_for_generation(state, generation, std::move(entry), false);
  };
}

void ActiveRunGuard::release() noexcept
{
  if (!state_)
    return;
  std::unique_lock lock(state_->mutex);
  if (state_->active && state_->generation == generation_)
  {
    RunOutcome outcome{.run_id = state_->run_id,
                       .reason = state_->stop_source.stop_requested() ? state_->requested_stop.value_or(StopReason::UserCanceled) : StopReason::ProviderError};
    if (outcome.reason == StopReason::ProviderError)
      outcome.error.emplace(ava::core::Error(ava::core::ErrorCategory::Unknown, "active run guard released before terminal outcome"));
    state_->phase = outcome.reason == StopReason::UserCanceled || outcome.reason == StopReason::Deadline ? RunPhase::Canceling : RunPhase::Failed;
    // Do not permit a generation route to enqueue after terminal release.
    // Waiting releases the state mutex, so the append driver can finish.
    state_->closing = true;
    state_->appends_drained.wait(lock, [&] { return state_->appends.empty(); });
    if (state_->persistence_failure)
    {
      outcome.reason = StopReason::PersistenceError;
      outcome.error = *state_->persistence_failure;
      state_->phase = RunPhase::Failed;
    }
    state_->outcome = outcome;
    if (state_->outcomes.emplace(outcome.run_id, outcome).second)
      state_->outcome_order.push_back(outcome.run_id);
    else
      state_->outcomes.insert_or_assign(outcome.run_id, outcome);
    while (state_->outcome_order.size() > kMaxRetainedRunOutcomes)
    {
      state_->outcomes.erase(state_->outcome_order.front());
      state_->outcome_order.pop_front();
    }
    state_->active = false;
    state_->commands.clear();
    state_->outcome_changed.notify_all();
  }
  lock.unlock();
  state_.reset();
  generation_ = 0;
}

std::string_view to_string(RunPhase phase)
{
  constexpr std::string_view names[] = {"admitted",        "building_context", "awaiting_provider", "persisting_assistant", "preparing_tools",
                                        "executing_tools", "settling_tools",   "compacting",        "completing",           "canceling",
                                        "failed"};
  return names[static_cast<std::size_t>(phase)];
}
std::string_view to_string(StopReason reason)
{
  constexpr std::string_view names[] = {"completed",   "user_canceled",  "deadline",   "max_turns",        "max_tool_calls",
                                        "no_progress", "provider_error", "tool_error", "persistence_error"};
  return names[static_cast<std::size_t>(reason)];
}

std::string_view to_string(AdmissionDisposition disposition)
{
  constexpr std::string_view names[] = {"admit", "join_existing_outcome", "rejected_different_prompt", "rejected_closing", "rejected_persistence_failure"};
  return names[static_cast<std::size_t>(disposition)];
}

}  // namespace ava::app
