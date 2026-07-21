#include "sys.h"
#include "tests/support/app_runtime_support.h"
#include "tests/support/test_harness.h"
#include "ava/app/runtime.h"
#include "ava/app/runtime_sessions.h"
#include "ava/app/runtime/Session.h"
#include "ava/app/subagent_delivery_manager.h"
#include "ava/agent/job_control.h"
#include "ava/agent/subagent_coordinator.h"
#include "ava/session/session_store.h"
#include "ava/core/ids.h"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <filesystem>
#include <future>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <stop_token>
#include <string>
#include <system_error>
#include <thread>
#include <fcntl.h>
#include <sys/wait.h>
#include <unistd.h>

namespace {

struct BlockingWorker
{
  std::mutex mutex;
  std::condition_variable changed;
  bool started = false;
  bool release = false;
  bool canceled = false;

  ava::agent::BackgroundJobCompletion run(ava::agent::BackgroundJobContext const& context, std::string text = "done")
  {
    std::stop_callback wake_on_stop(context.stop_token, [&] { changed.notify_all(); });
    std::unique_lock lock(mutex);
    started = true;
    changed.notify_all();
    changed.wait(lock, [&] { return release || context.stop_token.stop_requested(); });
    if (context.stop_token.stop_requested())
    {
      canceled = true;
      changed.notify_all();
      return {.state = ava::agent::BackgroundJobState::Canceled, .final_text = {}, .stop_reason = "canceled"};
    }
    return {.state = ava::agent::BackgroundJobState::Completed, .final_text = std::move(text), .stop_reason = "completed"};
  }

  bool wait_started()
  {
    std::unique_lock lock(mutex);
    return changed.wait_for(lock, std::chrono::seconds(1), [&] { return started; });
  }

  void finish()
  {
    std::lock_guard lock(mutex);
    release = true;
    changed.notify_all();
  }
};

ava::permissions::PermissionPrompt permission_prompt(std::string id)
{
  ava::permissions::PermissionPrompt prompt{};
  prompt.permission_request_id = std::move(id);
  prompt.operation = ava::permissions::Operation::ReadFile;
  prompt.mode = ava::agent::Mode::Build;
  return prompt;
}

ava::agent::QuestionPrompt question_prompt(std::string question)
{
  ava::agent::QuestionPrompt prompt;
  prompt.question = std::move(question);
  return prompt;
}

std::shared_ptr<ava::agent::SubagentCoordinator> coordinator_at(std::filesystem::path const& state, ava::agent::SubagentCoordinatorOptions options = {})
{
  options.ava_state_dir = state;
  auto created = ava::agent::SubagentCoordinator::create(std::move(options));
  expect(created.has_value(), created ? "subagent coordinator creates" : "subagent coordinator creates: " + created.error().format());
  return created ? *created : nullptr;
}

bool other_process_activates(std::filesystem::path const& state, std::string const& parent_session_id)
{
  auto const pid = ::fork();
  if (pid == 0)
  {
    ::alarm(5);
    auto coordinator = ava::agent::SubagentCoordinator::create({.ava_state_dir = state});
    auto activated = coordinator ? (*coordinator)->activate_parent(parent_session_id) : ava::core::VoidResult(std::unexpected(std::move(coordinator.error())));
    ::_exit(activated ? 0 : 1);
  }
  if (pid < 0)
    return false;
  int status = 0;
  return ::waitpid(pid, &status, 0) == pid && WIFEXITED(status) && WEXITSTATUS(status) == 0;
}

void test_coordinator_owner_wait_result_and_durable_terminal()
{
  auto root = temp_root() / "subagent-coordinator-owner";
  std::error_code error;
  std::filesystem::remove_all(root, error);
  auto coordinator = coordinator_at(root / "state");
  if (!coordinator)
    return;
  auto worker = std::make_shared<BlockingWorker>();
  auto started = coordinator->start_background("parent_a", {.title = "title", .description = "description", .child_session_id = "child_a"},
                                               [worker](auto const& context) { return worker->run(context, "terminal summary"); });
  expect(started && started->job.identity.job_id.starts_with("job_") && started->job.identity.delivery_id.starts_with("delivery_") &&
             started->job.identity.task_id == "child_a" && started->job.execution == ava::agent::SubagentExecutionState::Running,
         "coordinator publishes stable durable identities and running state");
  if (!started)
    return;
  expect(worker->wait_started(), "coordinator worker starts");
  auto timed = coordinator->wait("parent_a", started->job.identity.job_id, std::chrono::milliseconds(1));
  expect(timed && timed->timed_out && timed->job.execution == ava::agent::SubagentExecutionState::Running,
         "coordinator timeout returns the running snapshot with timed_out");
  auto hidden = coordinator->snapshot("parent_b", started->job.identity.job_id);
  expect(!hidden && hidden.error().category() == ava::core::ErrorCategory::NotFound, "owner mismatch is indistinguishable from a missing job");
  auto hidden_cancel = coordinator->cancel("parent_b", started->job.identity.job_id);
  expect(!hidden_cancel && hidden_cancel.error().category() == ava::core::ErrorCategory::NotFound, "unrelated owner cannot cancel a job");
  expect(!coordinator->result("parent_a", started->job.identity.job_id), "running result reports job_not_ready");

  worker->finish();
  auto completed = coordinator->wait("parent_a", started->job.identity.job_id, std::chrono::seconds(1));
  expect(completed && !completed->timed_out && completed->job.execution == ava::agent::SubagentExecutionState::Completed &&
             completed->job.delivery == ava::agent::SubagentDeliveryState::Pending && completed->job.summary == "terminal summary",
         "terminal and delivery-pending are durable before coordinator terminal publication");
  auto result = coordinator->result("parent_a", started->job.identity.job_id);
  expect(result && result->job.identity.job_id == started->job.identity.job_id, "terminal result remains explicitly queryable");
  auto const status_json = ava::agent::public_job_snapshot_json(*completed);
  auto const result_json = result ? ava::agent::public_job_snapshot_json(*result, ava::agent::PublicJobContent::IncludeTerminalResult) : std::string{};
  expect(status_json.find("terminal summary") == std::string::npos && result_json.find("terminal summary") != std::string::npos &&
             result_json.find("child_session_path") == std::string::npos && result_json.find("coordinator_error") == std::string::npos &&
             result_json.find("\"message\"") == std::string::npos && result_json.find("\"schema_version\":1") != std::string::npos &&
             result_json.find("\"provider_iterations\":0") != std::string::npos,
         "shared public serializer omits terminal content from status and includes only bounded completed result content");
  auto listed = coordinator->list("parent_a");
  expect(listed.size() == 1 && coordinator->list("parent_b").empty(), "coordinator list is owner filtered");

  auto journal = ava::agent::JobJournal::open(root / "state", "parent_a");
  auto projection =
      journal
          ? journal->projection()
          : ava::core::Result<ava::agent::SubagentJobProjection>(std::unexpected(ava::core::Error(ava::core::ErrorCategory::Unknown, "journal unavailable")));
  auto durable = projection ? projection->find(started->job.identity.job_id) : nullptr;
  expect(durable && durable->execution == ava::agent::SubagentExecutionState::Completed && durable->delivery == ava::agent::SubagentDeliveryState::Pending,
         "journal projects completed plus pending delivery");
}

void test_foreground_terminal_parity_and_promotion_races()
{
  auto root = temp_root() / "subagent-coordinator-foreground-promotion";
  std::error_code error;
  std::filesystem::remove_all(root, error);
  auto coordinator = coordinator_at(root / "state");
  if (!coordinator)
    return;

  auto direct_worker = std::make_shared<BlockingWorker>();
  auto direct = coordinator->start("parent_foreground", ava::agent::SubagentJobMode::Foreground, {.title = "direct", .child_session_id = "child_direct"},
                                   [direct_worker](auto const& context) {
                                     auto completion = direct_worker->run(context, "direct summary");
                                     completion.stop_reason = "end_turn";
                                     completion.provider_iterations = 3;
                                     completion.tool_calls = 4;
                                     completion.tool_iterations = 2;
                                     return completion;
                                   });
  expect(direct && direct_worker->wait_started(), "foreground coordinator starts one normal worker");
  if (direct)
  {
    direct_worker->finish();
    auto completed =
        coordinator->wait("parent_foreground", direct->job.identity.job_id, std::chrono::seconds(1), ava::agent::SubagentWaitMode::TerminalOrPromotion);
    expect(completed && completed->job.execution == ava::agent::SubagentExecutionState::Completed &&
               completed->job.mode == ava::agent::SubagentJobMode::Foreground && completed->job.delivery == ava::agent::SubagentDeliveryState::Direct &&
               completed->job.summary == "direct summary" && completed->job.stop_reason == "end_turn" && completed->job.provider_iterations == 3 &&
               completed->job.tool_calls == 4 && completed->job.tool_iterations == 2,
           "foreground terminal delivery preserves final summary, stop reason, and task accounting parity");
    auto late_promotion = coordinator->promote("parent_foreground", direct->job.identity.job_id);
    expect(late_promotion && late_promotion->job.execution == ava::agent::SubagentExecutionState::Completed && !late_promotion->job.was_promoted,
           "completion before promotion consistently returns terminal direct state");
  }

  auto promoted_worker = std::make_shared<BlockingWorker>();
  std::atomic<unsigned> worker_starts = 0;
  auto promoted_start = coordinator->start("parent_foreground", ava::agent::SubagentJobMode::Foreground,
                                           {.title = "promoted", .child_session_id = "child_promoted"}, [promoted_worker, &worker_starts](auto const& context) {
                                             worker_starts.fetch_add(1, std::memory_order_relaxed);
                                             auto completion = promoted_worker->run(context, "promoted summary");
                                             completion.provider_iterations = 5;
                                             completion.tool_calls = 6;
                                             completion.tool_iterations = 4;
                                             return completion;
                                           });
  expect(promoted_start && promoted_worker->wait_started(), "promotion fixture starts foreground worker once");
  if (!promoted_start)
    return;
  auto const identity = promoted_start->job.identity;
  auto hidden = coordinator->promote("other_parent", identity.job_id);
  expect(!hidden && hidden.error().category() == ava::core::ErrorCategory::NotFound, "wrong owner cannot promote a foreground job");
  auto promoted = coordinator->promote("parent_foreground", identity.job_id);
  auto repeated = coordinator->promote("parent_foreground", identity.job_id);
  auto wake = coordinator->wait("parent_foreground", identity.job_id, std::chrono::milliseconds(0), ava::agent::SubagentWaitMode::TerminalOrPromotion);
  expect(promoted && repeated && wake && promoted->job.was_promoted && repeated->job.was_promoted && wake->job.was_promoted &&
             promoted->job.identity.job_id == identity.job_id && promoted->job.identity.task_id == identity.task_id &&
             promoted->job.identity.child_session_id == identity.child_session_id && promoted->job.identity.delivery_id == identity.delivery_id,
         "promotion is idempotent and wakes mode-aware wait with every stable identity unchanged");
  promoted_worker->finish();
  auto completed = coordinator->wait("parent_foreground", identity.job_id, std::chrono::seconds(1));
  expect(completed && completed->job.execution == ava::agent::SubagentExecutionState::Completed &&
             completed->job.delivery == ava::agent::SubagentDeliveryState::Pending && completed->job.summary == "promoted summary" &&
             completed->job.provider_iterations == 5 && completed->job.tool_calls == 6 && completed->job.tool_iterations == 4 &&
             worker_starts.load(std::memory_order_relaxed) == 1,
         "promotion before completion keeps one worker and atomically publishes terminal plus delivery pending");

  auto canceled_worker = std::make_shared<BlockingWorker>();
  auto canceled = coordinator->start("parent_foreground", ava::agent::SubagentJobMode::Foreground, {.title = "cancel", .child_session_id = "child_canceled"},
                                     [canceled_worker](auto const& context) { return canceled_worker->run(context); });
  expect(canceled && canceled_worker->wait_started(), "cancel-promotion fixture starts");
  if (canceled)
  {
    expect(coordinator->cancel("parent_foreground", canceled->job.identity.job_id).has_value(), "cancel wins its deterministic race fixture");
    auto rejected = coordinator->promote("parent_foreground", canceled->job.identity.job_id);
    expect(!rejected || rejected->job.execution == ava::agent::SubagentExecutionState::Canceled,
           "cancel before promotion is rejected or consistently returns terminal canceled state");
    auto terminal = coordinator->wait("parent_foreground", canceled->job.identity.job_id, std::chrono::seconds(1));
    expect(terminal && terminal->job.execution == ava::agent::SubagentExecutionState::Canceled && !terminal->job.was_promoted,
           "cancel-before-promotion never creates pending background delivery");
  }
}

void test_atomic_promotion_completion_and_cancel_lock_races()
{
  struct TransitionGate
  {
    std::mutex mutex;
    std::condition_variable changed;
    ava::agent::JobJournalTransitionKind blocked = ava::agent::JobJournalTransitionKind::Started;
    bool seen = false;
    bool release = false;

    ava::core::VoidResult preflight(ava::agent::JobJournalRecord const& record)
    {
      if (record.kind != blocked)
        return {};
      std::unique_lock lock(mutex);
      seen = true;
      changed.notify_all();
      changed.wait(lock, [&] { return release; });
      return {};
    }
    bool wait_seen()
    {
      std::unique_lock lock(mutex);
      return changed.wait_for(lock, std::chrono::seconds(1), [&] { return seen; });
    }
    void finish()
    {
      std::lock_guard lock(mutex);
      release = true;
      changed.notify_all();
    }
  };

  auto root = temp_root() / "subagent-coordinator-atomic-races";
  std::error_code error;
  std::filesystem::remove_all(root, error);

  {
    auto gate = std::make_shared<TransitionGate>();
    gate->blocked = ava::agent::JobJournalTransitionKind::Terminal;
    ava::agent::SubagentCoordinatorOptions options;
    options.journal_append_preflight = [gate](auto const& record) { return gate->preflight(record); };
    auto coordinator = coordinator_at(root / "completion" / "state", std::move(options));
    if (coordinator)
    {
      auto started =
          coordinator->start("parent_completion_race", ava::agent::SubagentJobMode::Foreground, {.child_session_id = "child_completion_race"}, [](auto const&) {
            ava::agent::BackgroundJobCompletion completion;
            completion.final_text = "completion won";
            return completion;
          });
      expect(started && gate->wait_seen(), "completion race holds the terminal journal lock before promotion");
      if (started)
      {
        std::mutex entered_mutex;
        std::condition_variable entered_changed;
        bool entered = false;
        auto promotion = std::async(std::launch::async, [&] {
          {
            std::lock_guard lock(entered_mutex);
            entered = true;
            entered_changed.notify_all();
          }
          return coordinator->promote("parent_completion_race", started->job.identity.job_id);
        });
        {
          std::unique_lock lock(entered_mutex);
          expect(entered_changed.wait_for(lock, std::chrono::seconds(1), [&] { return entered; }),
                 "promotion contender enters while terminal publication owns the journal lock");
        }
        gate->finish();
        auto result = promotion.get();
        expect(result && result->job.execution == ava::agent::SubagentExecutionState::Completed && !result->job.was_promoted &&
                   result->job.delivery == ava::agent::SubagentDeliveryState::Direct,
               "terminal publication winning the atomic lock race returns direct completion to promotion");
      }
    }
  }

  {
    auto gate = std::make_shared<TransitionGate>();
    gate->blocked = ava::agent::JobJournalTransitionKind::CancelRequested;
    ava::agent::SubagentCoordinatorOptions options;
    options.journal_append_preflight = [gate](auto const& record) { return gate->preflight(record); };
    auto coordinator = coordinator_at(root / "cancel" / "state", std::move(options));
    auto worker = std::make_shared<BlockingWorker>();
    if (coordinator)
    {
      auto started = coordinator->start("parent_cancel_race", ava::agent::SubagentJobMode::Foreground, {.child_session_id = "child_cancel_race"},
                                        [worker](auto const& context) { return worker->run(context); });
      expect(started && worker->wait_started(), "cancel-first race worker starts");
      if (started)
      {
        auto cancellation = std::async(std::launch::async, [&] { return coordinator->cancel("parent_cancel_race", started->job.identity.job_id); });
        expect(gate->wait_seen(), "cancel-first race holds the journal lock before promotion");
        std::mutex entered_mutex;
        std::condition_variable entered_changed;
        bool entered = false;
        auto promotion = std::async(std::launch::async, [&] {
          {
            std::lock_guard lock(entered_mutex);
            entered = true;
            entered_changed.notify_all();
          }
          return coordinator->promote("parent_cancel_race", started->job.identity.job_id);
        });
        {
          std::unique_lock lock(entered_mutex);
          expect(entered_changed.wait_for(lock, std::chrono::seconds(1), [&] { return entered; }),
                 "promotion contender enters while cancellation owns the journal lock");
        }
        gate->finish();
        auto canceled = cancellation.get();
        auto promoted = promotion.get();
        auto terminal = coordinator->wait("parent_cancel_race", started->job.identity.job_id, std::chrono::seconds(1));
        expect(canceled && (!promoted || promoted->job.execution == ava::agent::SubagentExecutionState::Canceled) && terminal &&
                   terminal->job.execution == ava::agent::SubagentExecutionState::Canceled && !terminal->job.was_promoted,
               "cancellation winning the journal race prevents promotion without latching the coordinator");
      }
    }
  }

  {
    auto gate = std::make_shared<TransitionGate>();
    gate->blocked = ava::agent::JobJournalTransitionKind::Promoted;
    ava::agent::SubagentCoordinatorOptions options;
    options.journal_append_preflight = [gate](auto const& record) { return gate->preflight(record); };
    auto coordinator = coordinator_at(root / "promotion" / "state", std::move(options));
    auto worker = std::make_shared<BlockingWorker>();
    if (coordinator)
    {
      auto started = coordinator->start("parent_promotion_race", ava::agent::SubagentJobMode::Foreground, {.child_session_id = "child_promotion_race"},
                                        [worker](auto const& context) { return worker->run(context); });
      expect(started && worker->wait_started(), "promotion-first race worker starts");
      if (started)
      {
        auto first_promotion = std::async(std::launch::async, [&] { return coordinator->promote("parent_promotion_race", started->job.identity.job_id); });
        expect(gate->wait_seen(), "promotion-first race holds the journal lock");
        std::mutex entered_mutex;
        std::condition_variable entered_changed;
        unsigned entered = 0;
        auto contender = [&](bool cancel) {
          {
            std::lock_guard lock(entered_mutex);
            ++entered;
            entered_changed.notify_all();
          }
          return cancel ? coordinator->cancel("parent_promotion_race", started->job.identity.job_id)
                        : coordinator->promote("parent_promotion_race", started->job.identity.job_id);
        };
        auto repeated_promotion = std::async(std::launch::async, [&] { return contender(false); });
        auto cancellation = std::async(std::launch::async, [&] { return contender(true); });
        {
          std::unique_lock lock(entered_mutex);
          expect(entered_changed.wait_for(lock, std::chrono::seconds(1), [&] { return entered == 2; }),
                 "repeated promotion and cancellation contenders enter behind the promoted transition");
        }
        gate->finish();
        auto first = first_promotion.get();
        auto repeated = repeated_promotion.get();
        auto canceled = cancellation.get();
        auto terminal = coordinator->wait("parent_promotion_race", started->job.identity.job_id, std::chrono::seconds(1));
        expect(first && repeated && canceled && first->job.was_promoted && repeated->job.was_promoted && terminal && terminal->job.was_promoted &&
                   terminal->job.execution == ava::agent::SubagentExecutionState::Canceled &&
                   terminal->job.delivery == ava::agent::SubagentDeliveryState::Pending,
               "promotion winning the journal race is idempotent and later cancellation records terminal pending delivery");
      }
    }
  }
}

void test_promotion_interaction_gate_rejects_permission_and_question_races()
{
  struct Interaction
  {
    std::mutex mutex;
    std::condition_variable changed;
    bool entered = false;
    bool release = false;

    void enter_and_wait()
    {
      std::unique_lock lock(mutex);
      entered = true;
      changed.notify_all();
      changed.wait(lock, [&] { return release; });
    }
    bool wait_entered()
    {
      std::unique_lock lock(mutex);
      return changed.wait_for(lock, std::chrono::seconds(1), [&] { return entered; });
    }
    void finish()
    {
      std::lock_guard lock(mutex);
      release = true;
      changed.notify_all();
    }
  };

  auto root = temp_root() / "subagent-coordinator-interaction-gate";
  std::error_code error;
  std::filesystem::remove_all(root, error);
  auto coordinator = coordinator_at(root / "state");
  if (!coordinator)
    return;

  auto permission_interaction = std::make_shared<Interaction>();
  std::atomic<unsigned> permission_calls = 0;
  auto permission_gate = ava::agent::SubagentInteractionGate::create(
      ava::agent::SubagentJobMode::Foreground,
      [permission_interaction,
       &permission_calls](ava::permissions::PermissionPrompt const&) -> ava::core::Result<ava::permissions::PermissionResolutionDecision> {
        permission_calls.fetch_add(1, std::memory_order_relaxed);
        permission_interaction->enter_and_wait();
        return ava::permissions::PermissionResolution::Allow;
      },
      nullptr);
  auto permission_worker = std::make_shared<BlockingWorker>();
  auto permission_job = coordinator->start(
      "parent_interactions", ava::agent::SubagentJobMode::Foreground, {.child_session_id = "child_permission"},
      [permission_worker](auto const& context) { return permission_worker->run(context); }, permission_gate);
  expect(permission_job && permission_worker->wait_started(), "permission interaction fixture starts");
  if (permission_job)
  {
    auto resolver = permission_gate->permission_resolver();
    auto resolution = std::async(std::launch::async, [resolver] { return resolver(permission_prompt("permission_1")); });
    expect(permission_interaction->wait_entered(), "foreground permission resolver becomes outstanding");
    auto rejected = coordinator->promote("parent_interactions", permission_job->job.identity.job_id);
    expect(!rejected && rejected.error().format().find("foreground_interaction_outstanding") != std::string::npos,
           "promotion rejects an outstanding permission modal with a stable actionable error");
    permission_interaction->finish();
    expect(resolution.wait_for(std::chrono::seconds(1)) == std::future_status::ready && resolution.get().has_value(),
           "existing foreground permission resolver completes normally after rejected promotion");
    auto promoted = coordinator->promote("parent_interactions", permission_job->job.identity.job_id);
    auto unavailable = resolver(permission_prompt("permission_2"));
    expect(promoted && !unavailable && unavailable.error().format().find("background_interaction_unavailable") != std::string::npos &&
               permission_calls.load(std::memory_order_relaxed) == 1,
           "promotion releases the frontend permission callback and disables all future interactive permission calls");
    permission_worker->finish();
    expect(coordinator->wait("parent_interactions", permission_job->job.identity.job_id, std::chrono::seconds(1)).has_value(),
           "permission-gated promoted worker finishes");
  }

  auto question_interaction = std::make_shared<Interaction>();
  std::atomic<unsigned> question_calls = 0;
  auto question_gate = ava::agent::SubagentInteractionGate::create(
      ava::agent::SubagentJobMode::Foreground, nullptr,
      [question_interaction, &question_calls](ava::agent::QuestionPrompt const&) -> ava::core::Result<ava::agent::QuestionAnswer> {
        question_calls.fetch_add(1, std::memory_order_relaxed);
        question_interaction->enter_and_wait();
        return ava::agent::QuestionAnswer{.selected_options = {"yes"}, .custom_text = {}};
      });
  auto question_worker = std::make_shared<BlockingWorker>();
  auto question_job = coordinator->start(
      "parent_interactions", ava::agent::SubagentJobMode::Foreground, {.child_session_id = "child_question"},
      [question_worker](auto const& context) { return question_worker->run(context); }, question_gate);
  expect(question_job && question_worker->wait_started(), "question interaction fixture starts");
  if (question_job)
  {
    auto resolver = question_gate->question_resolver();
    auto answer = std::async(std::launch::async, [resolver] { return resolver(question_prompt("continue?")); });
    expect(question_interaction->wait_entered(), "foreground question resolver becomes outstanding");
    auto rejected = coordinator->promote("parent_interactions", question_job->job.identity.job_id);
    expect(!rejected && rejected.error().format().find("foreground_interaction_outstanding") != std::string::npos,
           "promotion rejects an outstanding question modal");
    question_interaction->finish();
    expect(answer.wait_for(std::chrono::seconds(1)) == std::future_status::ready && answer.get().has_value(),
           "existing foreground question resolver completes after rejected promotion");
    auto promoted = coordinator->promote("parent_interactions", question_job->job.identity.job_id);
    auto unavailable = resolver(question_prompt("again?"));
    expect(promoted && !unavailable && unavailable.error().format().find("background_interaction_unavailable") != std::string::npos &&
               question_calls.load(std::memory_order_relaxed) == 1,
           "promotion releases the frontend question callback and prevents a stranded future modal");
    question_worker->finish();
    expect(coordinator->wait("parent_interactions", question_job->job.identity.job_id, std::chrono::seconds(1)).has_value(),
           "question-gated promoted worker finishes");
  }

  std::atomic<unsigned> background_callbacks = 0;
  auto background_gate = ava::agent::SubagentInteractionGate::create(
      ava::agent::SubagentJobMode::Background,
      [&background_callbacks](ava::permissions::PermissionPrompt const&) -> ava::core::Result<ava::permissions::PermissionResolutionDecision> {
        background_callbacks.fetch_add(1, std::memory_order_relaxed);
        return ava::permissions::PermissionResolution::Allow;
      },
      [&background_callbacks](ava::agent::QuestionPrompt const&) -> ava::core::Result<ava::agent::QuestionAnswer> {
        background_callbacks.fetch_add(1, std::memory_order_relaxed);
        return ava::agent::QuestionAnswer{};
      });
  auto direct_permission = background_gate->permission_resolver()(permission_prompt("background_permission"));
  auto direct_question = background_gate->question_resolver()(question_prompt("background?"));
  expect(!direct_permission && !direct_question && background_callbacks.load(std::memory_order_relaxed) == 0 &&
             direct_permission.error().format().find("background_interaction_unavailable") != std::string::npos &&
             direct_question.error().format().find("background_interaction_unavailable") != std::string::npos,
         "direct background jobs never capture or open frontend permission/question callbacks");
}

void test_coordinator_start_journal_failure_prevents_publication()
{
  auto root = temp_root() / "subagent-coordinator-start-failure";
  std::error_code error;
  std::filesystem::remove_all(root, error);
  std::atomic<bool> worker_called = false;
  ava::agent::SubagentCoordinatorOptions options;
  options.journal_append_preflight = [](ava::agent::JobJournalRecord const& record) -> ava::core::VoidResult {
    if (record.kind == ava::agent::JobJournalTransitionKind::Started)
      return std::unexpected(ava::core::Error(ava::core::ErrorCategory::Io, "injected started journal failure"));
    return {};
  };
  auto coordinator = coordinator_at(root / "state", std::move(options));
  if (!coordinator)
    return;
  auto started = coordinator->start_background("parent_start_fail", {.title = "title", .description = "description", .child_session_id = "child_start_fail"},
                                               [&](auto const&) {
                                                 worker_called.store(true, std::memory_order_release);
                                                 return ava::agent::BackgroundJobCompletion{};
                                               });
  expect(!started && !worker_called.load(std::memory_order_acquire) && coordinator->list("parent_start_fail").empty(),
         "started journal failure prevents worker and live-job publication");
}

void test_coordinator_second_terminal_batch_record_failure_latches_without_partial_transition()
{
  auto root = temp_root() / "subagent-coordinator-terminal-failure";
  std::error_code error;
  std::filesystem::remove_all(root, error);
  std::atomic<unsigned> pending_attempts = 0;
  ava::agent::SubagentCoordinatorOptions options;
  options.journal_append_preflight = [&](ava::agent::JobJournalRecord const& record) -> ava::core::VoidResult {
    if (record.kind == ava::agent::JobJournalTransitionKind::DeliveryPending)
    {
      pending_attempts.fetch_add(1, std::memory_order_relaxed);
      return std::unexpected(ava::core::Error(ava::core::ErrorCategory::Io, "injected delivery-pending journal failure"));
    }
    return {};
  };
  auto coordinator = coordinator_at(root / "state", std::move(options));
  if (!coordinator)
    return;
  auto started =
      coordinator->start_background("parent_terminal_fail", {.title = "title", .description = "description", .child_session_id = "child_terminal_fail"},
                                    [](auto const&) -> ava::agent::BackgroundJobCompletion { throw std::runtime_error("worker boom"); });
  expect(started.has_value(), "worker exception fixture publishes after durable start");
  if (!started)
    return;
  auto waited = coordinator->wait("parent_terminal_fail", started->job.identity.job_id, std::chrono::seconds(1));
  expect(waited && waited->coordinator_latched && waited->coordinator_error &&
             waited->coordinator_error->find("injected delivery-pending journal failure") != std::string::npos,
         "second terminal-batch record failure latches visible coordinator state");
  expect(pending_attempts.load(std::memory_order_relaxed) == 1, "second terminal-batch record failure is never silently retried");
  auto journal = ava::agent::JobJournal::open(root / "state", "parent_terminal_fail");
  auto durable = journal ? journal->projection() : ava::core::Result<ava::agent::SubagentJobProjection>(std::unexpected(journal.error()));
  expect(durable && durable->find(started->job.identity.job_id) &&
             durable->find(started->job.identity.job_id)->execution == ava::agent::SubagentExecutionState::Running &&
             durable->find(started->job.identity.job_id)->delivery == ava::agent::SubagentDeliveryState::Direct,
         "failed second terminal-batch preflight leaves no partial durable terminal transition");
  auto rejected = coordinator->start_background("parent_terminal_fail", {.child_session_id = "child_after_latch"},
                                                [](auto const&) { return ava::agent::BackgroundJobCompletion{}; });
  expect(!rejected && rejected.error().message().find("latched") != std::string::npos, "post-publication journal failure latches coordinator admission");
}

void test_coordinator_worker_exception_is_durably_failed()
{
  auto root = temp_root() / "subagent-coordinator-exception";
  std::error_code error;
  std::filesystem::remove_all(root, error);
  auto coordinator = coordinator_at(root / "state");
  if (!coordinator)
    return;
  auto started = coordinator->start_background("parent_exception", {.title = "throws", .description = "throws", .child_session_id = "child_exception"},
                                               [](auto const&) -> ava::agent::BackgroundJobCompletion { throw std::runtime_error("coordinator worker boom"); });
  expect(started.has_value(), "coordinator publishes throwing worker after durable start");
  if (!started)
    return;
  auto failed = coordinator->wait("parent_exception", started->job.identity.job_id, std::chrono::seconds(1));
  expect(failed && failed->job.execution == ava::agent::SubagentExecutionState::Failed && failed->job.delivery == ava::agent::SubagentDeliveryState::Pending &&
             failed->job.error_category == "unknown" && failed->job.error == "subagent job failed" &&
             failed->job.error->find("coordinator worker boom") == std::string::npos,
         "worker exception becomes a bounded sanitized durable failure plus pending delivery");
  auto journal = ava::agent::JobJournal::open(root / "state", "parent_exception");
  auto projection = journal ? journal->projection() : ava::core::Result<ava::agent::SubagentJobProjection>(std::unexpected(journal.error()));
  expect(projection && projection->jobs.size() == 1 && projection->jobs.front().error_category == "unknown" &&
             projection->jobs.front().error == "subagent job failed" && projection->jobs.front().error->find("coordinator worker boom") == std::string::npos,
         "reopened failure journal retains only the safe error category and message without formatted exception context");
}

void test_coordinator_cancel_and_shutdown_are_durable_and_cooperative()
{
  auto root = temp_root() / "subagent-coordinator-shutdown";
  std::error_code error;
  std::filesystem::remove_all(root, error);
  auto coordinator = coordinator_at(root / "state");
  if (!coordinator)
    return;
  auto first_worker = std::make_shared<BlockingWorker>();
  auto first = coordinator->start_background("parent_shutdown", {.title = "one", .description = "one", .child_session_id = "child_one"},
                                             [first_worker](auto const& context) { return first_worker->run(context); });
  expect(first && first_worker->wait_started(), "cancel fixture worker starts");
  if (first)
  {
    auto canceled = coordinator->cancel("parent_shutdown", first->job.identity.job_id);
    expect(canceled && canceled->job.cancel_requested, "cancel_requested is durable before cancel response");
    auto terminal = coordinator->wait("parent_shutdown", first->job.identity.job_id, std::chrono::seconds(1));
    expect(terminal && terminal->job.execution == ava::agent::SubagentExecutionState::Canceled,
           "explicit cancellation reaches cooperative worker and persists terminal state");
  }

  auto second_worker = std::make_shared<BlockingWorker>();
  auto second = coordinator->start_background("parent_shutdown", {.title = "two", .description = "two", .child_session_id = "child_two"},
                                              [second_worker](auto const& context) { return second_worker->run(context); });
  expect(second && second_worker->wait_started(), "shutdown fixture worker starts");
  coordinator->shutdown();
  {
    std::lock_guard lock(second_worker->mutex);
    expect(second_worker->canceled, "application shutdown requests cancellation and joins the worker");
  }
  auto rejected =
      coordinator->start_background("parent_shutdown", {.child_session_id = "child_three"}, [](auto const&) { return ava::agent::BackgroundJobCompletion{}; });
  expect(!rejected, "coordinator rejects admission after shutdown begins");
}

void test_coordinator_preserves_registry_running_and_retention_limits()
{
  auto root = temp_root() / "subagent-coordinator-limits";
  std::error_code error;
  std::filesystem::remove_all(root, error);
  ava::agent::SubagentCoordinatorOptions options;
  options.registry_options.max_running_jobs = 1;
  options.registry_options.max_retained_finished_jobs = 1;
  auto coordinator = coordinator_at(root / "state", std::move(options));
  if (!coordinator)
    return;
  auto first_worker = std::make_shared<BlockingWorker>();
  auto first = coordinator->start_background("parent_limits", {.child_session_id = "child_limit_one"},
                                             [first_worker](auto const& context) { return first_worker->run(context, "first"); });
  expect(first && first_worker->wait_started(), "coordinator running-limit fixture starts first job");
  std::atomic<bool> rejected_worker_called = false;
  auto rejected = coordinator->start_background("parent_limits", {.child_session_id = "child_limit_rejected"}, [&](auto const&) {
    rejected_worker_called.store(true, std::memory_order_release);
    return ava::agent::BackgroundJobCompletion{};
  });
  auto rejected_journal = ava::agent::JobJournal::open(root / "state", "parent_limits");
  auto rejected_projection =
      rejected_journal ? rejected_journal->projection() : ava::core::Result<ava::agent::SubagentJobProjection>(std::unexpected(rejected_journal.error()));
  expect(!rejected && !rejected_worker_called.load(std::memory_order_acquire) &&
             ava::agent::subagent_publication_commit_state(rejected.error()) == ava::agent::SubagentPublicationCommitState::ProvenUnpublished &&
             rejected_projection && rejected_projection->jobs.size() == 1,
         "running-limit rejection rolls back its durable start with no phantom recovered job");
  if (!first)
    return;
  first_worker->finish();
  expect(coordinator->wait("parent_limits", first->job.identity.job_id, std::chrono::seconds(1)).has_value(), "first limited job completes");
  auto second = coordinator->start_background("parent_limits", {.child_session_id = "child_limit_two"}, [](auto const&) {
    return ava::agent::BackgroundJobCompletion{.state = ava::agent::BackgroundJobState::Completed, .final_text = "second", .stop_reason = "completed"};
  });
  expect(second.has_value(), "coordinator admits after running capacity is released");
  if (!second)
    return;
  expect(coordinator->wait("parent_limits", second->job.identity.job_id, std::chrono::seconds(1)).has_value(), "second limited job completes");
  auto retained = coordinator->list("parent_limits");
  expect(retained.size() == 1 && retained.front().job.identity.job_id == second->job.identity.job_id,
         "coordinator preserves registry finished-job retention limit");
  auto durable_pending = coordinator->pending_deliveries("parent_limits");
  expect(durable_pending && durable_pending->size() == 2, "coordinator keeps evicted delivery-pending jobs discoverable from the bounded durable journal");
  auto restored_attempt = coordinator->record_delivery_attempt("parent_limits", first->job.identity.job_id, "evicted_attempt", "evicted_fingerprint");
  auto restored_ack = coordinator->acknowledge_delivery("parent_limits", first->job.identity.job_id, "evicted_attempt", "evicted_commit");
  expect(restored_attempt && restored_ack && restored_ack->job.delivery == ava::agent::SubagentDeliveryState::Acknowledged,
         "delivery transitions restore an evicted pending job from its durable journal without restarting it");
}

void test_coordinator_thread_start_failure_rolls_back_unpublished_start()
{
  auto root = temp_root() / "subagent-coordinator-thread-start";
  std::error_code error;
  std::filesystem::remove_all(root, error);
  ava::agent::SubagentCoordinatorOptions options;
  options.registry_options.thread_start_preflight = []() -> ava::core::VoidResult {
    return std::unexpected(ava::core::Error(ava::core::ErrorCategory::Unknown, "injected thread start failure"));
  };
  auto coordinator = coordinator_at(root / "state", std::move(options));
  if (!coordinator)
    return;
  auto rejected = coordinator->start_background("parent_thread_start", {.child_session_id = "child_thread_start"},
                                                [](auto const&) { return ava::agent::BackgroundJobCompletion{}; });
  auto journal = ava::agent::JobJournal::open(root / "state", "parent_thread_start");
  auto projection = journal ? journal->projection() : ava::core::Result<ava::agent::SubagentJobProjection>(std::unexpected(journal.error()));
  expect(!rejected && ava::agent::subagent_publication_commit_state(rejected.error()) == ava::agent::SubagentPublicationCommitState::ProvenUnpublished &&
             projection && projection->jobs.empty(),
         "thread-start rejection removes the sole unpublished Started transition");
  coordinator.reset();
  auto restarted = coordinator_at(root / "state");
  expect(restarted && restarted->list("parent_thread_start").empty(), "rolled-back thread-start rejection creates no recovered phantom job");
}

void test_coordinator_rollback_failure_retains_durable_start_conservatively()
{
  auto root = temp_root() / "subagent-coordinator-rollback-failure";
  std::error_code error;
  std::filesystem::remove_all(root, error);
  ava::agent::SubagentCoordinatorOptions options;
  options.registry_options.max_running_jobs = 0;
  options.journal_rollback_preflight = [](ava::agent::SubagentJobIdentity const&) -> ava::core::VoidResult {
    return std::unexpected(ava::core::Error(ava::core::ErrorCategory::Io, "injected rollback failure"));
  };
  auto coordinator = coordinator_at(root / "state", std::move(options));
  if (!coordinator)
    return;
  auto rejected = coordinator->start_background("parent_rollback_failure", {.child_session_id = "child_rollback_failure"},
                                                [](auto const&) { return ava::agent::BackgroundJobCompletion{}; });
  expect(!rejected && ava::agent::subagent_publication_commit_state(rejected.error()) == ava::agent::SubagentPublicationCommitState::PublicationUncertain &&
             rejected.error().format().find("injected rollback failure") != std::string::npos,
         "rollback failure is conservatively marked publication-uncertain");
  coordinator.reset();
  auto restarted = coordinator_at(root / "state");
  auto activated = restarted ? restarted->activate_parent("parent_rollback_failure") : ava::core::VoidResult{};
  auto recovered = restarted && activated ? restarted->list("parent_rollback_failure") : std::vector<ava::agent::SubagentCoordinatorJobSnapshot>{};
  expect(restarted && activated && recovered.size() == 1 && recovered.front().job.execution == ava::agent::SubagentExecutionState::Interrupted &&
             recovered.front().job.identity.child_session_id == "child_rollback_failure",
         "rollback failure retains durable child context for on-demand crash-safe recovery");
}

void test_terminal_failure_concurrently_exercises_list_retention_and_shutdown()
{
  struct Gate
  {
    std::mutex mutex;
    std::condition_variable changed;
    bool terminal_preflight_seen = false;
    bool release = false;
  };
  auto root = temp_root() / "subagent-coordinator-lock-order";
  std::error_code error;
  std::filesystem::remove_all(root, error);
  auto gate = std::make_shared<Gate>();
  ava::agent::SubagentCoordinatorOptions options;
  options.journal_append_preflight = [gate](ava::agent::JobJournalRecord const& record) -> ava::core::VoidResult {
    if (record.kind != ava::agent::JobJournalTransitionKind::Terminal)
      return {};
    std::unique_lock lock(gate->mutex);
    gate->terminal_preflight_seen = true;
    gate->changed.notify_all();
    gate->changed.wait(lock, [&] { return gate->release; });
    return std::unexpected(ava::core::Error(ava::core::ErrorCategory::Io, "injected terminal journal failure"));
  };
  auto coordinator = coordinator_at(root / "state", std::move(options));
  if (!coordinator)
    return;
  auto started = coordinator->start_background("parent_lock_order", {.child_session_id = "child_lock_order"}, [](auto const&) {
    return ava::agent::BackgroundJobCompletion{.state = ava::agent::BackgroundJobState::Completed, .final_text = "done", .stop_reason = "completed"};
  });
  expect(started.has_value(), "lock-order fixture publishes durable start");
  if (!started)
    return;
  {
    std::unique_lock lock(gate->mutex);
    expect(gate->changed.wait_for(lock, std::chrono::seconds(1), [&] { return gate->terminal_preflight_seen; }),
           "terminal journal failure blocks at deterministic preflight gate");
  }
  auto listed = std::async(std::launch::async, [&] { return coordinator->list("parent_lock_order"); });
  expect(listed.wait_for(std::chrono::seconds(1)) == std::future_status::ready && listed.get().size() == 1,
         "list completes while a terminal journal failure is pending");
  auto stopped = std::async(std::launch::async, [&] { coordinator->shutdown(); });
  {
    std::lock_guard lock(gate->mutex);
    gate->release = true;
    gate->changed.notify_all();
  }
  expect(stopped.wait_for(std::chrono::seconds(2)) == std::future_status::ready,
         "terminal latch, retention synchronization, and shutdown complete without lock-order deadlock");
}

void test_coordinator_releases_only_idle_parent_owners()
{
  auto root = temp_root() / "subagent-coordinator-idle-release";
  std::error_code error;
  std::filesystem::remove_all(root, error);
  auto const state = root / "state";
  auto coordinator = coordinator_at(state);
  if (!coordinator)
    return;
  std::string const parent = "parent_idle_release";
  expect(coordinator->activate_parent(parent).has_value(), "idle-release fixture activates parent A");
  coordinator->release_parent_if_idle(parent);
  expect(other_process_activates(state, parent), "process B activates idle parent A after process A releases its in-memory owner");

  expect(coordinator->activate_parent(parent).has_value(), "process A reacquires parent before live-work retention checks");
  auto worker = std::make_shared<BlockingWorker>();
  auto started = coordinator->start_background(parent, {.title = "live", .child_session_id = "child_live"},
                                               [worker](ava::agent::BackgroundJobContext const& context) { return worker->run(context); });
  expect(started && worker->wait_started(), "idle-release fixture starts a live child");
  if (!started)
    return;
  coordinator->release_parent_if_idle(parent);
  auto live_owner = ava::agent::JobJournal::try_open_owned(state, parent);
  expect(live_owner && !*live_owner, "live work retains parent A's owner against process B");

  worker->finish();
  auto terminal = coordinator->wait(parent, started->job.identity.job_id, std::chrono::seconds(1));
  expect(terminal && terminal->job.delivery == ava::agent::SubagentDeliveryState::Pending, "terminal background work remains a pending deliverable");
  coordinator->release_parent_if_idle(parent);
  auto pending_owner = ava::agent::JobJournal::try_open_owned(state, parent);
  expect(pending_owner && !*pending_owner, "pending delivery retains parent A's owner against process B");

  auto attempted = coordinator->record_delivery_attempt(parent, started->job.identity.job_id, "attempt_idle_release", "fingerprint_idle_release");
  auto acknowledged = attempted ? coordinator->acknowledge_delivery(parent, started->job.identity.job_id, "attempt_idle_release", "turn_idle_release")
                                : ava::core::Result<ava::agent::SubagentCoordinatorJobSnapshot>(std::unexpected(std::move(attempted.error())));
  expect(attempted && acknowledged && acknowledged->job.delivery == ava::agent::SubagentDeliveryState::Acknowledged,
         "acknowledging the terminal delivery clears the durable retain condition");
  coordinator->release_parent_if_idle(parent);
  expect(coordinator->list(parent).empty() && other_process_activates(state, parent),
         "acknowledged historical jobs are evicted and process B activates parent A while process A remains alive");

  struct StartBarrier
  {
    std::mutex mutex;
    std::condition_variable changed;
    bool entered = false;
    bool release = false;
  } barrier;
  auto racing = coordinator_at(
      state, {.ava_state_dir = state, .journal_append_preflight = [&barrier](ava::agent::JobJournalRecord const& record) -> ava::core::VoidResult {
                if (record.kind != ava::agent::JobJournalTransitionKind::Started)
                  return {};
                std::unique_lock lock(barrier.mutex);
                barrier.entered = true;
                barrier.changed.notify_all();
                barrier.changed.wait(lock, [&] { return barrier.release; });
                return {};
              }});
  if (!racing)
    return;
  std::string const unrelated_parent = "parent_idle_during_unrelated_start";
  expect(racing->activate_parent(unrelated_parent).has_value(), "unrelated release fixture activates an idle parent");
  std::optional<ava::core::Result<ava::agent::SubagentCoordinatorJobSnapshot>> race_start;
  std::thread starter([&] {
    race_start.emplace(racing->start_background(parent, {.title = "race", .child_session_id = "child_race"}, [](auto const&) {
      return ava::agent::BackgroundJobCompletion{.state = ava::agent::BackgroundJobState::Completed, .final_text = "done", .stop_reason = "completed"};
    }));
  });
  {
    std::unique_lock lock(barrier.mutex);
    expect(barrier.changed.wait_for(lock, std::chrono::seconds(1), [&] { return barrier.entered; }), "race fixture blocks a start after admission");
  }
  racing->release_parent_if_idle(unrelated_parent);
  expect(other_process_activates(state, unrelated_parent), "a start admitted for parent A does not prevent process B from acquiring detached idle parent B");
  racing->release_parent_if_idle(parent);
  auto racing_owner = ava::agent::JobJournal::try_open_owned(state, parent);
  expect(racing_owner && !*racing_owner, "release cannot drop a parent owner while a start may still publish into it");
  {
    std::lock_guard lock(barrier.mutex);
    barrier.release = true;
    barrier.changed.notify_all();
  }
  starter.join();
  expect(race_start && race_start->has_value(), "blocked start publishes after the conservative release race check");
}

void test_coordinator_lazy_parent_activation_is_process_independent()
{
  auto root = temp_root() / "subagent-coordinator-lazy-activation";
  std::error_code error;
  std::filesystem::remove_all(root, error);
  auto const state = root / "state";
  auto seed_running = [&](std::string const& parent) {
    ava::agent::SubagentJobIdentity identity{.job_id = "job_" + parent,
                                             .task_id = "task_" + parent,
                                             .parent_session_id = parent,
                                             .child_session_id = "child_" + parent,
                                             .delivery_id = "delivery_" + parent};
    auto journal = ava::agent::JobJournal::open(state, parent);
    return journal && journal
                          ->append({.kind = ava::agent::JobJournalTransitionKind::Started,
                                    .identity = std::move(identity),
                                    .at = "2026-07-20T00:00:00.000Z",
                                    .mode = ava::agent::SubagentJobMode::Background})
                          .has_value();
  };
  expect(seed_running("parent_a") && seed_running("parent_b") && seed_running("parent_c"), "lazy activation fixture persists independent unmatched parents");

  auto idle = coordinator_at(state);
  auto untouched = ava::agent::JobJournal::open(state, "parent_a");
  auto untouched_projection = untouched ? untouched->projection() : ava::core::Result<ava::agent::SubagentJobProjection>(std::unexpected(untouched.error()));
  auto independently_owned = ava::agent::JobJournal::try_open_owned(state, "parent_c");
  expect(idle && untouched_projection && untouched_projection->jobs.front().execution == ava::agent::SubagentExecutionState::Running && independently_owned &&
             independently_owned->has_value(),
         "coordinator startup neither recovers historical parents nor squats their owner leases");
  if (independently_owned)
    independently_owned->reset();
  idle.reset();

  int ready[2] = {-1, -1};
  int release[2] = {-1, -1};
  expect(::pipe2(ready, O_CLOEXEC) == 0 && ::pipe2(release, O_CLOEXEC) == 0, "lazy activation fixture creates synchronization pipes");
  if (ready[0] < 0 || release[0] < 0)
    return;
  auto owner_pid = ::fork();
  if (owner_pid == 0)
  {
    ::alarm(5);
    static_cast<void>(::close(ready[0]));
    static_cast<void>(::close(release[1]));
    auto coordinator = ava::agent::SubagentCoordinator::create({.ava_state_dir = state});
    auto activated = coordinator ? (*coordinator)->activate_parent("parent_a") : ava::core::VoidResult(std::unexpected(coordinator.error()));
    char const status = activated ? '1' : '0';
    static_cast<void>(::write(ready[1], &status, 1));
    char wake = 0;
    if (status == '1')
      static_cast<void>(::read(release[0], &wake, 1));
    ::_exit(status == '1' ? 0 : 2);
  }
  static_cast<void>(::close(ready[1]));
  static_cast<void>(::close(release[0]));
  char owner_ready = 0;
  auto const ready_bytes = ::read(ready[0], &owner_ready, 1);
  static_cast<void>(::close(ready[0]));
  expect(owner_pid > 0 && ready_bytes == 1 && owner_ready == '1', "first process activates and recovers only parent A");
  if (owner_pid <= 0 || owner_ready != '1')
    return;

  auto competing_pid = ::fork();
  if (competing_pid == 0)
  {
    ::alarm(5);
    auto coordinator = ava::agent::SubagentCoordinator::create({.ava_state_dir = state});
    auto parent_a = coordinator ? (*coordinator)->activate_parent("parent_a") : ava::core::VoidResult(std::unexpected(coordinator.error()));
    auto parent_b = coordinator ? (*coordinator)->activate_parent("parent_b") : ava::core::VoidResult(std::unexpected(coordinator.error()));
    auto parent_c = coordinator ? (*coordinator)->activate_parent("parent_c") : ava::core::VoidResult(std::unexpected(coordinator.error()));
    bool const independent = !parent_a && parent_a.error().category() == ava::core::ErrorCategory::PermissionDenied && parent_b && parent_c;
    ::_exit(independent ? 0 : 3);
  }
  int competing_status = 0;
  static_cast<void>(::waitpid(competing_pid, &competing_status, 0));
  expect(WIFEXITED(competing_status) && WEXITSTATUS(competing_status) == 0,
         "another process manages unrelated B and C while the live owner prevents A recovery");

  char const wake = 'x';
  static_cast<void>(::write(release[1], &wake, 1));
  static_cast<void>(::close(release[1]));
  int owner_status = 0;
  static_cast<void>(::waitpid(owner_pid, &owner_status, 0));
  expect(WIFEXITED(owner_status) && WEXITSTATUS(owner_status) == 0, "parent A owner exits and releases only its lease");

  auto recovery_pid = ::fork();
  if (recovery_pid == 0)
  {
    ::alarm(5);
    auto coordinator = ava::agent::SubagentCoordinator::create({.ava_state_dir = state});
    auto activated = coordinator ? (*coordinator)->activate_parent("parent_a") : ava::core::VoidResult(std::unexpected(coordinator.error()));
    auto recovered = activated ? (*coordinator)->list("parent_a") : std::vector<ava::agent::SubagentCoordinatorJobSnapshot>{};
    bool const available_after_exit = activated && recovered.size() == 1 && recovered.front().job.execution == ava::agent::SubagentExecutionState::Interrupted;
    ::_exit(available_after_exit ? 0 : 4);
  }
  int recovery_status = 0;
  static_cast<void>(::waitpid(recovery_pid, &recovery_status, 0));
  expect(WIFEXITED(recovery_status) && WEXITSTATUS(recovery_status) == 0, "a later owner can activate the recovered parent after the former owner exits");
}

void test_coordinator_startup_recovery_marks_interrupted_without_worker()
{
  auto root = temp_root() / "subagent-coordinator-recovery";
  std::error_code error;
  std::filesystem::remove_all(root, error);
  ava::agent::SubagentJobIdentity identity{.job_id = "job_recovery",
                                           .task_id = "task_recovery",
                                           .parent_session_id = "parent_recovery",
                                           .child_session_id = "child_recovery",
                                           .delivery_id = "delivery_recovery"};
  {
    auto journal = ava::agent::JobJournal::open(root / "state", identity.parent_session_id);
    expect(journal.has_value(), "recovery fixture journal opens");
    if (!journal)
      return;
    auto appended = journal->append({.kind = ava::agent::JobJournalTransitionKind::Started,
                                     .identity = identity,
                                     .at = "2026-07-20T00:00:00.000Z",
                                     .mode = ava::agent::SubagentJobMode::Background});
    expect(appended.has_value(), "recovery fixture leaves unmatched started job");
  }
  auto coordinator = coordinator_at(root / "state");
  if (!coordinator)
    return;
  auto recovered = coordinator->snapshot(identity.parent_session_id, identity.job_id);
  expect(recovered && recovered->job.execution == ava::agent::SubagentExecutionState::Interrupted &&
             recovered->job.delivery == ava::agent::SubagentDeliveryState::Pending,
         "application startup marks unmatched job interrupted and pending without starting a worker");
}

void test_runtime_navigation_releases_idle_parent_owner()
{
  auto root = temp_root() / "subagent-coordinator-idle-navigation";
  std::error_code error;
  std::filesystem::remove_all(root, error);
  auto const workspace = root / "workspace";
  std::filesystem::create_directories(workspace);
  auto paths = ava::tests::app_test_paths(root);
  auto coordinator = coordinator_at(paths.ava_state_dir);
  if (!coordinator)
    return;
  ava::app::runtime::OpenOptions options;
  options.workspace_dir = workspace;
  options.current_dir = workspace;
  options.paths = paths;
  options.offline = true;
  options.subagent_coordinator = coordinator;
  auto visible = ava::app::open_runtime_session(options);
  expect(visible.has_value(), "idle-navigation fixture opens parent A");
  if (!visible)
    return;
  auto const parent_a = visible->store.session_id();
  auto replacement = ava::app::create_runtime_session_like(*visible, options);
  expect(replacement.has_value(), "idle-navigation fixture opens an unrelated replacement");
  if (!replacement)
    return;
  expect(ava::app::replace_runtime_session(*visible, std::move(*replacement)).has_value() && other_process_activates(paths.ava_state_dir, parent_a),
         "process B activates idle parent A after process A navigates away while its coordinator remains alive");
}

void test_runtime_navigation_preserves_coordinator_and_child_authority()
{
  auto root = temp_root() / "subagent-coordinator-navigation";
  std::error_code error;
  std::filesystem::remove_all(root, error);
  auto workspace = root / "workspace";
  ava::config::XdgPaths paths{.config_home = root / "config",
                              .state_home = root / "state-home",
                              .data_home = root / "data",
                              .ava_config_dir = root / "config" / "ava",
                              .ava_state_dir = root / "state-home" / "ava",
                              .auth_file = root / "config" / "ava" / "auth.json",
                              .compaction_file = root / "config" / "ava" / "compaction.json",
                              .global_agents_file = root / "config" / "ava" / "AGENTS.md",
                              .models_file = root / "config" / "ava" / "models.json",
                              .prompts_dir = root / "config" / "ava" / "prompts",
                              .sessions_dir = root / "state-home" / "ava" / "sessions"};
  auto coordinator = coordinator_at(paths.ava_state_dir);
  if (!coordinator)
    return;
  std::filesystem::create_directories(workspace);
  ava::app::runtime::OpenOptions open_options;
  open_options.workspace_dir = workspace;
  open_options.current_dir = workspace;
  open_options.paths = paths;
  open_options.offline = true;
  open_options.subagent_coordinator = coordinator;
  auto visible = ava::app::open_runtime_session(open_options);
  expect(visible.has_value(), "navigation fixture opens parent runtime session");
  if (!visible)
    return;
  auto const original_parent_id = visible->store.session_id();

  auto child_store = ava::session::SessionStore::create(workspace, paths.sessions_dir);
  auto child_lease =
      child_store
          ? ava::session::SessionLease::create_and_acquire(child_store->session_path())
          : ava::core::Result<ava::session::SessionLease>(std::unexpected(ava::core::Error(ava::core::ErrorCategory::Unknown, "child store unavailable")));
  expect(child_store && child_lease, "navigation fixture creates owned child session");
  if (!child_store || !child_lease)
    return;
  auto child_id = child_store->session_id();
  struct ChildState
  {
    ava::session::SessionStore store;
    ava::session::SessionLease lease;
    std::shared_ptr<BlockingWorker> gate;
  };
  auto child =
      std::make_shared<ChildState>(ChildState{.store = std::move(*child_store), .lease = std::move(*child_lease), .gate = std::make_shared<BlockingWorker>()});
  auto started = coordinator->start_background(
      original_parent_id, {.title = "navigation", .description = "navigation", .child_session_id = child_id},
      [child](ava::agent::BackgroundJobContext const& context) {
        auto completion = child->gate->run(context, "child persisted output");
        if (completion.state == ava::agent::BackgroundJobState::Completed)
        {
          auto appended = child->store.append(child->lease, ava::session::SessionEntry{.id = ava::core::make_id("entry"),
                                                                                       .parent_id = {},
                                                                                       .type = ava::session::EntryType::Error,
                                                                                       .timestamp = ava::session::now_timestamp(),
                                                                                       .data_json = "{\"message\":\"child persisted output\"}"});
          if (!appended)
            return ava::agent::BackgroundJobCompletion{
                .state = ava::agent::BackgroundJobState::Failed, .final_text = {}, .stop_reason = "append_failed", .error = appended.error()};
        }
        return completion;
      });
  expect(started && child->gate->wait_started() && !other_process_activates(paths.ava_state_dir, original_parent_id),
         "a live child retains its parent owner against process B before navigation");
  if (!started)
    return;

  auto replacement = ava::app::create_runtime_session_like(*visible, open_options);
  expect(replacement.has_value(), "navigation fixture opens unrelated visible session with shared coordinator");
  if (!replacement)
    return;
  auto const unrelated_parent_id = replacement->store.session_id();
  expect(ava::app::replace_runtime_session(*visible, std::move(*replacement)).has_value(), "visible runtime session is replaced");
  expect(visible->subagent_coordinator == coordinator && !other_process_activates(paths.ava_state_dir, original_parent_id),
         "process B cannot activate a parent after navigation while its child remains live");
  expect(visible->subagent_coordinator == coordinator, "runtime replacement preserves the exact application coordinator");
  {
    std::lock_guard lock(child->gate->mutex);
    expect(!child->gate->canceled, "runtime navigation does not cancel the running child");
  }
  auto hidden = coordinator->snapshot(unrelated_parent_id, started->job.identity.job_id);
  auto hidden_cancel = coordinator->cancel(unrelated_parent_id, started->job.identity.job_id);
  expect(!hidden && hidden.error().category() == ava::core::ErrorCategory::NotFound && !hidden_cancel &&
             hidden_cancel.error().category() == ava::core::ErrorCategory::NotFound,
         "newly visible unrelated session cannot query or cancel the original parent's job");

  child->gate->finish();
  auto completed = coordinator->wait(original_parent_id, started->job.identity.job_id, std::chrono::seconds(1));
  auto pending_owner = ava::agent::JobJournal::try_open_owned(paths.ava_state_dir, original_parent_id);
  expect(completed && completed->job.execution == ava::agent::SubagentExecutionState::Completed &&
             completed->job.delivery == ava::agent::SubagentDeliveryState::Pending && pending_owner && !*pending_owner &&
             !other_process_activates(paths.ava_state_dir, original_parent_id),
         "a terminal but deliverable child retains its detached parent owner against process B");
  auto attempted = coordinator->record_delivery_attempt(original_parent_id, started->job.identity.job_id, "attempt_navigation", "fingerprint_navigation");
  auto acknowledged = attempted ? coordinator->acknowledge_delivery(original_parent_id, started->job.identity.job_id, "attempt_navigation", "turn_navigation")
                                : ava::core::Result<ava::agent::SubagentCoordinatorJobSnapshot>(std::unexpected(std::move(attempted.error())));
  if (visible->subagent_delivery_manager)
    visible->subagent_delivery_manager->release_detached_parent(original_parent_id);
  expect(attempted && acknowledged && other_process_activates(paths.ava_state_dir, original_parent_id),
         "acknowledged detached delivery releases parent ownership for process B");
  auto persisted = child->store.load();
  expect(persisted && !persisted->empty() && persisted->back().data_json.find("child persisted output") != std::string::npos,
         "child keeps its store and lease by value and persists output after parent navigation");
}

}  // namespace

void run_subagent_coordinator_tests()
{
  test_coordinator_owner_wait_result_and_durable_terminal();
  test_foreground_terminal_parity_and_promotion_races();
  test_atomic_promotion_completion_and_cancel_lock_races();
  test_promotion_interaction_gate_rejects_permission_and_question_races();
  test_coordinator_start_journal_failure_prevents_publication();
  test_coordinator_second_terminal_batch_record_failure_latches_without_partial_transition();
  test_coordinator_worker_exception_is_durably_failed();
  test_coordinator_cancel_and_shutdown_are_durable_and_cooperative();
  test_coordinator_preserves_registry_running_and_retention_limits();
  test_coordinator_thread_start_failure_rolls_back_unpublished_start();
  test_coordinator_rollback_failure_retains_durable_start_conservatively();
  test_terminal_failure_concurrently_exercises_list_retention_and_shutdown();
  test_coordinator_releases_only_idle_parent_owners();
  test_coordinator_lazy_parent_activation_is_process_independent();
  test_coordinator_startup_recovery_marks_interrupted_without_worker();
  test_runtime_navigation_releases_idle_parent_owner();
  test_runtime_navigation_preserves_coordinator_and_child_authority();
}
