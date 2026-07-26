#include "sys.h"
#include "tests/agent_loop_test_declarations.h"
#include "tests/support/test_harness.h"
#include "ava/agent/agent_loop.h"
#include "ava/core/result.h"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <stop_token>
#include <string>
#include <thread>
#include <utility>
#include <vector>

void test_background_job_registry_worker_exception_marks_failed()
{
  ava::agent::BackgroundJobRegistry registry;
  auto started = registry.start(ava::agent::BackgroundJobStartOptions{.title = "throws"},
                                [](ava::agent::BackgroundJobContext const&) -> ava::agent::BackgroundJobCompletion { throw std::runtime_error("boom"); });
  expect(started.has_value(), "background registry starts worker that throws");
  if (started)
  {
    auto failed = registry.wait(started->job_id, std::chrono::milliseconds(1000));
    expect(failed && failed->state == ava::agent::BackgroundJobState::Failed && failed->error && failed->error->find("boom") != std::string::npos,
           "background registry records thrown worker exceptions as failed jobs");
    auto const joined = registry.join_finished();
    auto retained = registry.wait(started->job_id, std::chrono::milliseconds(0));
    expect(joined == 1 && retained && retained->state == ava::agent::BackgroundJobState::Failed,
           "background registry retains terminal job snapshots after joining worker threads");
  }
}

void test_background_job_registry_enforces_running_limit()
{
  struct State
  {
    std::mutex mutex;
    std::condition_variable changed;
    bool started = false;
  };
  auto state = std::make_shared<State>();
  ava::agent::BackgroundJobRegistry registry(ava::agent::BackgroundJobRegistryOptions{.max_running_jobs = 1, .max_retained_finished_jobs = 4});
  auto blocking_worker = [state](ava::agent::BackgroundJobContext const& context) {
    std::unique_lock lock(state->mutex);
    std::stop_callback notify_stop(context.stop_token, [&] { state->changed.notify_all(); });
    state->started = true;
    state->changed.notify_all();
    state->changed.wait(lock, [&] { return context.stop_token.stop_requested(); });
    return ava::agent::BackgroundJobCompletion{.state = ava::agent::BackgroundJobState::Canceled, .final_text = "", .stop_reason = "canceled"};
  };
  auto first = registry.start(ava::agent::BackgroundJobStartOptions{.title = "first"}, blocking_worker);
  expect(first.has_value(), "background registry starts first job under running limit");
  {
    std::unique_lock lock(state->mutex);
    expect(state->changed.wait_for(lock, std::chrono::milliseconds(1000), [&] { return state->started; }),
           "running-limit test worker starts before second job attempt");
  }
  auto second = registry.start(ava::agent::BackgroundJobStartOptions{.title = "second"}, [](ava::agent::BackgroundJobContext const&) {
    return ava::agent::BackgroundJobCompletion{.state = ava::agent::BackgroundJobState::Completed, .final_text = "unexpected", .stop_reason = "completed"};
  });
  expect(!second && second.error().message().find("limit") != std::string::npos, "background registry rejects jobs above the running limit");
  if (first)
  {
    static_cast<void>(registry.cancel(first->job_id));
    auto canceled = registry.wait(first->job_id, std::chrono::milliseconds(1000));
    expect(canceled && canceled->state == ava::agent::BackgroundJobState::Canceled, "background registry frees running capacity after cancellation");
  }
  registry.join_finished();
  auto third = registry.start(ava::agent::BackgroundJobStartOptions{.title = "third"}, [](ava::agent::BackgroundJobContext const&) {
    return ava::agent::BackgroundJobCompletion{.state = ava::agent::BackgroundJobState::Completed, .final_text = "third done", .stop_reason = "completed"};
  });
  expect(third.has_value(), "background registry accepts a new job after running job completes");
  if (third)
  {
    auto completed = registry.wait(third->job_id, std::chrono::milliseconds(1000));
    expect(completed && completed->final_text == "third done", "background registry completes job after running limit frees capacity");
  }
  registry.join_finished();
}

void test_background_job_registry_coerces_non_terminal_completion_to_failed()
{
  ava::agent::BackgroundJobRegistry registry;
  auto started = registry.start(ava::agent::BackgroundJobStartOptions{.title = "bad completion"}, [](ava::agent::BackgroundJobContext const&) {
    return ava::agent::BackgroundJobCompletion{.state = ava::agent::BackgroundJobState::Running, .final_text = "bad", .stop_reason = "running"};
  });
  expect(started.has_value(), "background registry starts worker with invalid completion state");
  if (started)
  {
    auto failed = registry.wait(started->job_id, std::chrono::milliseconds(1000));
    expect(failed && failed->state == ava::agent::BackgroundJobState::Failed && failed->error && failed->error->find("non-terminal") != std::string::npos,
           "background registry coerces non-terminal worker completions to failed snapshots");
    registry.join_finished();
  }
}

void test_background_job_registry_bounds_snapshot_text()
{
  ava::agent::BackgroundJobRegistry registry(
      ava::agent::BackgroundJobRegistryOptions{.max_running_jobs = 4, .max_retained_finished_jobs = 4, .max_description_bytes = 5, .max_final_text_bytes = 7});
  auto const multi_byte_suffix = std::string("\xE2\x82\xAC", 3) + "tail";
  auto started = registry.start(ava::agent::BackgroundJobStartOptions{.title = "bounded", .description = std::string("abcd") + multi_byte_suffix},
                                [](ava::agent::BackgroundJobContext const&) {
                                  return ava::agent::BackgroundJobCompletion{
                                      .state = ava::agent::BackgroundJobState::Completed,
                                      .final_text = std::string("abcdef") + std::string("\xE2\x82\xAC", 3) + "tail",
                                      .stop_reason = "",
                                  };
                                });
  expect(started && started->description == "abcd" && started->description_truncated,
         "background registry truncates oversized job descriptions without splitting UTF-8 codepoints");
  if (started)
  {
    auto completed = registry.wait(started->job_id, std::chrono::milliseconds(1000));
    expect(completed && completed->final_text == "abcdef" && completed->final_text_truncated,
           "background registry truncates oversized final text without splitting UTF-8 codepoints");
    registry.join_finished();
  }
}

void test_background_job_registry_normalizes_terminal_completion_fields()
{
  ava::agent::BackgroundJobRegistry registry;
  auto completed_with_error =
      registry.start(ava::agent::BackgroundJobStartOptions{.title = "completed with error"}, [](ava::agent::BackgroundJobContext const&) {
        auto error = ava::core::Error(ava::core::ErrorCategory::Unknown, "ignored completion error");
        return ava::agent::BackgroundJobCompletion{
            .state = ava::agent::BackgroundJobState::Completed,
            .final_text = "done",
            .stop_reason = "",
            .error = std::move(error),
        };
      });
  auto failed_without_error =
      registry.start(ava::agent::BackgroundJobStartOptions{.title = "failed without error"}, [](ava::agent::BackgroundJobContext const&) {
        return ava::agent::BackgroundJobCompletion{
            .state = ava::agent::BackgroundJobState::Failed,
            .final_text = "should disappear",
            .stop_reason = "",
        };
      });
  auto canceled_with_error = registry.start(ava::agent::BackgroundJobStartOptions{.title = "canceled with error"}, [](ava::agent::BackgroundJobContext const&) {
    auto error = ava::core::Error(ava::core::ErrorCategory::Unknown, "ignored cancel error");
    return ava::agent::BackgroundJobCompletion{
        .state = ava::agent::BackgroundJobState::Canceled,
        .final_text = "should disappear",
        .stop_reason = "",
        .error = std::move(error),
    };
  });

  expect(completed_with_error && failed_without_error && canceled_with_error, "background registry starts terminal normalization workers");
  if (completed_with_error)
  {
    auto completed = registry.wait(completed_with_error->job_id, std::chrono::milliseconds(1000));
    expect(completed && completed->state == ava::agent::BackgroundJobState::Completed && completed->final_text == "done" &&
               completed->stop_reason == "completed" && !completed->error,
           "background registry clears completed-job errors and defaults stop reasons");
  }
  if (failed_without_error)
  {
    auto failed = registry.wait(failed_without_error->job_id, std::chrono::milliseconds(1000));
    expect(failed && failed->state == ava::agent::BackgroundJobState::Failed && failed->final_text.empty() && failed->stop_reason == "failed" &&
               failed->error && failed->error->find("without an error") != std::string::npos,
           "background registry synthesizes failed-job errors and clears final text");
  }
  if (canceled_with_error)
  {
    auto canceled = registry.wait(canceled_with_error->job_id, std::chrono::milliseconds(1000));
    expect(canceled && canceled->state == ava::agent::BackgroundJobState::Canceled && canceled->final_text.empty() && canceled->stop_reason == "canceled" &&
               !canceled->error,
           "background registry clears canceled-job errors and final text");
  }
  registry.join_finished();
}

void test_background_job_registry_join_finished_is_concurrency_safe()
{
  ava::agent::BackgroundJobRegistry registry;
  auto started = registry.start(ava::agent::BackgroundJobStartOptions{.title = "concurrent join"}, [](ava::agent::BackgroundJobContext const&) {
    return ava::agent::BackgroundJobCompletion{.state = ava::agent::BackgroundJobState::Completed, .final_text = "done", .stop_reason = ""};
  });
  expect(started.has_value(), "background registry starts concurrent join worker");
  if (!started)
    return;
  auto completed = registry.wait(started->job_id, std::chrono::milliseconds(1000));
  expect(completed && completed->state == ava::agent::BackgroundJobState::Completed, "background registry has a terminal job before concurrent join");

  std::atomic_bool threw = false;
  std::atomic_size_t joined_total = 0;
  auto joiner = [&] {
    try
    {
      joined_total.fetch_add(registry.join_finished(), std::memory_order_relaxed);
    }
    catch (...)
    {
      threw = true;
    }
  };
  std::thread first(joiner);
  std::thread second(joiner);
  first.join();
  second.join();
  expect(!threw && joined_total.load(std::memory_order_relaxed) == 1, "background registry concurrent join_finished calls join exactly once");
}

void test_background_job_registry_prunes_retained_finished_jobs()
{
  ava::agent::BackgroundJobRegistry registry(ava::agent::BackgroundJobRegistryOptions{.max_running_jobs = 4, .max_retained_finished_jobs = 1});
  auto first = registry.start(ava::agent::BackgroundJobStartOptions{.title = "first retained"}, [](ava::agent::BackgroundJobContext const&) {
    return ava::agent::BackgroundJobCompletion{.state = ava::agent::BackgroundJobState::Completed, .final_text = "first", .stop_reason = ""};
  });
  auto second = registry.start(ava::agent::BackgroundJobStartOptions{.title = "second retained"}, [](ava::agent::BackgroundJobContext const&) {
    return ava::agent::BackgroundJobCompletion{.state = ava::agent::BackgroundJobState::Completed, .final_text = "second", .stop_reason = ""};
  });
  expect(first && second, "background registry starts retained-pruning workers");
  if (first)
    expect(registry.wait(first->job_id, std::chrono::milliseconds(1000)).has_value(), "first retained-pruning job completes");
  if (second)
    expect(registry.wait(second->job_id, std::chrono::milliseconds(1000)).has_value(), "second retained-pruning job completes");
  registry.join_finished();
  expect(registry.snapshot().size() <= 1, "background registry prunes joined terminal snapshots beyond retention limit");
}

void test_background_job_registry_destructor_stops_running_jobs()
{
  struct State
  {
    std::mutex mutex;
    std::condition_variable changed;
    bool started = false;
    bool canceled = false;
  };
  auto state = std::make_shared<State>();

  {
    ava::agent::BackgroundJobRegistry registry;
    auto started = registry.start(ava::agent::BackgroundJobStartOptions{.title = "destructor stop"}, [state](ava::agent::BackgroundJobContext const& context) {
      std::unique_lock lock(state->mutex);
      std::stop_callback notify_stop(context.stop_token, [&] { state->changed.notify_all(); });
      state->started = true;
      state->changed.notify_all();
      state->changed.wait(lock, [&] { return context.stop_token.stop_requested(); });
      state->canceled = true;
      state->changed.notify_all();
      return ava::agent::BackgroundJobCompletion{
          .state = ava::agent::BackgroundJobState::Canceled,
          .final_text = "",
          .stop_reason = "canceled",
      };
    });
    expect(started.has_value(), "background registry starts destructor stop worker");
    std::unique_lock lock(state->mutex);
    expect(state->changed.wait_for(lock, std::chrono::milliseconds(1000), [&] { return state->started; }),
           "background registry destructor test worker starts before scope exit");
  }

  std::lock_guard lock(state->mutex);
  expect(state->canceled, "background registry destructor requests stop and joins running jobs");
}
