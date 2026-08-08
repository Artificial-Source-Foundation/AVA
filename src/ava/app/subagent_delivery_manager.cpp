#include "sys.h"
#include "ava/debug/debug_ostream_operators.h"
#include "ava/core/thread.h"
#include "ava/http/transport.h"
#include "ava/app/headless_policy.h"
#include "ava/app/runtime.h"
#include "ava/app/runtime/Session.h"
#include "ava/app/runtime/session_ts.h"
#include "ava/app/subagent_delivery_manager.h"
#include "ava/session/session_store.h"
#include "ava/permissions/permission_rules.h"
#include "ava/core/fingerprint.h"
#include "ava/core/ids.h"
#include "ava/core/json.h"
#include "ava/core/path.h"

#include <algorithm>
#include <chrono>
#include <iomanip>
#include <limits>
#include <sstream>
#include <string>
#include <thread>
#include <utility>
#include "debug.h"

#ifdef CWDEBUG
#include <utils/at_scope_end.h>
#endif

namespace ava::app {
namespace {

constexpr std::string_view kDeliveryMarkerPrefix = "[AVA_SUBAGENT_DELIVERY_V1 delivery_id=";

class BoundedDeliveryTransport final : public ava::http::Transport
{
 public:
  BoundedDeliveryTransport(std::unique_ptr<ava::http::Transport> inner, std::stop_token stop_token, std::chrono::steady_clock::time_point deadline)
      : inner_(std::move(inner)), stop_token_(stop_token), deadline_(deadline)
  {
  }

  ava::core::Result<ava::http::HttpResponse> send(ava::http::HttpRequest const& request) override { return send(request, nullptr); }

  ava::core::Result<ava::http::HttpResponse> send(ava::http::HttpRequest const& request, CancelCallback cancel_requested) override
  {
    auto bounded = bounded_request(request);
    return inner_->send(bounded, [this, cancel_requested] { return canceled() || (cancel_requested && cancel_requested()); });
  }

  bool supports_streaming() const noexcept override { return inner_->supports_streaming(); }

  ava::core::Result<ava::http::HttpResponse> send_streaming(ava::http::HttpRequest const& request, BodyChunkSink on_body_chunk,
                                                            CancelCallback cancel_requested = nullptr) override
  {
    auto bounded = bounded_request(request);
    return inner_->send_streaming(bounded, std::move(on_body_chunk),
                                  [this, cancel_requested] { return canceled() || (cancel_requested && cancel_requested()); });
  }

 private:
  ava::http::HttpRequest bounded_request(ava::http::HttpRequest request) const
  {
    auto const now = std::chrono::steady_clock::now();
    auto const remaining = now < deadline_ ? std::chrono::duration_cast<std::chrono::milliseconds>(deadline_ - now).count() : 0;
    auto const bounded_ms = static_cast<int>(std::clamp<long long>(remaining, 1, std::numeric_limits<int>::max()));
    request.timeout_ms = request.timeout_ms > 0 ? std::min(request.timeout_ms, bounded_ms) : bounded_ms;
    return request;
  }

  bool canceled() const noexcept { return stop_token_.stop_requested() || std::chrono::steady_clock::now() >= deadline_; }

  std::unique_ptr<ava::http::Transport> inner_;
  std::stop_token stop_token_;
  std::chrono::steady_clock::time_point deadline_;
};

bool delivery_pending(ava::agent::SubagentJobSnapshot const& job)
{
  return job.delivery == ava::agent::SubagentDeliveryState::Pending || job.delivery == ava::agent::SubagentDeliveryState::Attempting;
}

bool execution_live(ava::agent::SubagentExecutionState state)
{
  return state == ava::agent::SubagentExecutionState::Starting || state == ava::agent::SubagentExecutionState::Running;
}

std::filesystem::path normalized_workspace_identity(std::filesystem::path const& workspace)
{
  return ava::core::normalized_absolute_path(workspace);
}

ava::core::Error retained_owner_not_found(std::string_view session_id)
{
  auto error = ava::core::Error(ava::core::ErrorCategory::NotFound, "retained session not found for this workspace");
  error.with_context("session_id", std::string(session_id));
  return error;
}

void clear_secret(std::string& secret) noexcept
{
  std::fill(secret.begin(), secret.end(), '\0');
  secret.clear();
}

runtime::RunOptions detached_run_options(runtime::RunOptions const& source)
{
  runtime::RunOptions result;
  result.access_token = source.access_token;
  result.credential_type = source.credential_type;
  result.openai_oauth = source.openai_oauth;
  result.openai_account_id = source.openai_account_id;
  result.stream = source.stream;
  result.enable_transport_retries = source.enable_transport_retries;
  result.exact_builtin_tool_names = std::vector<std::string>{};
  result.isolate_ambient_extensions = true;
  result.disable_session_mcp = true;
  result.require_descriptor_secure_workspace = source.require_descriptor_secure_workspace;
  result.announce_execution_after_permission = source.announce_execution_after_permission;
  result.redact_permission_audit_arguments = source.redact_permission_audit_arguments;
  result.require_explicit_file_permissions = source.require_explicit_file_permissions;
  result.exact_file_access = nullptr;
  result.command_executor = nullptr;
  result.offline = source.offline;
  result.expand_prompt_file_references = false;
  return result;
}

std::string delivery_marker(ava::agent::SubagentJobIdentity const& identity)
{
  return std::string(kDeliveryMarkerPrefix) + identity.delivery_id + "]";
}

std::string delivery_prompt(ava::agent::SubagentJobSnapshot const& job)
{
  std::string prompt = delivery_marker(job.identity);
  prompt += "\nAutomatic subagent completion delivery. Integrate this result into the parent conversation exactly once. ";
  prompt += "If this delivery marker already appears earlier, do not duplicate work; briefly acknowledge the existing integration.\n";
  prompt += "delivery_id: " + job.identity.delivery_id;
  prompt += "\njob_id: " + job.identity.job_id;
  prompt += "\ntask_id: " + job.identity.task_id;
  prompt += "\nchild_session_id: " + job.identity.child_session_id;
  prompt += "\nstate: " + std::string(ava::agent::to_string(job.execution));
  prompt += "\nsummary_truncated: " + std::string(job.summary_truncated ? "true" : "false");
  prompt += "\nerror_truncated: " + std::string(job.error_truncated ? "true" : "false");
  prompt += "\nstop_reason_truncated: " + std::string(job.stop_reason_truncated ? "true" : "false");
  prompt += "\nprovider_iterations: " + std::to_string(job.provider_iterations);
  prompt += "\ntool_calls: " + std::to_string(job.tool_calls);
  prompt += "\ntool_iterations: " + std::to_string(job.tool_iterations);
  if (job.execution == ava::agent::SubagentExecutionState::Completed)
    prompt += "\ncompleted_summary:\n" + job.summary.value_or("(completed without a summary)");
  else
    prompt += "\nstatus: The child did not complete successfully. Refer to child session " + job.identity.child_session_id +
              " for the canonical explicit result; no raw provider or tool error is included in this delivery.";
  return prompt;
}

std::string prompt_fingerprint(std::string_view prompt)
{
  std::ostringstream out;
  out << std::hex << std::setfill('0') << std::setw(16) << ava::core::content_fingerprint(prompt);
  return out.str();
}

std::optional<std::string> committed_delivery_turn(std::vector<ava::session::SessionEntry> const& entries, std::string_view delivery_id,
                                                   std::string_view fingerprint)
{
  for (std::size_t index = 0; index < entries.size(); ++index)
  {
    auto const& entry = entries[index];
    if (entry.type != ava::session::EntryType::UserMessage)
      continue;
    auto provenance = ava::session::parse_synthetic_delivery_provenance(entry);
    if (!provenance || !*provenance || (*provenance)->source != ava::session::kSyntheticSubagentDeliverySource || (*provenance)->delivery_id != delivery_id ||
        (*provenance)->prompt_fingerprint != fingerprint)
      continue;
    for (std::size_t following = index + 1; following < entries.size(); ++following)
    {
      if (entries[following].type == ava::session::EntryType::UserMessage && !ava::session::is_internal_replay_user_message(entries[following]))
        break;
      if (entries[following].type == ava::session::EntryType::AssistantTurnCommit)
      {
        auto commit = ava::session::parse_assistant_turn_commit(entries[following]);
        bool has_function_call = false;
        if (commit)
        {
          for (std::size_t item_index = index + 1; item_index < following; ++item_index)
          {
            if (entries[item_index].type != ava::session::EntryType::AssistantOutputItem)
              continue;
            auto item = ava::session::parse_assistant_output_item(entries[item_index]);
            if (item && item->assistant_turn_id == commit->assistant_turn_id && item->kind == ava::session::AssistantOutputItemKind::FunctionCall)
            {
              has_function_call = true;
              break;
            }
          }
        }
        if (commit && commit->finish_reason != "tool_calls" && !has_function_call)
          return entries[following].id;
      }
    }
  }
  return std::nullopt;
}

}  // namespace

struct SubagentDeliveryManager::ParentCapsule
{
  ParentCapsule(runtime::Session_aggregate_base&& detached_state, runtime::RunOptions options_in, CapsuleGeneration generation_in)
      : unlocked_session(std::move(detached_state)), run_options(std::move(options_in)), generation(generation_in)
  {
  }
  // Background delivery retains only this detached, independently leased copy; every access below is scoped through rat/wat guards.
  runtime::session_ts unlocked_session;
  runtime::RunOptions run_options;
  CapsuleGeneration generation = 0;

  ~ParentCapsule() { clear_secret(run_options.access_token); }
};

SubagentDeliveryManager::SubagentDeliveryManager(SubagentDeliveryManagerOptions options) : options_(std::move(options)), coordinator_(options_.coordinator)
{
  if (!options_.provider_bundle_factory)
    options_.provider_bundle_factory = create_runtime_provider_run_bundle;
}

ava::core::Result<std::shared_ptr<SubagentDeliveryManager>> SubagentDeliveryManager::create(SubagentDeliveryManagerOptions options)
{
  if (!options.coordinator)
    return std::unexpected(ava::core::Error(ava::core::ErrorCategory::InvalidArgument, "subagent delivery manager requires a coordinator"));
  if (options.max_queued_deliveries == 0 || options.max_retained_parents == 0 || options.max_delivery_attempts < 2 ||
      options.max_delivery_attempts > ava::agent::kMaxSubagentDeliveryAttemptHistory || options.delivery_deadline.count() <= 0 ||
      options.admission_retry_interval.count() <= 0)
    return std::unexpected(ava::core::Error(ava::core::ErrorCategory::InvalidArgument,
                                            "subagent delivery manager limits must be positive with between two and 64 delivery attempts"));
  auto manager = std::shared_ptr<SubagentDeliveryManager>(new SubagentDeliveryManager(std::move(options)));
  manager->start();
  return manager;
}

void SubagentDeliveryManager::start()
{
  std::weak_ptr<SubagentDeliveryManager> weak = shared_from_this();
  coordinator_->set_terminal_sink([weak](ava::agent::SubagentCoordinatorJobSnapshot const& snapshot) {
    if (auto manager = weak.lock())
      manager->enqueue(snapshot);
  });
  worker_ = ava::core::JoinThread::create("SA_delivery", [this](std::stop_token stop_token) { worker_loop(stop_token); });
}

SubagentDeliveryManager::~SubagentDeliveryManager()
{
  shutdown();
}

std::shared_ptr<ava::agent::SubagentCoordinator> const& SubagentDeliveryManager::coordinator() const noexcept
{
  return coordinator_;
}

ava::core::Result<SubagentDeliveryManager::CapsuleGeneration> SubagentDeliveryManager::refresh_parent(runtime::session_ts const& unlocked_session,
                                                                                                      runtime::RunOptions const& options)
{
  DoutEntering(dc::notice, "SubagentDeliveryManager::refresh_parent(" << unlocked_session << ", " << options << ")");
#ifdef CWDEBUG
  auto&& f = at_scope_end([]{ Dout(dc::notice, "Leaving SubagentDeliveryManager::refresh_parent()"); });
#endif

  CRITICAL_AREA_BEGIN_CR(session);

  if (!session_r->run_controller())
    return std::unexpected(ava::core::Error(ava::core::ErrorCategory::InvalidArgument, "cannot retain a parent without a run controller"));
  auto authority = session_r->read_authority_1();
  if (!authority)
    return std::unexpected(std::move(authority.error()));
  ava::session::SessionLease lease;
  if (!session_r->sessionless())
  {
    auto duplicated = session_r->lease().duplicate();
    if (!duplicated)
      return std::unexpected(std::move(duplicated.error()));
    lease = std::move(*duplicated);
  }
  auto detached_state = session_r->create_detached_state(std::move(lease), *authority, nullptr);

  auto safe_options = detached_run_options(options);
  auto const session_id = session_r->store.session_id();

  CRITICAL_AREA_END_R(session);

  CapsuleGeneration published_generation = 0;
  {
    std::lock_guard lock(mutex_);
    if (!accepting_)
      return std::unexpected(ava::core::Error(ava::core::ErrorCategory::InvalidArgument, "subagent delivery manager is shutting down"));
    auto found = parents_.find(session_id);
    if (found == parents_.end() && parents_.size() >= options_.max_retained_parents)
      return std::unexpected(ava::core::Error(ava::core::ErrorCategory::Tool, "automatic subagent delivery parent retention limit reached"));
    published_generation = next_generation_++;
    if (next_generation_ == 0)
      next_generation_ = 1;
    auto refreshed = std::make_shared<ParentCapsule>(std::move(detached_state), std::move(safe_options), published_generation);
    if (found == parents_.end())
      parents_.emplace(session_id, std::move(refreshed));
    else
    {
      // Capsules are immutable after publication. A delivery already holding
      // the previous generation completes safely while this fresh generation
      // becomes visible to later work.
      found->second = std::move(refreshed);
    }
  }
  auto pending = coordinator_->pending_deliveries(session_id);
  if (pending)
    for (auto const& delivery : *pending) enqueue(delivery);
  changed_.notify_all();
  return published_generation;
}

bool SubagentDeliveryManager::parent_needed(std::string_view session_id) const
{
  auto jobs = coordinator_->list(session_id);
  if (std::ranges::any_of(jobs, [](auto const& snapshot) { return execution_live(snapshot.job.execution); }))
    return true;
  auto pending = coordinator_->pending_deliveries(session_id);
  return pending && !pending->empty();
}

ava::core::VoidResult SubagentDeliveryManager::refresh_parent_configuration(runtime::session_ts const& unlocked_session)
{
  AVA_ASSERT_NO_SESSION_LOCK_HELD("calling SubagentDeliveryManager::refresh_parent_configuration");

  CRITICAL_AREA_BEGIN_CR(session);
  auto const session_id = session_r->store.session_id();
  CRITICAL_AREA_END_R(session);

  std::shared_ptr<ParentCapsule> retained;
  {
    std::lock_guard lock(mutex_);
    auto found = parents_.find(session_id);
    if (found == parents_.end())
      return {};
    retained = found->second;
  }

  CRITICAL_AREA_CONTINUE_R(session);
  auto authority = session_r->read_authority_1();
  if (!authority)
    return std::unexpected(std::move(authority.error()));
  ava::session::SessionLease lease;
  if (!session_r->sessionless())
  {
    auto duplicated = session_r->lease().duplicate();
    if (!duplicated)
      return std::unexpected(std::move(duplicated.error()));
    lease = std::move(*duplicated);
  }
  auto detached_state = session_r->create_detached_state(std::move(lease), *authority, nullptr);
  auto retained_options = retained->run_options;
  CRITICAL_AREA_END_R(session);

  {
    std::lock_guard lock(mutex_);
    auto found = parents_.find(session_id);
    if (!accepting_ || found == parents_.end() || found->second != retained)
    {
      clear_secret(retained_options.access_token);
      return {};
    }
    auto const generation = next_generation_++;
    if (next_generation_ == 0)
      next_generation_ = 1;
    found->second = std::make_shared<ParentCapsule>(std::move(detached_state), std::move(retained_options), generation);
  }
  changed_.notify_all();
  return {};
}

void SubagentDeliveryManager::release_parent_if_unused(std::string_view parent_session_id, CapsuleGeneration generation)
{
  // This function potentially destroys a ParentCapsule object, which owns a session_ts,
  // and we are not allowed to hold a session lock while destroying a Session instance.
  AVA_ASSERT_NO_SESSION_LOCK_HELD("calling SubagentDeliveryManager::release_parent_if_unused");

  auto capsule_active = [](std::shared_ptr<ParentCapsule> const& capsule) {
    SCOPED_CRITICAL_AREA_R(session_r, capsule->unlocked_session);
    return session_r->run_controller() && session_r->run_controller()->snapshot().active;
  };
  std::shared_ptr<ParentCapsule> candidate;
  {
    std::lock_guard lock(mutex_);
    auto found = parents_.find(std::string(parent_session_id));
    if (found == parents_.end() || found->second->generation != generation || capsule_active(found->second))
      return;
    candidate = found->second;
  }
  if (parent_needed(parent_session_id))
    return;
  {
    std::lock_guard lock(mutex_);
    auto found = parents_.find(std::string(parent_session_id));
    if (found == parents_.end() || found->second != candidate || found->second->generation != generation || capsule_active(found->second))
      return;
    parents_.erase(found);
    detached_parents_.erase(std::string(parent_session_id));
  }
}

void SubagentDeliveryManager::attach_parent(std::string_view parent_session_id)
{
  std::lock_guard lock(mutex_);
  detached_parents_.erase(std::string(parent_session_id));
}

void SubagentDeliveryManager::release_detached_parent(std::string_view parent_session_id)
{
  // Might call release_parent_if_unused.
  AVA_ASSERT_NO_SESSION_LOCK_HELD("calling SubagentDeliveryManager::release_detached_parent");

  std::optional<CapsuleGeneration> generation;
  {
    std::lock_guard lock(mutex_);
    if (auto found = parents_.find(std::string(parent_session_id)); found != parents_.end())
    {
      detached_parents_.insert(std::string(parent_session_id));
      generation = found->second->generation;
    }
  }
  if (generation)
    release_parent_if_unused(parent_session_id, *generation);
}

ava::core::Result<runtime::session_ts> SubagentDeliveryManager::retained_session(std::string_view session_id,
                                                                                  std::filesystem::path const& workspace_identity, bool& found,
                                                                                  bool exact_session_id)
{
  found = false;
  std::shared_ptr<ParentCapsule> capsule;
  auto const expected_workspace = normalized_workspace_identity(workspace_identity);
  bool saw_foreign_owner = false;
  {
    std::lock_guard lock(mutex_);
    if (!accepting_)
      return std::unexpected(retained_owner_not_found(session_id));
    auto retained = parents_.find(std::string(session_id));
    if (retained != parents_.end())
    {
      found = true;
      SCOPED_CRITICAL_AREA_R(session_r, retained->second->unlocked_session);
      if (normalized_workspace_identity(session_r->workspace_dir()) != expected_workspace)
        return std::unexpected(retained_owner_not_found(session_id));
      capsule = retained->second;
    }
    else if (!exact_session_id)
    {
      for (auto const& [retained_id, retained] : parents_)
      {
        if (!retained_id.starts_with(session_id))
          continue;
        SCOPED_CRITICAL_AREA_R(session_r, retained->unlocked_session);
        if (normalized_workspace_identity(session_r->workspace_dir()) != expected_workspace)
        {
          saw_foreign_owner = true;
          continue;
        }
        if (capsule)
        {
          auto error = ava::core::Error(ava::core::ErrorCategory::InvalidArgument, "session id prefix is ambiguous among retained parents");
          error.with_context("session_id", std::string(session_id));
          return std::unexpected(std::move(error));
        }
        capsule = retained;
        found = true;
      }
    }
    if (!capsule)
    {
      if (saw_foreign_owner)
      {
        found = true;
        return std::unexpected(retained_owner_not_found(session_id));
      }
      return std::unexpected(retained_owner_not_found(session_id));
    }
  }
  auto detached_state_result = [&]() -> ava::core::Result<runtime::Session_aggregate_base> {
    SCOPED_CRITICAL_AREA_R(session_r, capsule->unlocked_session);
    auto authority = session_r->read_authority_1();
    if (!authority)
      return std::unexpected(std::move(authority.error()));
    ava::session::SessionLease lease;
    if (!session_r->sessionless())
    {
      auto duplicated = session_r->lease().duplicate();
      if (!duplicated)
        return std::unexpected(std::move(duplicated.error()));
      lease = std::move(*duplicated);
    }
    return session_r->create_detached_state(std::move(lease), *authority, shared_from_this());
  }();
  if (!detached_state_result)
    return std::unexpected(std::move(detached_state_result.error()));
  detached_state_result->created = false;
  auto const attached_id = detached_state_result->store.session_id();

  attach_parent(attached_id);

  return ava::core::Result<runtime::session_ts>(std::in_place, std::move(*detached_state_result));
}

void SubagentDeliveryManager::enqueue(ava::agent::SubagentCoordinatorJobSnapshot const& snapshot) noexcept
{
  try
  {
    std::lock_guard lock(mutex_);
    if (!accepting_ || !delivery_pending(snapshot.job))
      return;
    auto duplicate = std::ranges::any_of(queue_, [&](auto const& queued) { return queued.job.identity.delivery_id == snapshot.job.identity.delivery_id; });
    if (!duplicate)
    {
      if (queue_.size() >= options_.max_queued_deliveries && parents_.contains(snapshot.job.identity.parent_session_id))
      {
        auto unavailable = std::ranges::find_if(queue_, [&](auto const& queued) { return !parents_.contains(queued.job.identity.parent_session_id); });
        if (unavailable != queue_.end())
          queue_.erase(unavailable);
      }
      if (queue_.size() < options_.max_queued_deliveries)
        queue_.push_back(snapshot);
    }
    changed_.notify_all();
  }
  catch (...)
  {
  }
}

void SubagentDeliveryManager::worker_loop(std::stop_token stop_token)
{
  while (!stop_token.stop_requested())
  {
    ava::agent::SubagentCoordinatorJobSnapshot snapshot;
    std::shared_ptr<ParentCapsule> capsule;
    {
      std::unique_lock lock(mutex_);
      changed_.wait(lock, stop_token, [&] {
        return !accepting_ || std::ranges::any_of(queue_, [&](auto const& queued) { return parents_.contains(queued.job.identity.parent_session_id); });
      });
      if (!accepting_ || stop_token.stop_requested())
        break;
      auto found = std::ranges::find_if(queue_, [&](auto const& queued) { return parents_.contains(queued.job.identity.parent_session_id); });
      if (found == queue_.end())
        continue;
      snapshot = *found;
      capsule = parents_.at(snapshot.job.identity.parent_session_id);
      queue_.erase(found);
    }
    deliver(std::move(snapshot), capsule, stop_token);

    // A full bounded queue may have omitted a notification. Once one slot is
    // consumed, rediscover durable pending work from retained parents rather
    // than relying on a lossy callback edge.
    std::vector<std::string> parents;
    {
      std::lock_guard lock(mutex_);
      parents.reserve(parents_.size());
      for (auto const& [parent, _] : parents_) parents.push_back(parent);
    }
    for (auto const& parent : parents)
    {
      auto pending = coordinator_->pending_deliveries(parent);
      if (!pending)
        continue;
      for (auto const& delivery : *pending) enqueue(delivery);
    }
  }
}

void SubagentDeliveryManager::deliver(ava::agent::SubagentCoordinatorJobSnapshot snapshot, std::shared_ptr<ParentCapsule> const& capsule,
                                      std::stop_token stop_token)
{
  // Might call release_parent_if_unused.
  AVA_ASSERT_NO_SESSION_LOCK_HELD("calling SubagentDeliveryManager::deliver");

  auto capsule_controller = [](std::shared_ptr<ParentCapsule> const& retained) {
    SCOPED_CRITICAL_AREA_R(session_r, retained->unlocked_session);
    return session_r->run_controller();
  };

  auto selected_capsule = capsule;
  auto current = coordinator_->snapshot(snapshot.job.identity.parent_session_id, snapshot.job.identity.job_id);
  if (current)
    snapshot = *current;
  if (!delivery_pending(snapshot.job))
    return;
  auto prompt = delivery_prompt(snapshot.job);
  auto const fingerprint = prompt_fingerprint(prompt);

  auto entries = [&]() -> ava::core::Result<ava::session::SessionReadAuthority> {
    SCOPED_CRITICAL_AREA_R(session_r, selected_capsule->unlocked_session);
    return session_r->read_authority_1();
  }();
  if (!entries)
    return;
  auto history = entries->load();
  if (!history)
    return;
  if (auto committed = committed_delivery_turn(*history, snapshot.job.identity.delivery_id,
                                               snapshot.job.delivery_attempt_history.empty()
                                                   ? std::string_view{}
                                                   : std::string_view(snapshot.job.delivery_attempt_history.back().prompt_fingerprint)))
  {
    if (!snapshot.job.delivery_attempt_history.empty())
    {
      auto acknowledged = coordinator_->acknowledge_delivery(snapshot.job.identity.parent_session_id, snapshot.job.identity.job_id,
                                                             snapshot.job.delivery_attempt_history.back().attempt_id, *committed);
      if (!acknowledged)
        enqueue(snapshot);
    }
    release_parent_if_unused(snapshot.job.identity.parent_session_id, selected_capsule->generation);
    return;
  }
  if (snapshot.job.delivery_attempts >= options_.max_delivery_attempts)
  {
    if (!snapshot.job.delivery_attempt_history.empty())
      static_cast<void>(coordinator_->exhaust_delivery(snapshot.job.identity.parent_session_id, snapshot.job.identity.job_id,
                                                       snapshot.job.delivery_attempt_history.back().attempt_id));
    release_parent_if_unused(snapshot.job.identity.parent_session_id, selected_capsule->generation);
    return;
  }

  auto const attempt_id = ava::core::make_id("delivery_attempt");
  auto const delivery_run_id = ava::core::make_id("automatic_delivery");
  auto const deadline = std::chrono::steady_clock::now() + options_.delivery_deadline;
  auto selected_controller = capsule_controller(selected_capsule);
  if (!selected_controller)
    return;

  // Do not consume an attempt or append the synthetic user message while an
  // ordinary parent turn is active. Once idle is observed, record the attempt
  // before admission; if a user races and wins, this same stable attempt waits
  // and retries admission without consuming another attempt.
  while (!stop_token.stop_requested() && std::chrono::steady_clock::now() < deadline)
  {
    auto admission = selected_controller->inspect_admission(RunRequest{.request_id = delivery_run_id});
    if (admission == AdmissionDisposition::Admit)
      break;
    if (admission == AdmissionDisposition::RejectClosing || admission == AdmissionDisposition::RejectPersistenceFailure)
      return;
    std::this_thread::sleep_for(options_.admission_retry_interval);
  }
  if (stop_token.stop_requested() || std::chrono::steady_clock::now() >= deadline)
  {
    if (!stop_token.stop_requested())
      enqueue(snapshot);
    return;
  }

  if (options_.admission_preflight)
    options_.admission_preflight(stop_token);
  if (stop_token.stop_requested() || std::chrono::steady_clock::now() >= deadline)
  {
    if (!stop_token.stop_requested())
      enqueue(snapshot);
    return;
  }

  {
    std::lock_guard lock(mutex_);
    auto refreshed = parents_.find(snapshot.job.identity.parent_session_id);
    if (refreshed == parents_.end())
      return;
    selected_capsule = refreshed->second;
  }
  selected_controller = capsule_controller(selected_capsule);
  if (!selected_controller)
    return;

  auto attempted = coordinator_->record_delivery_attempt(snapshot.job.identity.parent_session_id, snapshot.job.identity.job_id, attempt_id, fingerprint);
  if (!attempted)
    return;

  std::optional<ActiveRunGuard> guard;
  while (!stop_token.stop_requested() && std::chrono::steady_clock::now() < deadline)
  {
    auto admitted = selected_controller->admit(RunRequest{.request_id = delivery_run_id});
    if (admitted)
    {
      guard.emplace(std::move(*admitted));
      break;
    }
    auto admission = selected_controller->inspect_admission(RunRequest{.request_id = delivery_run_id});
    if (admission == AdmissionDisposition::RejectClosing || admission == AdmissionDisposition::RejectPersistenceFailure)
      return;
    std::this_thread::sleep_for(options_.admission_retry_interval);
  }
  if (!guard)
  {
    if (!stop_token.stop_requested())
      enqueue(*attempted);
    return;
  }

  std::shared_ptr<ParentCapsule> refreshed_capsule;
  {
    std::lock_guard lock(mutex_);
    auto refreshed = parents_.find(snapshot.job.identity.parent_session_id);
    if (refreshed == parents_.end())
      return;
    refreshed_capsule = refreshed->second;
  }
  if (capsule_controller(refreshed_capsule) != selected_controller)
    return;
  selected_capsule = std::move(refreshed_capsule);

  auto run_options = selected_capsule->run_options;
  run_options.request_id = delivery_run_id;
  run_options.event_sink = nullptr;
  run_options.question_resolver = nullptr;
  run_options.take_steering_messages = nullptr;
  run_options.session_mutex = nullptr;
  run_options.image_attachments.clear();
  run_options.synthetic_subagent_delivery = true;
  run_options.synthetic_user_message_provenance =
      ava::session::SyntheticDeliveryProvenance{.delivery_id = snapshot.job.identity.delivery_id, .prompt_fingerprint = fingerprint};
  // Automatic delivery always resolves credentials at execution time. A
  // retained token/account binding may have rotated while the parent was
  // inactive and must never be treated as current authority.
  clear_secret(run_options.access_token);
  run_options.credential_type = "bearer";
  run_options.openai_oauth = false;
  run_options.openai_account_id.clear();
  run_options.cancel_requested = [stop_token, deadline] { return stop_token.stop_requested() || std::chrono::steady_clock::now() >= deadline; };

  auto bundle = [&]() -> ava::core::Result<RuntimeProviderRunBundle> {
    run_options.permission_resolver = ava::permissions::build_persistent_permission_rule_resolver(
        runtime::session_ts::rat(selected_capsule->unlocked_session)->permission_rule_store(), build_headless_permission_resolver({}));
    return options_.provider_bundle_factory(selected_capsule->unlocked_session, std::move(run_options), "automatic subagent delivery");
  }();
  if (!bundle || !bundle->provider || !bundle->transport)
  {
    if (!stop_token.stop_requested())
      enqueue(*attempted);
    return;
  }
  BoundedDeliveryTransport bounded_transport(std::move(bundle->transport), stop_token, deadline);
  auto result = run_admitted_prompt(selected_capsule->unlocked_session, prompt, *bundle->provider, bounded_transport, bundle->options, std::move(*guard));
  if (!result || !result->committed_turn_id)
  {
    if (!stop_token.stop_requested())
      enqueue(*attempted);
    return;
  }
  auto acknowledged =
      coordinator_->acknowledge_delivery(snapshot.job.identity.parent_session_id, snapshot.job.identity.job_id, attempt_id, *result->committed_turn_id);
  if (!acknowledged)
    enqueue(*attempted);
  release_parent_if_unused(snapshot.job.identity.parent_session_id, selected_capsule->generation);
}

void SubagentDeliveryManager::shutdown() noexcept
{
  // Potentially joins a thread.
  AVA_ASSERT_NO_SESSION_LOCK_HELD("calling SubagentDeliveryManager::shutdown");

  try
  {
    std::vector<std::shared_ptr<SessionRunController>> controllers;
    {
      std::lock_guard lock(mutex_);
      if (!accepting_ && !worker_.joinable())
        return;
      accepting_ = false;
      queue_.clear();
      controllers.reserve(parents_.size());
      for (auto const& [_, capsule] : parents_)
      {
        SCOPED_CRITICAL_AREA_R(session_r, capsule->unlocked_session);
        if (session_r->run_controller())
          controllers.push_back(session_r->run_controller());
      }
    }
    // Stop callbacks may reenter application code. Never invoke them while the
    // manager mutex protects capsule/queue teardown.
    for (auto const& controller : controllers) static_cast<void>(controller->request_stop(StopReason::UserCanceled));
    coordinator_->set_terminal_sink(nullptr);
    changed_.notify_all();
    if (worker_.joinable())
    {
      worker_.request_stop();
      worker_.join();
    }
    coordinator_->shutdown();
    std::unordered_map<std::string, std::shared_ptr<ParentCapsule>> released;
    {
      std::lock_guard lock(mutex_);
      released.swap(parents_);
    }
  }
  catch (...)
  {
  }
}

}  // namespace ava::app
