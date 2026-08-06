#include "sys.h"
#include "tests/support/test_harness.h"
#include "tests/support/test_timeout.h"
#include "ava/observability/run_observer.h"
#include "ava/app/runtime.h"
#include "ava/app/runtime/Session.h"
#include "ava/app/session_run_controller.h"
#include "ava/session/session_store.h"
#include "ava/core/thread.h"

#include <algorithm>
#include <atomic>
#include <cerrno>
#include <chrono>
#include <condition_variable>
#include <filesystem>
#include <fstream>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <thread>
#include <utility>
#include <vector>
#include <unistd.h>

namespace {

class CallbackRunObserver final : public ava::observability::RunObserver
{
 public:
  explicit CallbackRunObserver(std::function<void(ava::observability::TraceEvent const&)> callback) : callback_(std::move(callback)) { }

  void on_event(ava::observability::TraceEvent const& event) override { callback_(event); }

 private:
  std::function<void(ava::observability::TraceEvent const&)> callback_;
};

std::shared_ptr<ava::session::SessionAppendTarget> ephemeral_controller_target()
{
  auto const root = create_empty_root("ephemeral_controller_target");
  auto store = ava::session::SessionStore::create_ephemeral(root / "controller-ephemeral-workspace");
  if (!store)
    return {};
  auto target = ava::session::SessionAppendTarget::create_ephemeral(*store);
  return target ? std::move(*target) : std::shared_ptr<ava::session::SessionAppendTarget>{};
}

ava::core::Result<std::shared_ptr<ava::session::SessionAppendTarget>> persistent_controller_target(ava::session::SessionStore const& store,
                                                                                                   ava::session::SessionLease const& lease)
{
  return ava::session::SessionAppendTarget::create_persistent(store, lease);
}

void test_session_run_controller_transitions_and_guard_release()
{
  ava::app::SessionRunController controller(ephemeral_controller_target());
  auto admitted = controller.admit({.request_id = "run-1"});
  expect(admitted.has_value(), "controller admits one run");
  if (!admitted)
    return;
  auto guard = std::move(*admitted);
  expect(guard.transition(ava::app::RunPhase::BuildingContext).has_value(), "controller permits admitted to building transition");
  expect(!guard.transition(ava::app::RunPhase::ExecutingTools).has_value(), "controller rejects skipped phase transition");
  expect(guard.transition(ava::app::RunPhase::AwaitingProvider).has_value(), "controller permits building to provider transition");
  expect(guard.transition(ava::app::RunPhase::Completing).has_value(), "controller permits provider completion transition");
  expect(guard.complete({.run_id = {}, .reason = ava::app::StopReason::Completed}).has_value(), "controller records one terminal outcome");
  expect(!guard.complete({.run_id = {}, .reason = ava::app::StopReason::Completed}).has_value(), "controller rejects a second terminal outcome");
  expect(!controller.snapshot().active, "completed guard releases active state");

  auto error_guard = controller.admit({.request_id = "run-2"});
  expect(error_guard.has_value(), "controller can admit after completed run");
  if (error_guard)
  {
    auto moved = std::move(*error_guard);
    auto moved_again = std::move(moved);
    expect(moved_again.active(), "move-only guard retains active ownership after move");
  }
  auto snapshot = controller.snapshot();
  expect(!snapshot.active && snapshot.outcome && snapshot.outcome->reason == ava::app::StopReason::ProviderError,
         "guard destructor releases an errored run exactly once");
}

void test_session_run_controller_concurrent_admit_wake_stop()
{
  ava::app::SessionRunController controller(ephemeral_controller_target());
  auto admitted = controller.admit({.request_id = "run"});
  expect(admitted.has_value(), "controller admits concurrency fixture run");
  if (!admitted)
    return;
  auto guard = std::move(*admitted);
  std::atomic<int> accepted = 0;
  std::vector<std::jthread> threads;
  for (int index = 0; index < 8; ++index)
  {
    threads.emplace_back([&] {
      if (controller.wake({.kind = ava::app::RunCommand::Kind::Steering, .correlation_id = "run", .message = "safe point"}))
        ++accepted;
    });
  }
  for (auto& thread : threads) thread.join();
  expect(accepted == 8 && controller.snapshot().queued_commands == 8, "concurrent wake calls retain ordered bounded commands");
  auto stopped = controller.request_stop();
  expect(stopped && *stopped && guard.stop_requested(), "stop interrupts the active guard and reports accepted arbitration");
  expect(guard.complete({.run_id = {}, .reason = ava::app::StopReason::UserCanceled}).has_value(), "stopped run has one canceled outcome");
  expect(!controller.wake({.kind = ava::app::RunCommand::Kind::Wake, .correlation_id = {}, .message = "late"}).has_value(),
         "wake rejects after terminal release");
}

void test_session_run_controller_generation_and_cancel_precedence()
{
  ava::app::SessionRunController controller(ephemeral_controller_target());
  auto first = controller.admit({.request_id = "A"});
  expect(first.has_value(), "controller admits first generation");
  if (!first)
    return;
  auto stale = std::move(*first);
  expect(stale.transition(ava::app::RunPhase::BuildingContext).has_value(), "first generation enters context");
  expect(stale.transition(ava::app::RunPhase::AwaitingProvider).has_value(), "first generation enters provider");
  expect(stale.transition(ava::app::RunPhase::Completing).has_value(), "first generation completes work");
  auto late_stop = controller.request_stop();
  expect(late_stop && !*late_stop && !stale.stop_requested(), "late cancellation is rejected after Completing");
  expect(stale.complete({.run_id = {}, .reason = ava::app::StopReason::Completed}).has_value(), "completion wins over cancellation after Completing");
  auto second = controller.admit({.request_id = "B"});
  expect(second.has_value(), "second generation admits after A completion");
  if (!second)
    return;
  auto current = std::move(*second);
  expect(!stale.transition(ava::app::RunPhase::BuildingContext).has_value(), "stale A guard cannot transition B");
  expect(!stale.complete({.run_id = {}, .reason = ava::app::StopReason::Completed}).has_value(), "stale A guard cannot complete B");
  expect(current.active(), "B remains owned after stale A operations");

  auto before_completing_stop = controller.request_stop();
  expect(before_completing_stop && *before_completing_stop, "cancellation before Completing wins controller arbitration");
  auto canceled = current.complete({.run_id = {}, .reason = ava::app::StopReason::ProviderError});
  expect(canceled && canceled->reason == ava::app::StopReason::UserCanceled && !canceled->error,
         "an accepted stop remains authoritative over a racing provider terminal error");
}

ava::session::SessionEntry append_entry(std::string id)
{
  return ava::session::SessionEntry{
      .id = std::move(id), .parent_id = "", .type = ava::session::EntryType::Error, .timestamp = ava::session::now_timestamp(), .data_json = "{\"test\":true}"};
}

void test_session_run_controller_owner_and_generation_routes()
{
  auto const root = create_empty_root("session-run-controller");

  auto store = ava::session::SessionStore::create(std::filesystem::current_path(), root);
  expect(store.has_value(), "controller append fixture store creates");
  if (!store)
    return;

  auto lease = ava::session::SessionLease::create_and_acquire(store->session_path());
  expect(lease.has_value(), "controller route fixture acquires a lease");
  if (!lease)
    return;
  auto target = persistent_controller_target(*store, *lease);
  expect(target.has_value(), "controller route fixture creates append target");
  if (!target)
    return;
  ava::app::SessionRunController controller(std::move(*target));
  auto owner = controller.owner_append_route();
  expect(owner(append_entry("inactive-owner")).has_value(), "owner route persists while no run is active");
  auto first = controller.admit({.request_id = "A"});
  expect(first.has_value(), "controller admits append generation A");
  if (!first)
    return;
  auto guard = std::move(*first);
  auto active = guard.append_route();
  expect(active(append_entry("A-active")).has_value(), "generation route persists active A entry");
  expect(owner(append_entry("A-owner")).has_value(), "owner route serializes active parent notice");
  expect(guard.transition(ava::app::RunPhase::BuildingContext).has_value(), "A enters context before completion");
  expect(guard.transition(ava::app::RunPhase::AwaitingProvider).has_value(), "A enters provider before completion");
  expect(guard.transition(ava::app::RunPhase::Completing).has_value(), "A enters completion before completion");
  expect(guard.complete({.run_id = {}, .reason = ava::app::StopReason::Completed}).has_value(), "A completes before inactive owner append");
  expect(owner(append_entry("after-A-owner")).has_value(), "owner route remains valid after A completion");
  auto second = controller.admit({.request_id = "B"});
  expect(second.has_value(), "controller admits B after A");
  if (!second)
    return;
  auto b_guard = std::move(*second);
  expect(!active(append_entry("stale-A")).has_value(), "completed A route cannot append into B");
  expect(owner(append_entry("during-B-owner")).has_value(), "owner route persists notice during B");
  auto entries = store->load();
  expect(entries && entries->size() == 5, "owner and active routes preserve all non-stale entries in order");
  if (entries && entries->size() == 5)
    expect(entries->at(0).id == "inactive-owner" && entries->at(4).id == "during-B-owner", "owner append FIFO order is durable");
}

void test_runtime_session_destruction_does_not_own_background_coordinator()
{
  auto const root = create_empty_root("session-run-controller-teardown");
  auto coordinator_result = ava::agent::SubagentCoordinator::create();
  expect(coordinator_result.has_value(), "application coordinator creates for runtime teardown fixture");
  if (!coordinator_result)
    return;
  auto coordinator = *coordinator_result;
  std::mutex mutex;
  std::condition_variable changed;
  bool worker_started = false;
  bool worker_canceled = false;
  {
    auto visible_session_coordinator = coordinator;
    auto started = visible_session_coordinator->start_background(
        "parent_teardown", {.title = "blocked", .description = "blocked", .child_session_id = "child_teardown"},
        [&](ava::agent::BackgroundJobContext const& context) {
          std::stop_callback wake_on_stop(context.stop_token, [&] { changed.notify_all(); });
          std::unique_lock lock(mutex);
          worker_started = true;
          changed.notify_all();
          changed.wait(lock, [&] { return context.stop_token.stop_requested(); });
          worker_canceled = true;
          changed.notify_all();
          return ava::agent::BackgroundJobCompletion{
              .state = ava::agent::BackgroundJobState::Canceled, .final_text = {}, .stop_reason = "canceled", .error = std::nullopt};
        });
    expect(started.has_value(), "application coordinator starts blocked worker");
    std::unique_lock lock(mutex);
    expect(changed.wait_for(lock, std::chrono::seconds(1), [&] { return worker_started; }), "blocked worker starts before visible session destruction");
    visible_session_coordinator.reset();
  }
  {
    std::lock_guard lock(mutex);
    expect(!worker_canceled, "visible runtime session destruction does not cancel application-owned worker");
  }
  coordinator->shutdown();
  std::lock_guard lock(mutex);
  expect(worker_canceled, "application coordinator shutdown cancels and joins worker");
}

void test_session_run_controller_admission_inspection_and_join()
{
  ava::app::SessionRunController controller(ephemeral_controller_target());
  expect(controller.inspect_admission({.request_id = "same"}) == ava::app::AdmissionDisposition::Admit, "inactive request is admissible");
  auto admitted = controller.admit({.request_id = "same"});
  expect(admitted.has_value(), "same-correlation fixture admits");
  if (!admitted)
    return;
  auto guard = std::move(*admitted);
  expect(controller.inspect_admission({.request_id = "same"}) == ava::app::AdmissionDisposition::JoinExistingOutcome,
         "same correlation has explicit join disposition");
  expect(controller.inspect_admission({.request_id = "other"}) == ava::app::AdmissionDisposition::RejectDifferentRequest,
         "different correlation has bounded reject disposition");
  std::optional<ava::app::RunOutcome> joined;
  std::jthread waiter = ava::core::make_jthread("waiter", [&] {
    auto outcome = controller.wait_outcome("same");
    if (outcome)
      joined = *outcome;
  });
  expect(controller.wake({.kind = ava::app::RunCommand::Kind::FollowUp, .correlation_id = "same", .message = "next"}).has_value(),
         "controller accepts a correlated follow-up command");
  auto commands = controller.take_commands("same", ava::app::RunCommand::Kind::FollowUp);
  expect(commands && commands->size() == 1 && commands->front().kind == ava::app::RunCommand::Kind::FollowUp,
         "follow-up has a non-dropping controller adapter");
  expect(guard.transition(ava::app::RunPhase::BuildingContext).has_value(), "same request enters context");
  expect(guard.transition(ava::app::RunPhase::AwaitingProvider).has_value(), "same request enters provider");
  expect(guard.transition(ava::app::RunPhase::Completing).has_value(), "same request enters completion");
  auto completed = guard.complete({.run_id = {}, .reason = ava::app::StopReason::Completed});
  expect(completed.has_value(), "same request completes for join waiter");
  if (!completed)
    guard = {};
  waiter.join();
  expect(joined && joined->reason == ava::app::StopReason::Completed, "same request waiter receives correlated terminal outcome");
}

void test_session_run_controller_stable_join_and_command_kinds()
{
  ava::app::SessionRunController controller(ephemeral_controller_target());
  auto a = controller.admit({.request_id = "A"});
  expect(a.has_value(), "stable join fixture admits A");
  if (!a)
    return;
  auto guard_a = std::move(*a);
  std::optional<ava::app::RunOutcome> joined;
  std::jthread waiter = ava::core::make_jthread("waiter", [&] {
    auto outcome = controller.wait_outcome("A");
    if (outcome)
      joined = *outcome;
  });
  expect(guard_a.transition(ava::app::RunPhase::BuildingContext).has_value(), "A enters context");
  expect(guard_a.transition(ava::app::RunPhase::AwaitingProvider).has_value(), "A enters provider");
  expect(guard_a.transition(ava::app::RunPhase::Completing).has_value(), "A enters completion");
  expect(guard_a.complete({.run_id = {}, .reason = ava::app::StopReason::Completed}).has_value(), "A completes");
  auto b = controller.admit({.request_id = "B"});
  expect(b.has_value(), "B can admit before A waiter is scheduled");
  waiter.join();
  expect(joined && joined->run_id == "A" && joined->reason == ava::app::StopReason::Completed, "A waiter retains A terminal outcome across B admission");

  if (!b)
    return;
  auto guard_b = std::move(*b);
  expect(controller.wake({.kind = ava::app::RunCommand::Kind::Steering, .correlation_id = "B", .message = "steer"}).has_value(), "steering queues");
  expect(controller.wake({.kind = ava::app::RunCommand::Kind::FollowUp, .correlation_id = "B", .message = "follow"}).has_value(), "follow-up queues");
  expect(controller.wake({.kind = ava::app::RunCommand::Kind::Wake, .correlation_id = "B", .message = "wake"}).has_value(), "wake queues");
  auto steering = controller.take_commands("B");
  expect(steering && steering->size() == 1 && steering->front().kind == ava::app::RunCommand::Kind::Steering, "steering adapter consumes only steering");
  auto follow_up = controller.take_commands("B", ava::app::RunCommand::Kind::FollowUp);
  auto wake = controller.take_commands("B", ava::app::RunCommand::Kind::Wake);
  expect(follow_up && follow_up->size() == 1 && wake && wake->size() == 1, "follow-up and wake survive mixed steering consumption");
}

void test_session_run_controller_effective_failure_and_stop_reentry()
{
  auto const root = create_empty_root("session-run-controller-latch");

  auto store = ava::session::SessionStore::create(std::filesystem::current_path(), root);
  expect(store.has_value(), "latch fixture store creates");
  if (!store)
    return;
  auto lease = ava::session::SessionLease::create_and_acquire(store->session_path());
  expect(lease.has_value(), "latch fixture acquires a lease");
  if (!lease)
    return;
  auto target = persistent_controller_target(*store, *lease);
  expect(target.has_value(), "latch fixture creates append target");
  if (!target)
    return;
  ava::app::SessionRunController controller(std::move(*target));
  auto admitted = controller.admit({.request_id = "failed"});
  expect(admitted.has_value(), "latch fixture admits");
  if (!admitted)
    return;
  auto guard = std::move(*admitted);
  std::atomic<bool> callback_reentered = false;
  std::stop_callback callback(guard.stop_token(), [&] { callback_reentered.store(controller.snapshot().stop_requested, std::memory_order_release); });
  auto invalid = append_entry("invalid");
  invalid.data_json.clear();
  expect(!guard.append_route()(std::move(invalid)).has_value(), "append failure latches");
  expect(callback_reentered.load(std::memory_order_acquire), "append failure stop callback reenters without deadlock");
  auto effective = guard.complete({.run_id = {}, .reason = ava::app::StopReason::ProviderError});
  expect(effective && effective->reason == ava::app::StopReason::PersistenceError && effective->error,
         "complete reports persistence as effective terminal outcome");
  expect(!controller.admit({.request_id = "blocked"}).has_value(), "persistence latch blocks admission");
  expect(controller.reset_persistence_failure().has_value(), "explicit terminal/drained recovery clears latch");
  auto recovered = controller.admit({.request_id = "recovered"});
  expect(recovered.has_value(), "admission resumes only after explicit recovery");
}

struct PartialAppendControl
{
  std::atomic_bool fail = true;
  std::atomic_int calls = 0;

  ssize_t write(int fd, std::string_view bytes)
  {
    if (!fail.load(std::memory_order_acquire))
      return ::write(fd, bytes.data(), bytes.size());
    if (calls.fetch_add(1, std::memory_order_acq_rel) == 0)
      return ::write(fd, bytes.data(), std::min<std::size_t>(1, bytes.size()));
    errno = EIO;
    return -1;
  }
};

void test_session_run_controller_recovers_persistent_and_ephemeral_targets()
{
  auto const root = create_empty_root("session-run-controller-recovery");

  auto const workspace = root / "workspace";
  auto store = ava::session::SessionStore::create(std::filesystem::current_path(), root);
  auto lease = store ? ava::session::SessionLease::create_and_acquire(store->session_path())
                     : ava::core::Result<ava::session::SessionLease>(std::unexpected(store.error()));
  expect(store && lease, "controller recovery fixture creates a persistent owned store");
  if (store && lease)
  {
    expect(store->append(*lease, append_entry("recovery-prefix")).has_value(), "controller recovery fixture seeds a framed record");
    auto control = std::make_shared<PartialAppendControl>();
    store->set_append_write_for_test([control](int fd, std::string_view bytes) { return control->write(fd, bytes); });
    auto target = persistent_controller_target(*store, *lease);
    if (target)
    {
      ava::app::SessionRunController controller(std::move(*target));
      auto owner = controller.owner_append_route();
      auto torn = owner(append_entry("recovery-torn"));
      expect(!torn, "partial persistent owner append latches a controller failure");
      auto recovered = controller.reset_persistence_failure();
      control->fail.store(false, std::memory_order_release);
      auto next = owner(append_entry("recovery-next"));
      auto entries = store->load();
      expect(recovered && next && entries && entries->size() == 2 && entries->back().id == "recovery-next",
             "controller recovery repairs a torn tail before clearing the latch so the next owner append succeeds");
    }
  }

  auto ephemeral = ava::session::SessionStore::create_ephemeral(root / "ephemeral-workspace");
  auto ephemeral_target = ephemeral ? ava::session::SessionAppendTarget::create_ephemeral(*ephemeral)
                                    : ava::core::Result<std::shared_ptr<ava::session::SessionAppendTarget>>(std::unexpected(ephemeral.error()));
  if (ephemeral_target)
  {
    ava::app::SessionRunController controller(std::move(*ephemeral_target));
    auto owner = controller.owner_append_route();
    auto invalid = append_entry("ephemeral-invalid");
    invalid.data_json.clear();
    auto failed = owner(std::move(invalid));
    auto recovered = controller.reset_persistence_failure();
    auto next = owner(append_entry("ephemeral-next"));
    expect(!failed && recovered && next, "ephemeral target recovery is an explicit no-op that clears only its controller latch");
  }
}

void test_session_run_controller_failed_and_inflight_recovery()
{
  auto const root = create_empty_root("session-run-controller-recovery-races");

  {
    auto store = ava::session::SessionStore::create(std::filesystem::current_path(), root / "failed");
    auto lease = store ? ava::session::SessionLease::create_and_acquire(store->session_path())
                       : ava::core::Result<ava::session::SessionLease>(std::unexpected(store.error()));
    if (store && lease)
    {
      expect(store->append(*lease, append_entry("failed-prefix")).has_value(), "failed recovery fixture seeds a framed record");
      auto control = std::make_shared<PartialAppendControl>();
      store->set_append_write_for_test([control](int fd, std::string_view bytes) { return control->write(fd, bytes); });
      auto target = persistent_controller_target(*store, *lease);
      if (target)
      {
        ava::app::SessionRunController controller(std::move(*target));
        auto owner = controller.owner_append_route();
        auto torn = owner(append_entry("failed-torn"));
        auto const parked = store->session_path().string() + ".parked";
        std::filesystem::rename(store->session_path(), parked);
        std::ofstream replacement(store->session_path());
        replacement << "replacement\n";
        replacement.close();
        auto recovered = controller.reset_persistence_failure();
        auto blocked = owner(append_entry("must-remain-blocked"));
        expect(!torn && !recovered && !blocked &&
                   controller.inspect_admission({.request_id = "blocked"}) == ava::app::AdmissionDisposition::RejectPersistenceFailure,
               "failed persistent recovery leaves the original latch authoritative and blocks later owner and run appends");
      }
    }
  }

  {
    auto store = ava::session::SessionStore::create(std::filesystem::current_path(), root / "inflight");
    auto lease = store ? ava::session::SessionLease::create_and_acquire(store->session_path())
                       : ava::core::Result<ava::session::SessionLease>(std::unexpected(store.error()));
    if (store && lease)
    {
      expect(store->append(*lease, append_entry("inflight-prefix")).has_value(), "in-flight recovery fixture seeds a framed record");
      auto control = std::make_shared<PartialAppendControl>();
      std::mutex hook_mutex;
      std::condition_variable hook_cv;
      bool entered = false;
      bool release = false;
      store->set_before_append_identity_check_for_test([&] {
        std::unique_lock lock(hook_mutex);
        entered = true;
        hook_cv.notify_all();
        static_cast<void>(hook_cv.wait_for(lock, std::chrono::seconds(3), [&] { return release; }));
      });
      store->set_append_write_for_test([control](int fd, std::string_view bytes) { return control->write(fd, bytes); });
      auto target = persistent_controller_target(*store, *lease);
      if (target)
      {
        ava::app::SessionRunController controller(std::move(*target));
        auto owner = controller.owner_append_route();
        std::optional<ava::core::VoidResult> append_result;
        std::optional<ava::core::VoidResult> reset_result;
        std::jthread writer = ava::core::make_jthread("writer", [&] { append_result.emplace(owner(append_entry("inflight-torn"))); });
        {
          std::unique_lock lock(hook_mutex);
          expect(hook_cv.wait_for(lock, std::chrono::seconds(3), [&] { return entered; }), "in-flight append reaches deterministic write gate");
        }
        std::jthread resetter = ava::core::make_jthread("resetter", [&] { reset_result.emplace(controller.reset_persistence_failure()); });
        {
          std::lock_guard lock(hook_mutex);
          release = true;
        }
        hook_cv.notify_all();
        writer.join();
        resetter.join();
        control->fail.store(false, std::memory_order_release);
        auto next = owner(append_entry("inflight-next"));
        expect(append_result && !*append_result && reset_result && *reset_result && next,
               "in-flight reset serializes behind the append driver, repairs its partial failure, and permits the next append");
      }
    }
  }
}

void test_session_run_controller_shutdown_during_recovery_and_queued_append()
{
  auto const root = create_empty_root("session-run-controller-shutdown-races");

  {
    auto store = ava::session::SessionStore::create(std::filesystem::current_path(), root / "reset");
    auto lease = store ? ava::session::SessionLease::create_and_acquire(store->session_path())
                       : ava::core::Result<ava::session::SessionLease>(std::unexpected(store.error()));
    if (store && lease)
    {
      expect(store->append(*lease, append_entry("reset-prefix")).has_value(), "shutdown/reset fixture seeds a framed record");
      auto control = std::make_shared<PartialAppendControl>();
      std::mutex hook_mutex;
      std::condition_variable hook_cv;
      bool recovery_entered = false;
      bool release_recovery = false;
      store->set_append_write_for_test([control](int fd, std::string_view bytes) { return control->write(fd, bytes); });
      store->set_after_recovery_quarantine_publication_for_test([&] {
        std::unique_lock lock(hook_mutex);
        recovery_entered = true;
        hook_cv.notify_all();
        static_cast<void>(hook_cv.wait_for(lock, std::chrono::seconds(3), [&] { return release_recovery; }));
      });
      auto target = persistent_controller_target(*store, *lease);
      if (target)
      {
        ava::app::SessionRunController controller(std::move(*target));
        auto owner = controller.owner_append_route();
        expect(!owner(append_entry("reset-torn")), "shutdown/reset fixture creates a partial persistence latch");
        std::optional<ava::core::VoidResult> reset_result;
        std::jthread resetter = ava::core::make_jthread("resetter", [&] { reset_result.emplace(controller.reset_persistence_failure()); });
        {
          std::unique_lock lock(hook_mutex);
          expect(hook_cv.wait_for(lock, std::chrono::seconds(3), [&] { return recovery_entered; }), "recovery reaches deterministic publication gate");
        }
        std::jthread shutdown = ava::core::make_jthread("shutdown", [&] { controller.shutdown(); });
        auto const deadline = ava::tests::now_plus_seconds(3);
        while (controller.inspect_admission({.request_id = "closing"}) != ava::app::AdmissionDisposition::RejectClosing &&
               std::chrono::steady_clock::now() < deadline)
          std::this_thread::yield();
        {
          std::lock_guard lock(hook_mutex);
          release_recovery = true;
        }
        hook_cv.notify_all();
        resetter.join();
        shutdown.join();
        expect(reset_result && !*reset_result && !owner(append_entry("stale-after-reset-shutdown")),
               "shutdown racing recovery revokes authority, prevents latch clearing, and completes without lock inversion");
      }
    }
  }

  {
    auto store = ava::session::SessionStore::create(std::filesystem::current_path(), root / "queued");
    auto lease = store ? ava::session::SessionLease::create_and_acquire(store->session_path())
                       : ava::core::Result<ava::session::SessionLease>(std::unexpected(store.error()));
    if (store && lease)
    {
      std::mutex hook_mutex;
      std::condition_variable hook_cv;
      bool active_entered = false;
      bool release_active = false;
      store->set_before_append_identity_check_for_test([&] {
        std::unique_lock lock(hook_mutex);
        if (active_entered)
          return;
        active_entered = true;
        hook_cv.notify_all();
        static_cast<void>(hook_cv.wait_for(lock, std::chrono::seconds(3), [&] { return release_active; }));
      });
      auto target = persistent_controller_target(*store, *lease);
      if (target)
      {
        ava::app::SessionRunController controller(std::move(*target));
        auto admitted = controller.admit({.request_id = "shutdown-queued"});
        if (admitted)
        {
          auto guard = std::move(*admitted);
          auto active = guard.append_route();
          auto owner = controller.owner_append_route();
          std::optional<ava::core::VoidResult> active_result;
          std::optional<ava::core::VoidResult> queued_result;
          std::jthread first = ava::core::make_jthread("first", [&] { active_result.emplace(active(append_entry("shutdown-active"))); });
          {
            std::unique_lock lock(hook_mutex);
            expect(hook_cv.wait_for(lock, std::chrono::seconds(3), [&] { return active_entered; }), "active append reaches shutdown gate");
          }
          std::jthread second = ava::core::make_jthread("second", [&] { queued_result.emplace(owner(append_entry("shutdown-queued"))); });
          auto const queue_deadline = ava::tests::now_plus_seconds(3);
          while (controller.snapshot().queued_appends < 2 && std::chrono::steady_clock::now() < queue_deadline) std::this_thread::yield();
          std::jthread shutdown = ava::core::make_jthread("shutdown", [&] { controller.shutdown(); });
          auto const close_deadline = ava::tests::now_plus_seconds(3);
          while (controller.inspect_admission({.request_id = "closing"}) != ava::app::AdmissionDisposition::RejectClosing &&
                 std::chrono::steady_clock::now() < close_deadline)
            std::this_thread::yield();
          {
            std::lock_guard lock(hook_mutex);
            release_active = true;
          }
          hook_cv.notify_all();
          first.join();
          second.join();
          shutdown.join();
          auto outcome = guard.complete({.run_id = {}, .reason = ava::app::StopReason::ProviderError});
          auto entries = store->load();
          expect(active_result && *active_result && queued_result && entries && entries->size() >= 1 && entries->size() <= 2 && outcome &&
                     outcome->reason != ava::app::StopReason::PersistenceError && !owner(append_entry("shutdown-stale")),
                 "shutdown preserves the accepted active append, completes a queued caller, releases authority, and never synthesizes a persistence latch");
        }
      }
    }
  }
}

void test_session_run_controller_cross_thread_observer_shutdown_is_nonblocking()
{
  struct ShutdownState
  {
    std::mutex mutex;
    std::condition_variable changed;
    bool append_at_hook = false;
    bool release_append = false;
    bool callback_entered = false;
    bool call_shutdown = false;
    bool shutdown_calling = false;
    bool shutdown_returned = false;
    bool first_done = false;
    bool second_done = false;
    bool callback_context = false;
    std::optional<ava::core::VoidResult> first_result;
    std::optional<ava::core::VoidResult> second_result;
  };

  auto const root = create_empty_root("session-run-controller-observer-shutdown");

  auto store = ava::session::SessionStore::create(std::filesystem::current_path(), root);
  auto lease = store ? ava::session::SessionLease::create_and_acquire(store->session_path())
                     : ava::core::Result<ava::session::SessionLease>(std::unexpected(store.error()));
  expect(store && lease, "cross-thread observer shutdown fixture creates a persistent owned session");
  if (!store || !lease)
    return;

  auto state = std::make_shared<ShutdownState>();
  store->set_after_append_write_for_test([state] {
    std::unique_lock lock(state->mutex);
    if (state->append_at_hook)
      return;
    state->append_at_hook = true;
    state->changed.notify_all();
    static_cast<void>(state->changed.wait_for(lock, std::chrono::seconds(3), [&] { return state->release_append; }));
  });

  auto controller_slot = std::make_shared<std::shared_ptr<ava::app::SessionRunController>>();
  auto observer = std::make_shared<CallbackRunObserver>([state, controller_slot](ava::observability::TraceEvent const& event) {
    if (event.type != ava::observability::TraceEventType::AgentRunStart)
      return;
    {
      std::unique_lock lock(state->mutex);
      state->callback_context = ava::observability::in_run_observer_callback();
      state->callback_entered = true;
      state->changed.notify_all();
      static_cast<void>(state->changed.wait_for(lock, std::chrono::seconds(3), [&] { return state->call_shutdown; }));
      state->shutdown_calling = true;
    }
    state->changed.notify_all();
    if (auto controller = *controller_slot)
      controller->shutdown();
    {
      std::lock_guard lock(state->mutex);
      state->shutdown_returned = true;
    }
    state->changed.notify_all();
  });
  auto observation = std::make_shared<ava::observability::RunObservation>(observer);
  static_cast<void>(store->set_run_observation(observation, {}));

  auto target = persistent_controller_target(*store, *lease);
  expect(target.has_value(), "cross-thread observer shutdown fixture creates append target");
  if (!target)
    return;
  auto controller = std::make_shared<ava::app::SessionRunController>(std::move(*target));
  *controller_slot = controller;
  auto owner = controller->owner_append_route();
  auto first_entry = append_entry("observer-shutdown-active");
  auto second_entry = append_entry("observer-shutdown-queued");
  auto const expected_bytes = first_entry.id.size() + first_entry.parent_id.size() + first_entry.timestamp.size() + first_entry.data_json.size() + 32 +
                              second_entry.id.size() + second_entry.parent_id.size() + second_entry.timestamp.size() + second_entry.data_json.size() + 32;

  std::thread first([state, owner, entry = std::move(first_entry)]() mutable {
    auto result = owner(std::move(entry));
    {
      std::lock_guard lock(state->mutex);
      state->first_result.emplace(std::move(result));
      state->first_done = true;
    }
    state->changed.notify_all();
  });
  bool append_reached_hook = false;
  {
    std::unique_lock lock(state->mutex);
    append_reached_hook = state->changed.wait_for(lock, std::chrono::seconds(3), [&] { return state->append_at_hook; });
  }
  expect(append_reached_hook, "append driver reaches the post-write gate while owning append serialization");
  if (!append_reached_hook)
  {
    {
      std::lock_guard lock(state->mutex);
      state->release_append = true;
    }
    state->changed.notify_all();
    first.join();
    return;
  }

  std::thread second([state, owner, entry = std::move(second_entry)]() mutable {
    auto result = owner(std::move(entry));
    {
      std::lock_guard lock(state->mutex);
      state->second_result.emplace(std::move(result));
      state->second_done = true;
    }
    state->changed.notify_all();
  });
  auto const queue_deadline = ava::tests::now_plus_seconds(3);
  auto queued = controller->snapshot();
  while (queued.queued_appends < 2 && std::chrono::steady_clock::now() < queue_deadline)
  {
    std::this_thread::yield();
    queued = controller->snapshot();
  }
  expect(queued.queued_appends == 2 && queued.queued_append_bytes == expected_bytes,
         "a second accepted ticket waits behind the append driver with exact byte accounting");

  std::thread emitter([observation] { observation->emit(ava::observability::TraceEventType::AgentRunStart, {}); });
  bool callback_entered = false;
  {
    std::unique_lock lock(state->mutex);
    callback_entered = state->changed.wait_for(lock, std::chrono::seconds(3), [&] { return state->callback_entered; });
    state->call_shutdown = true;
  }
  state->changed.notify_all();
  expect(callback_entered, "unrelated producer enters the user observer while the append driver is gated");

  bool shutdown_calling = false;
  {
    std::unique_lock lock(state->mutex);
    shutdown_calling = state->changed.wait_for(lock, std::chrono::seconds(3), [&] { return state->shutdown_calling; });
    state->release_append = true;
  }
  state->changed.notify_all();
  expect(shutdown_calling, "observer callback starts shutdown before the append may emit its result");

  bool completed = false;
  {
    std::unique_lock lock(state->mutex);
    completed = state->changed.wait_for(lock, std::chrono::seconds(3), [&] { return state->shutdown_returned && state->first_done && state->second_done; });
  }
  if (!completed)
  {
    first.detach();
    second.detach();
    emitter.detach();
    expect(false, "observer shutdown and every accepted append ticket terminate without emit_mutex/append_mutex deadlock");
    return;
  }
  first.join();
  second.join();
  emitter.join();

  bool terminal_results = false;
  bool callback_context = false;
  {
    std::lock_guard lock(state->mutex);
    terminal_results = state->first_result && *state->first_result && state->second_result && !*state->second_result;
    callback_context = state->callback_context;
  }
  auto terminal = controller->snapshot();
  *lease = ava::session::SessionLease{};
  auto reacquired = ava::session::SessionLease::acquire(store->session_path());
  expect(terminal_results && callback_context && terminal.queued_appends == 0 && terminal.queued_append_bytes == 0 && reacquired &&
             !owner(append_entry("observer-shutdown-stale")),
         "observer shutdown returns nonblocking, commits only the in-flight append, drains queued work, and releases target lease authority");
}

void test_session_run_controller_observer_reset_is_immediate_for_unrelated_observer()
{
  struct ResetState
  {
    std::mutex mutex;
    std::condition_variable changed;
    bool append_at_hook = false;
    bool release_append = false;
    bool callback_returned = false;
    bool writer_done = false;
    bool callback_context = false;
    std::optional<ava::core::VoidResult> reset_result;
    std::optional<ava::core::VoidResult> append_result;
  };

  auto const root = create_empty_root("session-run-controller-observer-reset");

  auto store = ava::session::SessionStore::create(std::filesystem::current_path(), root);
  auto lease = store ? ava::session::SessionLease::create_and_acquire(store->session_path())
                     : ava::core::Result<ava::session::SessionLease>(std::unexpected(store.error()));
  expect(store && lease, "observer reset fixture creates a persistent owned session");
  if (!store || !lease)
    return;

  auto state = std::make_shared<ResetState>();
  store->set_after_append_write_for_test([state] {
    std::unique_lock lock(state->mutex);
    state->append_at_hook = true;
    state->changed.notify_all();
    static_cast<void>(state->changed.wait_for(lock, std::chrono::seconds(3), [&] { return state->release_append; }));
  });
  auto target = persistent_controller_target(*store, *lease);
  if (!target)
    return;
  auto controller = std::make_shared<ava::app::SessionRunController>(std::move(*target));
  auto owner = controller->owner_append_route();

  std::thread writer([state, owner] {
    auto result = owner(append_entry("observer-reset-active"));
    {
      std::lock_guard lock(state->mutex);
      state->append_result.emplace(std::move(result));
      state->writer_done = true;
    }
    state->changed.notify_all();
  });
  bool append_reached_hook = false;
  {
    std::unique_lock lock(state->mutex);
    append_reached_hook = state->changed.wait_for(lock, std::chrono::seconds(3), [&] { return state->append_at_hook; });
  }

  auto unrelated =
      std::make_shared<ava::observability::RunObservation>(std::make_shared<CallbackRunObserver>([state, controller](ava::observability::TraceEvent const&) {
        auto reset = controller->reset_persistence_failure();
        {
          std::lock_guard lock(state->mutex);
          state->callback_context = ava::observability::in_run_observer_callback();
          state->reset_result.emplace(std::move(reset));
          state->callback_returned = true;
        }
        state->changed.notify_all();
      }));
  std::thread emitter([unrelated] { unrelated->emit(ava::observability::TraceEventType::AgentRunStart, {}); });

  bool callback_returned_while_driver_gated = false;
  {
    std::unique_lock lock(state->mutex);
    callback_returned_while_driver_gated = state->changed.wait_for(lock, std::chrono::seconds(3), [&] { return state->callback_returned; });
    state->release_append = true;
  }
  state->changed.notify_all();
  writer.join();
  emitter.join();

  bool stable_rejection = false;
  bool append_succeeded = false;
  bool callback_context = false;
  {
    std::lock_guard lock(state->mutex);
    stable_rejection =
        state->reset_result && !*state->reset_result && state->reset_result->error().message() == "cannot reset persistence from a session append observer";
    append_succeeded = state->append_result && *state->append_result;
    callback_context = state->callback_context;
  }
  expect(append_reached_hook && callback_returned_while_driver_gated && callback_context && stable_rejection && append_succeeded,
         "reset from any user observer rejects with its stable error before waiting on append serialization");
  controller->shutdown();
}

void test_session_run_controller_nested_controller_membership_is_stack_aware()
{
  struct NestedState
  {
    std::mutex mutex;
    std::condition_variable changed;
    std::function<ava::core::VoidResult(ava::session::SessionEntry)> route_a;
    std::function<ava::core::VoidResult(ava::session::SessionEntry)> route_b;
    std::optional<ava::core::VoidResult> recursive_a_result;
    std::optional<ava::core::VoidResult> nested_b_result;
    std::optional<ava::core::VoidResult> outer_a_result;
    bool entered_a = false;
    bool entered_b = false;
    bool outer_done = false;
  };

  auto state = std::make_shared<NestedState>();
  auto const root = create_empty_root("test_session_run_controller_nested_controller_membership_is_stack_aware");

  auto store_a = ava::session::SessionStore::create_ephemeral(root / "controller-nested-a");
  auto store_b = ava::session::SessionStore::create_ephemeral(root / "controller-nested-b");
  expect(store_a && store_b, "nested controller fixture creates two ephemeral sessions");
  if (!store_a || !store_b)
    return;

  auto observer_a = std::make_shared<CallbackRunObserver>([state](ava::observability::TraceEvent const& event) {
    if (event.type != ava::observability::TraceEventType::SessionAppendAttempt)
      return;
    {
      std::lock_guard lock(state->mutex);
      if (state->entered_a)
        return;
      state->entered_a = true;
    }
    auto result = state->route_b(append_entry("nested-b"));
    std::lock_guard lock(state->mutex);
    state->nested_b_result.emplace(std::move(result));
  });
  auto observer_b = std::make_shared<CallbackRunObserver>([state](ava::observability::TraceEvent const& event) {
    if (event.type != ava::observability::TraceEventType::SessionAppendAttempt)
      return;
    {
      std::lock_guard lock(state->mutex);
      if (state->entered_b)
        return;
      state->entered_b = true;
    }
    auto result = state->route_a(append_entry("recursive-a"));
    std::lock_guard lock(state->mutex);
    state->recursive_a_result.emplace(std::move(result));
  });
  static_cast<void>(store_a->set_run_observation(std::make_shared<ava::observability::RunObservation>(observer_a), {}));
  static_cast<void>(store_b->set_run_observation(std::make_shared<ava::observability::RunObservation>(observer_b), {}));
  auto target_a = ava::session::SessionAppendTarget::create_ephemeral(*store_a);
  auto target_b = ava::session::SessionAppendTarget::create_ephemeral(*store_b);
  expect(target_a && target_b, "nested controller fixture creates two append targets");
  if (!target_a || !target_b)
    return;
  auto controller_a = std::make_shared<ava::app::SessionRunController>(std::move(*target_a));
  auto controller_b = std::make_shared<ava::app::SessionRunController>(std::move(*target_b));
  state->route_a = controller_a->owner_append_route();
  state->route_b = controller_b->owner_append_route();

  std::thread writer([state, controller_a, controller_b] {
    auto result = state->route_a(append_entry("outer-a"));
    {
      std::lock_guard lock(state->mutex);
      state->outer_a_result.emplace(std::move(result));
      state->outer_done = true;
    }
    state->changed.notify_all();
  });
  bool completed = false;
  {
    std::unique_lock lock(state->mutex);
    completed = state->changed.wait_for(lock, std::chrono::seconds(3), [&] { return state->outer_done; });
  }
  if (!completed)
  {
    writer.detach();
    expect(false, "nested A to B to A callback rejects instead of recursively waiting on A append serialization");
    return;
  }
  writer.join();

  bool recursive_rejected = false;
  bool nested_succeeded = false;
  bool outer_succeeded = false;
  {
    std::lock_guard lock(state->mutex);
    recursive_rejected =
        state->recursive_a_result && !*state->recursive_a_result && state->recursive_a_result->error().message() == "reentrant session append is not allowed";
    nested_succeeded = state->nested_b_result && *state->nested_b_result;
    outer_succeeded = state->outer_a_result && *state->outer_a_result;
  }
  auto entries_a = store_a->load();
  auto entries_b = store_b->load();
  expect(recursive_rejected && nested_succeeded && outer_succeeded && entries_a && entries_a->size() == 1 && entries_b && entries_b->size() == 1,
         "active-controller membership retains outer A across nested B and preserves both nonrecursive FIFO appends");
  controller_a->shutdown();
  controller_b->shutdown();
  state->route_a = {};
  state->route_b = {};
}

void test_session_run_controller_unrelated_observer_shutdown_without_active_append()
{
  struct ShutdownState
  {
    std::mutex mutex;
    std::condition_variable changed;
    bool returned = false;
    bool callback_context = false;
  };

  auto const root = create_empty_root("session-run-controller-unrelated-observer-shutdown");

  auto store = ava::session::SessionStore::create(std::filesystem::current_path(), root);
  auto lease = store ? ava::session::SessionLease::create_and_acquire(store->session_path())
                     : ava::core::Result<ava::session::SessionLease>(std::unexpected(store.error()));
  expect(store && lease, "unrelated observer shutdown fixture creates a persistent owned session");
  if (!store || !lease)
    return;
  auto target = persistent_controller_target(*store, *lease);
  if (!target)
    return;
  auto controller = std::make_shared<ava::app::SessionRunController>(std::move(*target));
  auto owner = controller->owner_append_route();
  auto state = std::make_shared<ShutdownState>();
  auto observation =
      std::make_shared<ava::observability::RunObservation>(std::make_shared<CallbackRunObserver>([state, controller](ava::observability::TraceEvent const&) {
        controller->shutdown();
        {
          std::lock_guard lock(state->mutex);
          state->callback_context = ava::observability::in_run_observer_callback();
          state->returned = true;
        }
        state->changed.notify_all();
      }));

  std::thread emitter([observation] { observation->emit(ava::observability::TraceEventType::AgentRunStart, {}); });
  bool returned = false;
  {
    std::unique_lock lock(state->mutex);
    returned = state->changed.wait_for(lock, std::chrono::seconds(3), [&] { return state->returned; });
  }
  emitter.join();
  *lease = ava::session::SessionLease{};
  auto reacquired = ava::session::SessionLease::acquire(store->session_path());
  expect(returned && state->callback_context && reacquired && !owner(append_entry("unrelated-observer-stale")),
         "an unrelated observer finalizes an idle controller immediately and copied routes retain no target lease");
}

void test_session_run_controller_failure_drains_tickets_with_exact_accounting()
{
  auto const root = create_empty_root("session-run-controller-failure-tickets");

  auto store = ava::session::SessionStore::create(std::filesystem::current_path(), root);
  auto lease = store ? ava::session::SessionLease::create_and_acquire(store->session_path())
                     : ava::core::Result<ava::session::SessionLease>(std::unexpected(store.error()));
  if (!store || !lease)
    return;
  expect(store->append(*lease, append_entry("ticket-prefix")).has_value(), "failure ticket fixture seeds a framed record");

  std::mutex gate_mutex;
  std::condition_variable gate_changed;
  bool entered = false;
  bool release = false;
  store->set_before_append_identity_check_for_test([&] {
    std::unique_lock lock(gate_mutex);
    if (entered)
      return;
    entered = true;
    gate_changed.notify_all();
    static_cast<void>(gate_changed.wait_for(lock, std::chrono::seconds(3), [&] { return release; }));
  });
  auto control = std::make_shared<PartialAppendControl>();
  store->set_append_write_for_test([control](int fd, std::string_view bytes) { return control->write(fd, bytes); });
  auto target = persistent_controller_target(*store, *lease);
  if (!target)
    return;
  ava::app::SessionRunController controller(std::move(*target));
  auto admitted = controller.admit({.request_id = "failure-tickets"});
  if (!admitted)
    return;
  auto guard = std::move(*admitted);
  auto route = guard.append_route();

  auto first_entry = append_entry("ticket-first");
  auto second_entry = append_entry("ticket-second");
  auto third_entry = append_entry("ticket-third");
  auto bytes = [](ava::session::SessionEntry const& entry) {
    return entry.id.size() + entry.parent_id.size() + entry.timestamp.size() + entry.data_json.size() + 32;
  };
  auto const expected_bytes = bytes(first_entry) + bytes(second_entry) + bytes(third_entry);
  std::optional<ava::core::VoidResult> first_result;
  std::optional<ava::core::VoidResult> second_result;
  std::optional<ava::core::VoidResult> third_result;
  std::jthread first = ava::core::make_jthread("first", [&] { first_result.emplace(route(std::move(first_entry))); });
  {
    std::unique_lock lock(gate_mutex);
    expect(gate_changed.wait_for(lock, std::chrono::seconds(3), [&] { return entered; }), "failure ticket first append reaches its write gate");
  }
  std::jthread second = ava::core::make_jthread("second", [&] { second_result.emplace(route(std::move(second_entry))); });
  std::jthread third = ava::core::make_jthread("third", [&] { third_result.emplace(route(std::move(third_entry))); });
  auto const deadline = ava::tests::now_plus_seconds(3);
  auto queued = controller.snapshot();
  while (queued.queued_appends < 3 && std::chrono::steady_clock::now() < deadline)
  {
    std::this_thread::yield();
    queued = controller.snapshot();
  }
  expect(queued.queued_appends == 3 && queued.queued_append_bytes == expected_bytes,
         "accepted append tickets expose exact aggregate queue byte accounting before persistence");
  {
    std::lock_guard lock(gate_mutex);
    release = true;
  }
  gate_changed.notify_all();
  first.join();
  second.join();
  third.join();

  auto same_failure = [&] {
    return first_result && second_result && third_result && !*first_result && !*second_result && !*third_result &&
           first_result->error().format() == second_result->error().format() && first_result->error().format() == third_result->error().format() &&
           first_result->error().format().find("append_commit_state: partial_or_unknown") != std::string::npos;
  };
  auto drained = controller.snapshot();
  auto outcome = guard.complete({.run_id = {}, .reason = ava::app::StopReason::ProviderError});
  control->fail.store(false, std::memory_order_release);
  auto recovered = controller.reset_persistence_failure();
  auto next = controller.append(append_entry("ticket-after-recovery"));
  auto entries = store->load();
  expect(same_failure() && control->calls.load(std::memory_order_acquire) == 2 && drained.queued_appends == 0 && drained.queued_append_bytes == 0 && outcome &&
             outcome->reason == ava::app::StopReason::PersistenceError && recovered && next && entries && entries->size() == 2,
         "one immutable partial failure terminally drains every accepted ticket, prevents queued writes, and recovers without byte-account drift");
}

void test_session_run_controller_batch_is_one_ticket_and_latches_failure()
{
  auto const root = create_empty_root("session-run-controller-batch");
  auto store = ava::session::SessionStore::create(std::filesystem::current_path(), root);
  auto lease = store ? ava::session::SessionLease::create_and_acquire(store->session_path())
                     : ava::core::Result<ava::session::SessionLease>(std::unexpected(store.error()));
  if (!store || !lease)
    return;

  std::mutex gate_mutex;
  std::condition_variable gate_changed;
  bool entered = false;
  bool release = false;
  store->set_before_append_identity_check_for_test([&] {
    std::unique_lock lock(gate_mutex);
    entered = true;
    gate_changed.notify_all();
    static_cast<void>(gate_changed.wait_for(lock, std::chrono::seconds(3), [&] { return release; }));
  });
  store->set_append_write_for_test([](int, std::string_view) -> ssize_t {
    errno = EIO;
    return -1;
  });

  auto target = persistent_controller_target(*store, *lease);
  if (!target)
    return;
  ava::app::SessionRunController controller(std::move(*target));
  auto admitted = controller.admit({.request_id = "batch"});
  if (!admitted)
    return;
  auto guard = std::move(*admitted);
  auto route = guard.append_batch_route();
  auto output_data = ava::session::serialize_assistant_output_item_data_json(ava::session::AssistantOutputItem{
      .assistant_turn_id = "controller-batch-turn",
      .sequence = 0,
      .kind = ava::session::AssistantOutputItemKind::Text,
      .provider_item_id = "controller-batch-item",
      .provider_output_index = 0,
      .payload = ava::session::AssistantOutputText{.text = "staged", .assistant_phase = ava::session::AssistantOutputTextPhase::Commentary}});
  auto commit_data = ava::session::serialize_assistant_turn_commit_data_json(ava::session::AssistantTurnCommit{.assistant_turn_id = "controller-batch-turn",
                                                                                                               .item_count = 1,
                                                                                                               .provider = "openai",
                                                                                                               .model = "gpt-5.5",
                                                                                                               .finish_reason = "completed",
                                                                                                               .usage_json = std::nullopt});
  if (!output_data || !commit_data)
    return;
  std::vector<ava::session::SessionEntry> entries;
  auto output = append_entry("batch-output");
  output.type = ava::session::EntryType::AssistantOutputItem;
  output.data_json = std::move(*output_data);
  entries.push_back(std::move(output));
  auto commit = append_entry("batch-commit");
  commit.type = ava::session::EntryType::AssistantTurnCommit;
  commit.data_json = std::move(*commit_data);
  entries.push_back(std::move(commit));
  auto bytes = [](ava::session::SessionEntry const& entry) {
    return entry.id.size() + entry.parent_id.size() + entry.timestamp.size() + entry.data_json.size() + 32;
  };
  auto const expected_bytes = bytes(entries[0]) + bytes(entries[1]);
  std::optional<ava::core::VoidResult> result;
  std::jthread writer = ava::core::make_jthread("writer", [&] { result.emplace(route(std::move(entries))); });
  bool reached_gate = false;
  {
    std::unique_lock lock(gate_mutex);
    reached_gate = gate_changed.wait_for(lock, std::chrono::seconds(3), [&] { return entered; });
  }
  auto queued = controller.snapshot();
  {
    std::lock_guard lock(gate_mutex);
    release = true;
  }
  gate_changed.notify_all();
  writer.join();

  auto outcome = guard.complete({.run_id = {}, .reason = ava::app::StopReason::ProviderError});
  auto drained = controller.snapshot();
  expect(reached_gate && queued.queued_appends == 1 && queued.queued_append_bytes == expected_bytes && result && !*result && outcome &&
             outcome->reason == ava::app::StopReason::PersistenceError && drained.queued_appends == 0 && drained.queued_append_bytes == 0 &&
             controller.inspect_admission({.request_id = "blocked"}) == ava::app::AdmissionDisposition::RejectPersistenceFailure,
         "one session append batch reserves one bounded controller ticket and latches a persistence failure exactly like a single append");
}

void test_session_run_controller_concurrent_fifo_appends()
{
  auto const root = create_empty_root("session-run-controller-fifo");

  auto store = ava::session::SessionStore::create(std::filesystem::current_path(), root);
  expect(store.has_value(), "fifo fixture store creates");
  if (!store)
    return;
  auto lease = ava::session::SessionLease::create_and_acquire(store->session_path());
  expect(lease.has_value(), "fifo fixture acquires a lease");
  if (!lease)
    return;
  auto target = persistent_controller_target(*store, *lease);
  expect(target.has_value(), "fifo fixture creates append target");
  if (!target)
    return;
  ava::app::SessionRunController controller(std::move(*target));
  auto admitted = controller.admit({.request_id = "fifo"});
  expect(admitted.has_value(), "fifo fixture admits");
  if (!admitted)
    return;
  auto guard = std::move(*admitted);
  auto active = guard.append_route();
  auto owner = controller.owner_append_route();
  std::atomic<int> accepted = 0;
  std::vector<std::jthread> workers;
  for (int i = 0; i < 16; ++i)
    workers.emplace_back([&, i] {
      if ((i % 2 ? active : owner)(append_entry("fifo-" + std::to_string(i))))
        ++accepted;
    });
  for (auto& worker : workers) worker.join();
  auto entries = store->load();
  expect(accepted == 16 && entries && entries->size() == 16, "concurrent owner and generation appends retain every accepted record");
  if (entries)
  {
    std::vector<std::string> ids;
    for (auto const& entry : *entries) ids.push_back(entry.id);
    std::sort(ids.begin(), ids.end());
    expect(std::ranges::adjacent_find(ids) == ids.end(), "concurrent accepted entries retain unique ids");
  }
}

void test_session_run_controller_routes_release_target_on_shutdown()
{
  auto const root = create_empty_root("test_session_run_controller_routes_release_target_on_shutdown");
  auto store = ava::session::SessionStore::create_ephemeral(root / "controller-route-teardown");
  expect(store.has_value(), "sessionless route teardown fixture creates an ephemeral store");
  if (!store)
    return;
  auto target = ava::session::SessionAppendTarget::create_ephemeral(*store);
  expect(target.has_value(), "sessionless route teardown fixture creates an append target");
  if (!target)
    return;
  ava::app::SessionRunController controller(std::move(*target));
  auto admitted = controller.admit({.request_id = "shutdown"});
  expect(admitted.has_value(), "sessionless controller admits an active generation");
  if (!admitted)
    return;
  auto guard = std::move(*admitted);
  auto owner = controller.owner_append_route();
  auto active = guard.append_route();
  expect(owner(append_entry("sessionless-owner")).has_value() && active(append_entry("sessionless-active")).has_value(),
         "sessionless owner and active routes append through the immutable target");

  std::mutex mutex;
  std::condition_variable ready;
  bool writer_ready = false;
  bool release_writer = false;
  std::optional<ava::core::VoidResult> racing_result;
  std::jthread writer = ava::core::make_jthread("writer", [&] {
    std::unique_lock lock(mutex);
    writer_ready = true;
    ready.notify_all();
    ready.wait(lock, [&] { return release_writer; });
    lock.unlock();
    racing_result.emplace(owner(append_entry("racing-owner")));
  });
  {
    std::unique_lock lock(mutex);
    ready.wait(lock, [&] { return writer_ready; });
    release_writer = true;
  }
  ready.notify_all();
  controller.shutdown();
  writer.join();
  expect(racing_result.has_value(), "route-vs-teardown racing append completes or fails without retaining a runtime reference");
  expect(!owner(append_entry("stale-owner")).has_value() && !active(append_entry("stale-active")).has_value(),
         "owner and active route copies reject after teardown clears the append target");
  auto entries = store->load();
  expect(entries && entries->size() >= 2 && entries->size() <= 3, "teardown either drains or fails the one concurrent accepted append without data loss");
}

void test_session_run_controller_bounds_and_reentrant_snapshot()
{
  ava::app::SessionRunController controller(ephemeral_controller_target());
  auto admitted = controller.admit({.request_id = "run"});
  expect(admitted.has_value(), "controller admits bounded queue fixture");
  if (!admitted)
    return;
  auto guard = std::move(*admitted);
  for (std::size_t index = 0; index < ava::app::kMaxSessionRunCommands; ++index)
    expect(controller.wake({.kind = ava::app::RunCommand::Kind::Wake, .correlation_id = {}, .message = "x"}).has_value(),
           "controller accepts each bounded command");
  expect(!controller.wake({.kind = ava::app::RunCommand::Kind::Wake, .correlation_id = {}, .message = "overflow"}).has_value(),
         "controller rejects command queue overflow");
  auto snapshot = controller.snapshot();
  expect(snapshot.active && snapshot.queued_commands == ava::app::kMaxSessionRunCommands,
         "snapshot is reentrant and observes controller state without callback locking");
}

}  // namespace

void run_session_run_controller_tests()
{
  test_session_run_controller_transitions_and_guard_release();
  test_session_run_controller_concurrent_admit_wake_stop();
  test_session_run_controller_generation_and_cancel_precedence();
  test_session_run_controller_owner_and_generation_routes();
  test_runtime_session_destruction_does_not_own_background_coordinator();
  test_session_run_controller_admission_inspection_and_join();
  test_session_run_controller_stable_join_and_command_kinds();
  test_session_run_controller_effective_failure_and_stop_reentry();
  test_session_run_controller_recovers_persistent_and_ephemeral_targets();
  test_session_run_controller_failed_and_inflight_recovery();
  test_session_run_controller_shutdown_during_recovery_and_queued_append();
  test_session_run_controller_cross_thread_observer_shutdown_is_nonblocking();
  test_session_run_controller_observer_reset_is_immediate_for_unrelated_observer();
  test_session_run_controller_nested_controller_membership_is_stack_aware();
  test_session_run_controller_unrelated_observer_shutdown_without_active_append();
  test_session_run_controller_failure_drains_tickets_with_exact_accounting();
  test_session_run_controller_batch_is_one_ticket_and_latches_failure();
  test_session_run_controller_concurrent_fifo_appends();
  test_session_run_controller_routes_release_target_on_shutdown();
  test_session_run_controller_bounds_and_reentrant_snapshot();
}
