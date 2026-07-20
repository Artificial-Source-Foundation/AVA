#include "sys.h"
#include "ava/agent/job_control.h"
#include "ava/agent/subagent_coordinator.h"
#include "ava/session/session_store.h"
#include "ava/core/ids.h"

#include <algorithm>
#include <exception>
#include <unordered_set>
#include <utility>

namespace ava::agent {
namespace {

ava::core::Error interaction_unavailable(std::string_view interaction, bool background)
{
  auto error =
      ava::core::Error(ava::core::ErrorCategory::Tool, background ? "background interaction unavailable" : "foreground interaction resolver is unavailable");
  error.with_context("error_code", background ? "background_interaction_unavailable" : "foreground_interaction_unavailable");
  error.with_context("interaction", std::string(interaction));
  return error;
}

ava::core::Error not_found(std::string_view job_id)
{
  auto error = ava::core::Error(ava::core::ErrorCategory::NotFound, "subagent job not found");
  error.with_context("job_id", std::string(job_id));
  return error;
}

bool terminal(SubagentExecutionState state) noexcept
{
  return state == SubagentExecutionState::Completed || state == SubagentExecutionState::Failed || state == SubagentExecutionState::Canceled ||
         state == SubagentExecutionState::Interrupted;
}

BackgroundJobCompletion exception_completion(std::string const& job_id, std::stop_token const& stop_token, std::string cause)
{
  auto error = ava::core::Error(ava::core::ErrorCategory::Unknown, "background job worker threw an exception");
  error.with_context("job_id", job_id).with_context("cause", std::move(cause));
  return {.state = stop_token.stop_requested() ? BackgroundJobState::Canceled : BackgroundJobState::Failed,
          .final_text = {},
          .stop_reason = stop_token.stop_requested() ? "canceled" : "failed",
          .error = std::move(error)};
}

BackgroundJobCompletion normalize(std::string const& job_id, BackgroundJobCompletion completion)
{
  if (completion.state == BackgroundJobState::Completed)
  {
    completion.error = std::nullopt;
    if (completion.stop_reason.empty())
      completion.stop_reason = "completed";
    return completion;
  }
  if (completion.state == BackgroundJobState::Canceled)
  {
    completion.final_text.clear();
    completion.error = std::nullopt;
    if (completion.stop_reason.empty())
      completion.stop_reason = "canceled";
    return completion;
  }
  if (completion.state != BackgroundJobState::Failed)
  {
    auto error = ava::core::Error(ava::core::ErrorCategory::Unknown, "background job completed with a non-terminal state");
    error.with_context("job_id", job_id);
    completion = {.state = BackgroundJobState::Failed, .final_text = {}, .stop_reason = "failed", .error = std::move(error)};
  }
  completion.final_text.clear();
  if (completion.stop_reason.empty())
    completion.stop_reason = "failed";
  if (!completion.error)
  {
    auto error = ava::core::Error(ava::core::ErrorCategory::Unknown, "background job failed without an error");
    error.with_context("job_id", job_id);
    completion.error = std::move(error);
  }
  return completion;
}

JobJournalRecord terminal_record(SubagentJobIdentity const& identity, BackgroundJobCompletion const& completion, std::string at)
{
  JobJournalRecord record{.kind = JobJournalTransitionKind::Terminal,
                          .identity = identity,
                          .at = std::move(at),
                          .provider_iterations = completion.provider_iterations,
                          .tool_calls = completion.tool_calls,
                          .tool_iterations = completion.tool_iterations};
  if (completion.state == BackgroundJobState::Completed)
  {
    record.terminal_state = SubagentTerminalState::Completed;
    record.summary = completion.final_text;
    record.summary_truncated = false;
    record.stop_reason = completion.stop_reason;
    record.stop_reason_truncated = false;
  }
  else if (completion.state == BackgroundJobState::Canceled)
  {
    record.terminal_state = SubagentTerminalState::Canceled;
    record.stop_reason = completion.stop_reason;
    record.stop_reason_truncated = false;
  }
  else
  {
    record.terminal_state = SubagentTerminalState::Failed;
    if (completion.error)
    {
      record.error_category = safe_subagent_error_category(*completion.error);
      record.error = safe_subagent_error_message(*completion.error);
    }
    else
    {
      record.error_category = "unknown";
      record.error = "subagent job failed";
    }
    record.error_truncated = false;
  }
  return record;
}

constexpr std::string_view kPublicationCommitStateContext = "subagent_publication_commit_state";

ava::core::Error& with_publication_commit_state(ava::core::Error& error, SubagentPublicationCommitState state)
{
  return error.with_context(std::string(kPublicationCommitStateContext), std::string(to_string(state)));
}

ava::core::Error journal_publication_error(ava::core::Error const&, std::string const& job_id)
{
  auto error = ava::core::Error(ava::core::ErrorCategory::Session, "subagent coordinator latched after a durable journal failure");
  error.with_context("job_id", job_id);
  return error;
}

}  // namespace

SubagentInteractionGate::SubagentInteractionGate(SubagentJobMode mode, ava::permissions::PermissionResolver permission_resolver,
                                                 QuestionResolver question_resolver)
    : mode_(mode),
      permission_resolver_(mode == SubagentJobMode::Foreground ? std::move(permission_resolver) : nullptr),
      question_resolver_(mode == SubagentJobMode::Foreground ? std::move(question_resolver) : nullptr)
{
}

std::shared_ptr<SubagentInteractionGate> SubagentInteractionGate::create(SubagentJobMode mode, ava::permissions::PermissionResolver permission_resolver,
                                                                         QuestionResolver question_resolver)
{
  return std::shared_ptr<SubagentInteractionGate>(new SubagentInteractionGate(mode, std::move(permission_resolver), std::move(question_resolver)));
}

ava::permissions::PermissionResolver SubagentInteractionGate::permission_resolver()
{
  auto self = shared_from_this();
  return [self = std::move(self)](ava::permissions::PermissionPrompt const& prompt) -> ava::core::Result<ava::permissions::PermissionResolutionDecision> {
    ava::permissions::PermissionResolver resolver;
    {
      std::lock_guard lock(self->mutex_);
      if (self->mode_ != SubagentJobMode::Foreground || self->promotion_pending_)
        return std::unexpected(interaction_unavailable("permission", true));
      if (!self->permission_resolver_)
        return std::unexpected(interaction_unavailable("permission", false));
      ++self->outstanding_;
      resolver = self->permission_resolver_;
    }
    struct Completion final
    {
      std::shared_ptr<SubagentInteractionGate> gate;
      ~Completion()
      {
        std::lock_guard lock(gate->mutex_);
        --gate->outstanding_;
      }
    } completion{self};
    return resolver(prompt);
  };
}

QuestionResolver SubagentInteractionGate::question_resolver()
{
  auto self = shared_from_this();
  return [self = std::move(self)](QuestionPrompt const& prompt) -> ava::core::Result<QuestionAnswer> {
    QuestionResolver resolver;
    {
      std::lock_guard lock(self->mutex_);
      if (self->mode_ != SubagentJobMode::Foreground || self->promotion_pending_)
        return std::unexpected(interaction_unavailable("question", true));
      if (!self->question_resolver_)
        return std::unexpected(interaction_unavailable("question", false));
      ++self->outstanding_;
      resolver = self->question_resolver_;
    }
    struct Completion final
    {
      std::shared_ptr<SubagentInteractionGate> gate;
      ~Completion()
      {
        std::lock_guard lock(gate->mutex_);
        --gate->outstanding_;
      }
    } completion{self};
    return resolver(prompt);
  };
}

ava::core::VoidResult SubagentInteractionGate::prepare_promotion()
{
  std::lock_guard lock(mutex_);
  if (mode_ == SubagentJobMode::Background)
    return {};
  if (outstanding_ != 0)
  {
    auto error = ava::core::Error(ava::core::ErrorCategory::Tool, "cannot promote subagent while a foreground interaction is outstanding");
    error.with_context("error_code", "foreground_interaction_outstanding");
    return std::unexpected(std::move(error));
  }
  promotion_pending_ = true;
  return {};
}

void SubagentInteractionGate::commit_promotion()
{
  std::lock_guard lock(mutex_);
  mode_ = SubagentJobMode::Background;
  promotion_pending_ = false;
  permission_resolver_ = nullptr;
  question_resolver_ = nullptr;
}

void SubagentInteractionGate::abort_promotion()
{
  std::lock_guard lock(mutex_);
  promotion_pending_ = false;
}

void SubagentInteractionGate::finish()
{
  std::lock_guard lock(mutex_);
  mode_ = SubagentJobMode::Background;
  promotion_pending_ = false;
  permission_resolver_ = nullptr;
  question_resolver_ = nullptr;
}

std::string_view to_string(SubagentPublicationCommitState value) noexcept
{
  switch (value)
  {
    case SubagentPublicationCommitState::ProvenUnpublished:
      return "proven_unpublished";
    case SubagentPublicationCommitState::PublicationUncertain:
      return "publication_uncertain";
  }
  return "publication_uncertain";
}

SubagentPublicationCommitState subagent_publication_commit_state(ava::core::Error const& error) noexcept
{
  for (auto const& context : error.context())
    if (context.key == kPublicationCommitStateContext && context.value == to_string(SubagentPublicationCommitState::ProvenUnpublished))
      return SubagentPublicationCommitState::ProvenUnpublished;
  return SubagentPublicationCommitState::PublicationUncertain;
}

struct SubagentCoordinator::JournalState
{
  explicit JournalState(JobJournal journal_in) : journal(std::move(journal_in)) { }
  JobJournal journal;
  std::mutex mutex;
};

struct SubagentCoordinator::JobState
{
  SubagentJobSnapshot snapshot;
  std::shared_ptr<JournalState> journal;
  mutable std::mutex mutex;
  std::condition_variable changed;
  std::shared_ptr<SubagentInteractionGate> interaction_gate = nullptr;
  bool live = false;
  bool coordinator_latched = false;
  std::optional<std::string> coordinator_error = std::nullopt;
};

SubagentCoordinator::SubagentCoordinator(SubagentCoordinatorOptions options) : options_(std::move(options)), registry_(options_.registry_options)
{
}

ava::core::Result<std::shared_ptr<SubagentCoordinator>> SubagentCoordinator::create(SubagentCoordinatorOptions options)
{
  if (options.ava_state_dir.empty() || !options.ava_state_dir.is_absolute())
    return std::unexpected(ava::core::Error(ava::core::ErrorCategory::InvalidArgument, "subagent coordinator requires an absolute AVA state directory"));
  // Parent journals are activated only when their exact parent session is
  // opened or addressed. Startup must not discover, recover, or retain owner
  // leases for unrelated historical parents.
  return std::shared_ptr<SubagentCoordinator>(new SubagentCoordinator(std::move(options)));
}

SubagentCoordinator::~SubagentCoordinator()
{
  shutdown();
}

SubagentCoordinator::StartAdmission::~StartAdmission()
{
  std::lock_guard lock(coordinator_.mutex_);
  auto found = coordinator_.active_starts_by_parent_.find(parent_session_id_);
  if (found != coordinator_.active_starts_by_parent_.end() && --found->second == 0)
    coordinator_.active_starts_by_parent_.erase(found);
  coordinator_.admission_changed_.notify_all();
}

ava::core::VoidResult SubagentCoordinator::activate_parent(std::string parent_session_id)
{
  // Serializing activation under the coordinator mutex makes concurrent calls
  // idempotent. No JournalState mutex is held while publishing the recovered
  // projection, preserving the coordinator -> JobState lock order.
  std::lock_guard lock(mutex_);
  if (journals_.contains(parent_session_id))
    return {};

  auto opened = JobJournal::try_open_owned(options_.ava_state_dir, parent_session_id, options_.journal_limits);
  if (!opened)
    return std::unexpected(std::move(opened.error()));
  if (!*opened)
  {
    auto error = ava::core::Error(ava::core::ErrorCategory::PermissionDenied, "subagent jobs for this parent are managed by another AVA process");
    error.with_context("parent_session_id", parent_session_id).with_context("session_id", parent_session_id);
    return std::unexpected(std::move(error));
  }

  // JobJournal takes its own journal lock while retaining the just-acquired
  // owner lease. This is the sole recovery point for this activated parent.
  auto projection = (*opened)->recover_interrupted_jobs();
  if (!projection)
    return std::unexpected(std::move(projection.error()));
  auto journal = std::make_shared<JournalState>(std::move(**opened));
  journals_.emplace(parent_session_id, journal);
  for (auto const& snapshot : projection->jobs)
  {
    auto state = std::make_shared<JobState>();
    state->snapshot = snapshot;
    state->journal = journal;
    jobs_.emplace(snapshot.identity.job_id, std::move(state));
  }
  return {};
}

bool SubagentCoordinator::release_parent_if_idle(std::string_view parent_session_id)
{
  std::unordered_map<std::string, std::shared_ptr<JobState>> released_jobs;
  std::shared_ptr<JournalState> released_journal;
  {
    std::lock_guard lock(mutex_);
    // A start has not selected its journal until after admission. Retain this
    // parent while one is in flight so release cannot win between admission,
    // durable publication, and JobState publication; unrelated parents remain
    // independently releasable.
    if (active_starts_by_parent_.contains(std::string(parent_session_id)))
      return false;
    auto journal = journals_.find(std::string(parent_session_id));
    if (journal == journals_.end())
      return false;
    for (auto const& [_, state] : jobs_)
    {
      std::lock_guard state_lock(state->mutex);
      if (state->snapshot.identity.parent_session_id != parent_session_id)
        continue;
      if (!terminal(state->snapshot.execution) || state->snapshot.delivery == SubagentDeliveryState::Pending ||
          state->snapshot.delivery == SubagentDeliveryState::Attempting)
        return false;
    }
    for (auto job = jobs_.begin(); job != jobs_.end();)
    {
      std::lock_guard state_lock(job->second->mutex);
      if (job->second->snapshot.identity.parent_session_id == parent_session_id)
      {
        released_jobs.emplace(job->first, std::move(job->second));
        job = jobs_.erase(job);
      }
      else
      {
        ++job;
      }
    }
    released_journal = std::move(journal->second);
    journals_.erase(journal);
  }
  // Destroy terminal state before the journal so no retained JobState keeps
  // the per-parent owner descriptor alive after an accepted release.
  released_jobs.clear();
  released_journal.reset();
  return true;
}

ava::core::Result<std::shared_ptr<SubagentCoordinator::JournalState>> SubagentCoordinator::journal_for_parent(std::string const& parent_session_id)
{
  auto activated = activate_parent(parent_session_id);
  if (!activated)
    return std::unexpected(std::move(activated.error()));
  std::lock_guard lock(mutex_);
  return journals_.at(parent_session_id);
}

ava::core::Result<SubagentJobSnapshot> SubagentCoordinator::append_transition(std::shared_ptr<JobState> const& state, JobJournalRecord record)
{
  std::lock_guard journal_lock(state->journal->mutex);
  if (options_.journal_append_preflight)
  {
    if (auto allowed = options_.journal_append_preflight(record); !allowed)
    {
      auto error = std::move(allowed.error());
      error.with_context("subagent_journal_preflight_rejected", "true");
      return std::unexpected(std::move(error));
    }
  }
  auto projection = state->journal->journal.append(record);
  if (!projection)
    return std::unexpected(std::move(projection.error()));
  auto projected = projection->find(record.identity.job_id);
  if (projected == nullptr)
    return std::unexpected(ava::core::Error(ava::core::ErrorCategory::Session, "durable journal projection omitted the transitioned job"));
  std::lock_guard state_lock(state->mutex);
  state->snapshot = *projected;
  state->changed.notify_all();
  return state->snapshot;
}

void SubagentCoordinator::latch(std::shared_ptr<JobState> const& state, ava::core::Error const& error)
{
  auto formatted = error.format();
  if (formatted.size() > options_.journal_limits.max_error_bytes)
    formatted.resize(options_.journal_limits.max_error_bytes);
  // Keep the coordinator -> JobState order used by list, lookup, retention,
  // and shutdown. Callers must release JournalState before entering here.
  std::lock_guard coordinator_lock(mutex_);
  coordinator_latched_ = true;
  accepting_ = false;
  std::lock_guard state_lock(state->mutex);
  state->coordinator_latched = true;
  state->coordinator_error = std::move(formatted);
}

void SubagentCoordinator::synchronize_registry_retention()
{
  auto snapshots = registry_.snapshot();
  std::unordered_set<std::string> retained;
  retained.reserve(snapshots.size());
  for (auto const& snapshot : snapshots) retained.insert(snapshot.job_id);
  std::lock_guard lock(mutex_);
  for (auto job = jobs_.begin(); job != jobs_.end();)
  {
    bool live = false;
    {
      std::lock_guard state_lock(job->second->mutex);
      live = job->second->live;
    }
    if (live && !retained.contains(job->first))
      job = jobs_.erase(job);
    else
      ++job;
  }
}

ava::core::Result<SubagentCoordinatorJobSnapshot> SubagentCoordinator::start(std::string parent_session_id, SubagentJobMode mode,
                                                                             BackgroundJobStartOptions options, BackgroundJobWorker worker,
                                                                             std::shared_ptr<SubagentInteractionGate> interaction_gate)
{
  if (!worker)
    return std::unexpected(ava::core::Error(ava::core::ErrorCategory::InvalidArgument, "background job worker is unavailable"));
  {
    std::lock_guard lock(mutex_);
    if (!accepting_)
      return std::unexpected(ava::core::Error(ava::core::ErrorCategory::Tool, coordinator_latched_ ? "subagent coordinator is latched after a journal failure"
                                                                                                   : "subagent coordinator is shutting down"));
    ++active_starts_by_parent_[parent_session_id];
  }
  StartAdmission admission(*this, parent_session_id);
  auto journal = journal_for_parent(parent_session_id);
  if (!journal)
    return std::unexpected(std::move(journal.error()));
  SubagentJobIdentity identity{.job_id = ava::core::make_id("job"),
                               .task_id = options.child_session_id,
                               .parent_session_id = std::move(parent_session_id),
                               .child_session_id = options.child_session_id,
                               .delivery_id = ava::core::make_id("delivery")};
  auto state = std::make_shared<JobState>();
  state->journal = *journal;
  state->interaction_gate = std::move(interaction_gate);
  auto started_at = ava::session::now_timestamp();
  auto started = append_transition(state, JobJournalRecord{.kind = JobJournalTransitionKind::Started, .identity = identity, .at = started_at, .mode = mode});
  if (!started)
  {
    auto error = std::move(started.error());
    // The test preflight runs before JobJournal mutation; any actual append
    // error is conservatively uncertain because an fsync/write failure may
    // have reached durable storage.
    if (std::ranges::any_of(error.context(), [](ava::core::ErrorContext const& context) {
          return context.key == "subagent_journal_preflight_rejected" && context.value == "true";
        }))
    {
      with_publication_commit_state(error, SubagentPublicationCommitState::ProvenUnpublished);
    }
    return std::unexpected(std::move(error));
  }

  options.job_id = identity.job_id;
  auto published = registry_.start(std::move(options), [this, state, worker = std::move(worker)](BackgroundJobContext const& context) mutable {
    BackgroundJobCompletion completion;
    try
    {
      completion = worker(context);
    }
    catch (std::exception const& error)
    {
      completion = exception_completion(context.job_id, context.stop_token, error.what());
    }
    catch (...)
    {
      completion = exception_completion(context.job_id, context.stop_token, "unknown exception");
    }
    return persist_terminal(state, std::move(completion));
  });
  if (!published)
  {
    auto publication_error = std::move(published.error());
    ava::core::Result<SubagentJobProjection> rolled_back =
        std::unexpected(ava::core::Error(ava::core::ErrorCategory::Unknown, "unpublished-start rollback was not attempted"));
    {
      std::lock_guard journal_lock(state->journal->mutex);
      if (options_.journal_rollback_preflight)
      {
        if (auto allowed = options_.journal_rollback_preflight(identity); !allowed)
          rolled_back = std::unexpected(std::move(allowed.error()));
        else
          rolled_back = state->journal->journal.rollback_unpublished_started(identity);
      }
      else
      {
        rolled_back = state->journal->journal.rollback_unpublished_started(identity);
      }
    }
    if (rolled_back)
    {
      with_publication_commit_state(publication_error, SubagentPublicationCommitState::ProvenUnpublished);
    }
    else
    {
      // Keep the durable Started record and child identity for startup
      // recovery. AgentLoop sees PublicationUncertain and must not delete it.
      latch(state, rolled_back.error());
      publication_error.with_context("journal_rollback_failure", rolled_back.error().format());
      with_publication_commit_state(publication_error, SubagentPublicationCommitState::PublicationUncertain);
    }
    return std::unexpected(std::move(publication_error));
  }
  {
    std::lock_guard state_lock(state->mutex);
    state->live = true;
  }
  {
    std::lock_guard lock(mutex_);
    jobs_.emplace(identity.job_id, state);
  }
  synchronize_registry_retention();
  std::lock_guard state_lock(state->mutex);
  return public_snapshot_locked(*state);
}

ava::core::Result<SubagentCoordinatorJobSnapshot> SubagentCoordinator::start_background(std::string parent_session_id, BackgroundJobStartOptions options,
                                                                                        BackgroundJobWorker worker)
{
  return start(std::move(parent_session_id), SubagentJobMode::Background, std::move(options), std::move(worker));
}

BackgroundJobCompletion SubagentCoordinator::persist_terminal(std::shared_ptr<JobState> const& state, BackgroundJobCompletion completion)
{
  std::string job_id;
  SubagentJobIdentity identity;
  {
    std::lock_guard lock(state->mutex);
    identity = state->snapshot.identity;
    job_id = identity.job_id;
  }
  completion = normalize(job_id, std::move(completion));
  auto const at = ava::session::now_timestamp();
  ava::core::Result<SubagentJobProjection> persisted =
      std::unexpected(ava::core::Error(ava::core::ErrorCategory::Unknown, "terminal journal preflight failed"));
  bool preflight_failed = false;
  bool projection_missing = false;
  {
    // Promotion and completion serialize on the durable journal lock. The
    // mode observed here therefore decides atomically whether terminal output
    // is direct or requires background delivery.
    std::lock_guard journal_lock(state->journal->mutex);
    SubagentJobMode mode = SubagentJobMode::Foreground;
    {
      std::lock_guard state_lock(state->mutex);
      mode = state->snapshot.mode;
    }
    std::vector<JobJournalRecord> records{terminal_record(identity, completion, at)};
    if (mode == SubagentJobMode::Background)
      records.push_back(JobJournalRecord{.kind = JobJournalTransitionKind::DeliveryPending, .identity = identity, .at = at});
    for (auto const& record : records)
    {
      if (options_.journal_append_preflight)
      {
        if (auto allowed = options_.journal_append_preflight(record); !allowed)
        {
          persisted = std::unexpected(std::move(allowed.error()));
          preflight_failed = true;
          break;
        }
      }
    }
    if (!preflight_failed)
      persisted = records.size() == 1 ? state->journal->journal.append(records.front()) : state->journal->journal.append_batch(records);
    if (persisted)
    {
      auto projected = persisted->find(job_id);
      if (projected == nullptr)
      {
        projection_missing = true;
      }
      else
      {
        std::lock_guard state_lock(state->mutex);
        state->snapshot = *projected;
        state->changed.notify_all();
      }
    }
  }
  if (!persisted)
  {
    latch(state, persisted.error());
    auto error = journal_publication_error(persisted.error(), job_id);
    return {.state = BackgroundJobState::Failed, .final_text = {}, .stop_reason = "journal_failure", .error = std::move(error)};
  }
  if (projection_missing)
  {
    auto error = ava::core::Error(ava::core::ErrorCategory::Session, "durable terminal projection omitted the subagent job");
    latch(state, error);
    return {.state = BackgroundJobState::Failed, .final_text = {}, .stop_reason = "journal_failure", .error = std::move(error)};
  }

  // Delivery notification is deliberately outside all coordinator, state, and
  // journal locks. A sink is advisory: even allocation/copy failures while
  // materializing the notification cannot rewrite durable child completion.
  try
  {
    SubagentTerminalSink sink;
    SubagentCoordinatorJobSnapshot notification;
    {
      std::lock_guard coordinator_lock(mutex_);
      sink = terminal_sink_;
      std::lock_guard state_lock(state->mutex);
      if (state->snapshot.delivery == SubagentDeliveryState::Pending || state->snapshot.delivery == SubagentDeliveryState::Attempting)
        notification = public_snapshot_locked(*state);
      else
        sink = nullptr;
    }
    if (sink)
      sink(notification);
  }
  catch (...)
  {
  }
  return completion;
}

std::shared_ptr<SubagentCoordinator::JobState> SubagentCoordinator::find_owned_locked(std::string_view parent_session_id, std::string_view job_id) const
{
  auto found = jobs_.find(std::string(job_id));
  if (found == jobs_.end())
    return nullptr;
  std::lock_guard state_lock(found->second->mutex);
  if (found->second->snapshot.identity.parent_session_id != parent_session_id)
    return nullptr;
  return found->second;
}

ava::core::Result<std::shared_ptr<SubagentCoordinator::JobState>> SubagentCoordinator::restore_owned(std::string_view parent_session_id,
                                                                                                     std::string_view job_id)
{
  auto journal = journal_for_parent(std::string(parent_session_id));
  if (!journal)
    return std::unexpected(std::move(journal.error()));
  ava::core::Result<SubagentJobProjection> projection =
      std::unexpected(ava::core::Error(ava::core::ErrorCategory::Unknown, "subagent journal projection was not loaded"));
  {
    std::lock_guard journal_lock((*journal)->mutex);
    projection = (*journal)->journal.projection();
  }
  if (!projection)
    return std::unexpected(std::move(projection.error()));
  auto const* restored = projection->find(job_id);
  if (!restored || restored->identity.parent_session_id != parent_session_id)
    return std::unexpected(not_found(job_id));
  auto candidate = std::make_shared<JobState>();
  candidate->snapshot = *restored;
  candidate->journal = *journal;
  std::lock_guard lock(mutex_);
  if (auto existing = find_owned_locked(parent_session_id, job_id))
    return existing;
  jobs_.emplace(restored->identity.job_id, candidate);
  return candidate;
}

SubagentCoordinatorJobSnapshot SubagentCoordinator::public_snapshot_locked(JobState const& state, bool timed_out) const
{
  return {.job = state.snapshot, .timed_out = timed_out, .coordinator_latched = state.coordinator_latched, .coordinator_error = state.coordinator_error};
}

std::vector<SubagentCoordinatorJobSnapshot> SubagentCoordinator::list(std::string_view parent_session_id) const
{
  std::vector<SubagentCoordinatorJobSnapshot> result;
  std::lock_guard lock(mutex_);
  for (auto const& [_, state] : jobs_)
  {
    std::lock_guard state_lock(state->mutex);
    if (state->snapshot.identity.parent_session_id == parent_session_id)
      result.push_back(public_snapshot_locked(*state));
  }
  std::ranges::sort(result, [](auto const& left, auto const& right) {
    if (left.job.started_at != right.job.started_at)
      return left.job.started_at < right.job.started_at;
    return left.job.identity.job_id < right.job.identity.job_id;
  });
  return result;
}

ava::core::Result<std::vector<SubagentCoordinatorJobSnapshot>> SubagentCoordinator::pending_deliveries(std::string_view parent_session_id)
{
  auto activated = activate_parent(std::string(parent_session_id));
  if (!activated)
    return std::unexpected(std::move(activated.error()));
  std::shared_ptr<JournalState> journal;
  {
    std::lock_guard lock(mutex_);
    journal = journals_.at(std::string(parent_session_id));
  }
  ava::core::Result<SubagentJobProjection> projection =
      std::unexpected(ava::core::Error(ava::core::ErrorCategory::Unknown, "subagent journal projection was not loaded"));
  {
    std::lock_guard journal_lock(journal->mutex);
    projection = journal->journal.projection();
  }
  if (!projection)
    return std::unexpected(std::move(projection.error()));
  std::vector<SubagentCoordinatorJobSnapshot> pending;
  for (auto const& job : projection->jobs)
  {
    if (job.delivery == SubagentDeliveryState::Pending || job.delivery == SubagentDeliveryState::Attempting)
      pending.push_back(SubagentCoordinatorJobSnapshot{.job = job});
  }
  return pending;
}

ava::core::Result<SubagentCoordinatorJobSnapshot> SubagentCoordinator::snapshot(std::string_view parent_session_id, std::string_view job_id)
{
  auto activated = activate_parent(std::string(parent_session_id));
  if (!activated)
    return std::unexpected(std::move(activated.error()));
  std::shared_ptr<JobState> state;
  {
    std::lock_guard lock(mutex_);
    state = find_owned_locked(parent_session_id, job_id);
  }
  if (!state)
    return std::unexpected(not_found(job_id));
  std::lock_guard state_lock(state->mutex);
  return public_snapshot_locked(*state);
}

ava::core::Result<SubagentCoordinatorJobSnapshot> SubagentCoordinator::wait(std::string_view parent_session_id, std::string_view job_id,
                                                                            std::chrono::milliseconds timeout, SubagentWaitMode mode)
{
  auto activated = activate_parent(std::string(parent_session_id));
  if (!activated)
    return std::unexpected(std::move(activated.error()));
  std::shared_ptr<JobState> state;
  {
    std::lock_guard lock(mutex_);
    state = find_owned_locked(parent_session_id, job_id);
  }
  if (!state)
    return std::unexpected(not_found(job_id));

  auto const deadline = std::chrono::steady_clock::now() + timeout;
  auto const ready = [&] {
    return !state->live || terminal(state->snapshot.execution) || (mode == SubagentWaitMode::TerminalOrPromotion && state->snapshot.was_promoted);
  };
  std::unique_lock state_lock(state->mutex);
  if (!ready() && !state->changed.wait_for(state_lock, timeout, ready))
    return public_snapshot_locked(*state, true);
  bool const is_terminal = terminal(state->snapshot.execution);
  bool const was_live = state->live;
  bool const was_promoted = state->snapshot.was_promoted;
  auto gate = state->interaction_gate;
  auto result = public_snapshot_locked(*state);
  state_lock.unlock();
  if (was_promoted && gate)
    gate->commit_promotion();
  if (is_terminal && was_live)
  {
    auto const now = std::chrono::steady_clock::now();
    auto const remaining = now < deadline ? std::chrono::duration_cast<std::chrono::milliseconds>(deadline - now) : std::chrono::milliseconds(0);
    auto registry_snapshot = registry_.wait_snapshot(job_id, remaining);
    if (!registry_snapshot)
      return std::unexpected(std::move(registry_snapshot.error()));
    if (registry_snapshot->timed_out)
      result.timed_out = true;
    else
    {
      static_cast<void>(registry_.join_finished());
      synchronize_registry_retention();
    }
  }
  return result;
}

ava::core::Result<SubagentCoordinatorJobSnapshot> SubagentCoordinator::result(std::string_view parent_session_id, std::string_view job_id)
{
  auto current = snapshot(parent_session_id, job_id);
  if (!current)
    return std::unexpected(std::move(current.error()));
  if (!terminal(current->job.execution))
  {
    auto error = ava::core::Error(ava::core::ErrorCategory::Tool, "subagent job is not ready");
    error.with_context("job_error_code", "job_not_ready").with_context("job_id", std::string(job_id));
    return std::unexpected(std::move(error));
  }
  return current;
}

ava::core::Result<SubagentCoordinatorJobSnapshot> SubagentCoordinator::cancel(std::string_view parent_session_id, std::string_view job_id)
{
  auto activated = activate_parent(std::string(parent_session_id));
  if (!activated)
    return std::unexpected(std::move(activated.error()));
  std::shared_ptr<JobState> state;
  {
    std::lock_guard lock(mutex_);
    state = find_owned_locked(parent_session_id, job_id);
  }
  if (!state)
    return std::unexpected(not_found(job_id));
  SubagentJobSnapshot current;
  {
    std::lock_guard state_lock(state->mutex);
    current = state->snapshot;
  }
  if (!terminal(current.execution) && !current.cancel_requested)
  {
    auto appended = append_transition(
        state, JobJournalRecord{.kind = JobJournalTransitionKind::CancelRequested, .identity = current.identity, .at = ava::session::now_timestamp()});
    if (!appended)
    {
      // Completion may have won after the caller's snapshot but before the
      // journal lock. In that case cancellation is already idempotently done.
      auto latest = snapshot(parent_session_id, job_id);
      if (latest && terminal(latest->job.execution))
        return latest;
      latch(state, appended.error());
      static_cast<void>(registry_.cancel(job_id));
      return std::unexpected(std::move(appended.error()));
    }
  }
  if (!terminal(current.execution))
  {
    auto canceled = registry_.cancel(job_id);
    if (!canceled)
      return std::unexpected(std::move(canceled.error()));
  }
  return snapshot(parent_session_id, job_id);
}

ava::core::Result<SubagentCoordinatorJobSnapshot> SubagentCoordinator::promote(std::string_view parent_session_id, std::string_view job_id)
{
  auto activated = activate_parent(std::string(parent_session_id));
  if (!activated)
    return std::unexpected(std::move(activated.error()));
  std::shared_ptr<JobState> state;
  {
    std::lock_guard lock(mutex_);
    state = find_owned_locked(parent_session_id, job_id);
  }
  if (!state)
    return std::unexpected(not_found(job_id));

  SubagentJobSnapshot current;
  std::shared_ptr<SubagentInteractionGate> gate;
  {
    std::lock_guard state_lock(state->mutex);
    current = state->snapshot;
    gate = state->interaction_gate;
    if (terminal(current.execution) || current.was_promoted)
      return public_snapshot_locked(*state);
  }
  if (current.mode != SubagentJobMode::Foreground)
  {
    auto error = ava::core::Error(ava::core::ErrorCategory::Tool, "only a running foreground subagent can be promoted");
    error.with_context("job_id", std::string(job_id));
    return std::unexpected(std::move(error));
  }
  if (current.cancel_requested)
  {
    auto error = ava::core::Error(ava::core::ErrorCategory::Tool, "cannot promote a subagent after cancellation was requested");
    error.with_context("job_id", std::string(job_id));
    return std::unexpected(std::move(error));
  }
  if (gate)
  {
    if (auto prepared = gate->prepare_promotion(); !prepared)
      return std::unexpected(std::move(prepared.error()));
  }
  auto promoted =
      append_transition(state, JobJournalRecord{.kind = JobJournalTransitionKind::Promoted, .identity = current.identity, .at = ava::session::now_timestamp()});
  if (!promoted)
  {
    auto latest = snapshot(parent_session_id, job_id);
    if (latest && terminal(latest->job.execution))
    {
      if (gate)
        gate->abort_promotion();
      return latest;
    }
    if (latest && latest->job.was_promoted)
    {
      if (gate)
        gate->commit_promotion();
      return latest;
    }
    if (latest && latest->job.cancel_requested)
    {
      if (gate)
        gate->abort_promotion();
      auto error = ava::core::Error(ava::core::ErrorCategory::Tool, "cannot promote a subagent after cancellation was requested");
      error.with_context("job_id", std::string(job_id));
      return std::unexpected(std::move(error));
    }
    bool const proven_preflight_failure = std::ranges::any_of(promoted.error().context(), [](ava::core::ErrorContext const& context) {
      return context.key == "subagent_journal_preflight_rejected" && context.value == "true";
    });
    if (gate)
    {
      if (proven_preflight_failure)
        gate->abort_promotion();
      else
        // A write/fsync failure can leave the Promoted record durable. Fail
        // closed by releasing frontend callbacks even though publication is
        // uncertain and the coordinator is latched.
        gate->commit_promotion();
    }
    latch(state, promoted.error());
    return std::unexpected(std::move(promoted.error()));
  }
  if (gate)
    gate->commit_promotion();
  std::lock_guard state_lock(state->mutex);
  return public_snapshot_locked(*state);
}

void SubagentCoordinator::set_terminal_sink(SubagentTerminalSink sink)
{
  std::vector<SubagentCoordinatorJobSnapshot> pending;
  {
    std::lock_guard lock(mutex_);
    terminal_sink_ = sink;
    if (sink)
    {
      for (auto const& [_, state] : jobs_)
      {
        std::lock_guard state_lock(state->mutex);
        if (state->snapshot.delivery == SubagentDeliveryState::Pending || state->snapshot.delivery == SubagentDeliveryState::Attempting)
          pending.push_back(public_snapshot_locked(*state));
      }
    }
  }
  for (auto const& notification : pending)
  {
    try
    {
      sink(notification);
    }
    catch (...)
    {
    }
  }
}

ava::core::Result<SubagentCoordinatorJobSnapshot> SubagentCoordinator::record_delivery_attempt(std::string_view parent_session_id, std::string_view job_id,
                                                                                               std::string attempt_id, std::string prompt_fingerprint)
{
  auto activated = activate_parent(std::string(parent_session_id));
  if (!activated)
    return std::unexpected(std::move(activated.error()));
  std::shared_ptr<JobState> state;
  {
    std::lock_guard lock(mutex_);
    state = find_owned_locked(parent_session_id, job_id);
  }
  if (!state)
  {
    auto restored = restore_owned(parent_session_id, job_id);
    if (!restored)
      return std::unexpected(std::move(restored.error()));
    state = std::move(*restored);
  }
  SubagentJobIdentity identity;
  {
    std::lock_guard state_lock(state->mutex);
    identity = state->snapshot.identity;
  }
  auto appended = append_transition(state, JobJournalRecord{.kind = JobJournalTransitionKind::DeliveryAttempt,
                                                            .identity = std::move(identity),
                                                            .at = ava::session::now_timestamp(),
                                                            .attempt_id = std::move(attempt_id),
                                                            .prompt_fingerprint = std::move(prompt_fingerprint)});
  if (!appended)
    return std::unexpected(std::move(appended.error()));
  std::lock_guard state_lock(state->mutex);
  return public_snapshot_locked(*state);
}

ava::core::Result<SubagentCoordinatorJobSnapshot> SubagentCoordinator::acknowledge_delivery(std::string_view parent_session_id, std::string_view job_id,
                                                                                            std::string_view attempt_id, std::string committed_turn_id)
{
  auto activated = activate_parent(std::string(parent_session_id));
  if (!activated)
    return std::unexpected(std::move(activated.error()));
  std::shared_ptr<JobState> state;
  {
    std::lock_guard lock(mutex_);
    state = find_owned_locked(parent_session_id, job_id);
  }
  if (!state)
  {
    auto restored = restore_owned(parent_session_id, job_id);
    if (!restored)
      return std::unexpected(std::move(restored.error()));
    state = std::move(*restored);
  }
  SubagentJobIdentity identity;
  {
    std::lock_guard state_lock(state->mutex);
    identity = state->snapshot.identity;
  }
  auto appended = append_transition(state, JobJournalRecord{.kind = JobJournalTransitionKind::DeliveryAck,
                                                            .identity = std::move(identity),
                                                            .at = ava::session::now_timestamp(),
                                                            .attempt_id = std::string(attempt_id),
                                                            .committed_turn_id = std::move(committed_turn_id)});
  if (!appended)
    return std::unexpected(std::move(appended.error()));
  std::lock_guard state_lock(state->mutex);
  return public_snapshot_locked(*state);
}

void SubagentCoordinator::shutdown()
{
  std::vector<std::pair<std::string, std::string>> jobs;
  {
    std::unique_lock lock(mutex_);
    if (shutdown_complete_)
      return;
    accepting_ = false;
    terminal_sink_ = nullptr;
    admission_changed_.wait(lock, [this] { return active_starts_by_parent_.empty(); });
    for (auto const& [job_id, state] : jobs_)
    {
      std::lock_guard state_lock(state->mutex);
      if (state->live && !terminal(state->snapshot.execution))
        jobs.emplace_back(state->snapshot.identity.parent_session_id, job_id);
    }
  }
  for (auto const& [parent, job_id] : jobs) static_cast<void>(cancel(parent, job_id));
  registry_.shutdown();
  std::unordered_map<std::string, std::shared_ptr<JournalState>> released_journals;
  std::unordered_map<std::string, std::shared_ptr<JobState>> released_jobs;
  {
    std::lock_guard lock(mutex_);
    shutdown_complete_ = true;
    released_journals.swap(journals_);
    released_jobs.swap(jobs_);
  }
  // Releasing the last JobState/JournalState references closes each
  // process-lifetime owner descriptor only after every worker and durable
  // shutdown transition has completed.
}

}  // namespace ava::agent
