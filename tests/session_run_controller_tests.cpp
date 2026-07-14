#include "tests/support/test_harness.h"
#include "ava/app/runtime.h"
#include "ava/app/session_run_controller.h"
#include "ava/session/session_store.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <filesystem>
#include <thread>
#include <utility>
#include <vector>

namespace {

void test_session_run_controller_transitions_and_guard_release()
{
  ava::app::SessionRunController controller;
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
  ava::app::SessionRunController controller;
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
  ava::app::SessionRunController controller;
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
  std::error_code error;
  auto root = temp_root() / "session-run-controller";
  std::filesystem::remove_all(root, error);
  auto store = ava::session::SessionStore::create(std::filesystem::current_path(), root);
  expect(store.has_value(), "controller append fixture store creates");
  if (!store)
    return;

  ava::app::SessionRunController controller;
  auto owner = controller.owner_append_route(*store);
  expect(owner(append_entry("inactive-owner")).has_value(), "owner route persists while no run is active");
  auto first = controller.admit({.request_id = "A"});
  expect(first.has_value(), "controller admits append generation A");
  if (!first)
    return;
  auto guard = std::move(*first);
  auto active = guard.append_route(*store);
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

void test_runtime_session_destroys_background_before_owner_route()
{
  std::error_code error;
  auto root = temp_root() / "session-run-controller-teardown";
  std::filesystem::remove_all(root, error);
  auto store = ava::session::SessionStore::create(std::filesystem::current_path(), root);
  expect(store.has_value(), "teardown fixture store creates");
  if (!store)
    return;

  std::atomic<bool> worker_started = false;
  std::atomic<bool> owner_append_succeeded = false;
  {
    ava::app::RuntimeSession session{.store = std::move(*store),
                                     .lease = {},
                                     .mode = ava::agent::Mode::Build,
                                     .model = {},
                                     .base_prompt = {},
                                     .paths = {},
                                     .workspace_dir = {},
                                     .current_dir = {},
                                     .project_trust = {},
                                     .prompt_overrides = {},
                                     .tool_visibility = {},
                                     .context_sources = {},
                                     .freshness_sources = {},
                                     .system_prompt = {},
                                     .reasoning = std::nullopt,
                                     .scoped_model_cycle = std::nullopt,
                                     .created = false,
                                     .sessionless = false,
                                     .run_controller = std::make_unique<ava::app::SessionRunController>(),
                                     .background_jobs = std::make_shared<ava::agent::BackgroundJobRegistry>(),
                                     .offline = false};
    auto owner = session.owner_append_route();
    auto started = session.background_jobs->start(
        {.title = "blocked", .description = "blocked"},
        [owner = std::move(owner), &worker_started, &owner_append_succeeded](ava::agent::BackgroundJobContext const& context) {
          worker_started.store(true, std::memory_order_release);
          while (!context.stop_token.stop_requested()) std::this_thread::sleep_for(std::chrono::milliseconds(1));
          owner_append_succeeded.store(owner(append_entry("teardown-owner")).has_value(), std::memory_order_release);
          return ava::agent::BackgroundJobCompletion{
              .state = ava::agent::BackgroundJobState::Canceled, .final_text = {}, .stop_reason = "canceled", .error = std::nullopt};
        });
    expect(started.has_value(), "blocked teardown worker starts");
    while (!worker_started.load(std::memory_order_acquire)) std::this_thread::sleep_for(std::chrono::milliseconds(1));
  }
  expect(owner_append_succeeded.load(std::memory_order_acquire), "background teardown can use owner route before controller/store destruction");
}

void test_session_run_controller_admission_inspection_and_join()
{
  ava::app::SessionRunController controller;
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
  std::jthread waiter([&] {
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
  ava::app::SessionRunController controller;
  auto a = controller.admit({.request_id = "A"});
  expect(a.has_value(), "stable join fixture admits A");
  if (!a)
    return;
  auto guard_a = std::move(*a);
  std::optional<ava::app::RunOutcome> joined;
  std::jthread waiter([&] {
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
  std::error_code error;
  auto root = temp_root() / "session-run-controller-latch";
  std::filesystem::remove_all(root, error);
  auto store = ava::session::SessionStore::create(std::filesystem::current_path(), root);
  expect(store.has_value(), "latch fixture store creates");
  if (!store)
    return;
  ava::app::SessionRunController controller;
  auto admitted = controller.admit({.request_id = "failed"});
  expect(admitted.has_value(), "latch fixture admits");
  if (!admitted)
    return;
  auto guard = std::move(*admitted);
  std::atomic<bool> callback_reentered = false;
  std::stop_callback callback(guard.stop_token(), [&] { callback_reentered.store(controller.snapshot().stop_requested, std::memory_order_release); });
  auto invalid = append_entry("invalid");
  invalid.data_json.clear();
  expect(!guard.append_route(*store)(std::move(invalid)).has_value(), "append failure latches");
  expect(callback_reentered.load(std::memory_order_acquire), "append failure stop callback reenters without deadlock");
  auto effective = guard.complete({.run_id = {}, .reason = ava::app::StopReason::ProviderError});
  expect(effective && effective->reason == ava::app::StopReason::PersistenceError && effective->error,
         "complete reports persistence as effective terminal outcome");
  expect(!controller.admit({.request_id = "blocked"}).has_value(), "persistence latch blocks admission");
  expect(controller.reset_persistence_failure().has_value(), "explicit terminal/drained recovery clears latch");
  auto recovered = controller.admit({.request_id = "recovered"});
  expect(recovered.has_value(), "admission resumes only after explicit recovery");
}

void test_session_run_controller_concurrent_fifo_appends()
{
  std::error_code error;
  auto root = temp_root() / "session-run-controller-fifo";
  std::filesystem::remove_all(root, error);
  auto store = ava::session::SessionStore::create(std::filesystem::current_path(), root);
  expect(store.has_value(), "fifo fixture store creates");
  if (!store)
    return;
  ava::app::SessionRunController controller;
  auto admitted = controller.admit({.request_id = "fifo"});
  expect(admitted.has_value(), "fifo fixture admits");
  if (!admitted)
    return;
  auto guard = std::move(*admitted);
  auto active = guard.append_route(*store);
  auto owner = controller.owner_append_route(*store);
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

void test_session_run_controller_bounds_and_reentrant_snapshot()
{
  ava::app::SessionRunController controller;
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
  test_runtime_session_destroys_background_before_owner_route();
  test_session_run_controller_admission_inspection_and_join();
  test_session_run_controller_stable_join_and_command_kinds();
  test_session_run_controller_effective_failure_and_stop_reentry();
  test_session_run_controller_concurrent_fifo_appends();
  test_session_run_controller_bounds_and_reentrant_snapshot();
}
