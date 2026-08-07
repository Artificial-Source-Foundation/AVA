#pragma once

#include "ava/app/runtime/RunOptions.h"
#include "ava/app/runtime_credentials.h"
#include "ava/agent/subagent_coordinator.h"
#include "ava/core/result.h"
#include "ava/core/thread.h"

#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <filesystem>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <stop_token>
#include <string>
#include <thread>
#include <unordered_map>
#include <unordered_set>

namespace ava::app {
namespace runtime {
class Session;
} // namespace runtime

struct SubagentDeliveryManagerOptions
{
  std::shared_ptr<ava::agent::SubagentCoordinator> coordinator;
  RuntimeProviderRunBundleFactory provider_bundle_factory = nullptr;
  std::size_t max_queued_deliveries = 64;
  std::size_t max_retained_parents = 64;
  std::size_t max_delivery_attempts = 3;
  std::chrono::milliseconds delivery_deadline = std::chrono::seconds(30);
  std::chrono::milliseconds admission_retry_interval = std::chrono::milliseconds(10);
  // Optional application-owned hook invoked without manager, session, or
  // controller locks after an idle controller is observed but before delivery
  // attempt recording or admission. It must unblock when stop is requested.
  std::function<void(std::stop_token)> admission_preflight = nullptr;

  AVA_DEBUG_PRINT_MEMBERS_OPT_OUT
};

// Application-owned automatic summary delivery. It retains only detached,
// callback-free parent runtime capsules and never references frontend/RPC
// state. The coordinator remains protocol-neutral and emits process-local
// terminal notifications into this manager's bounded advisory queue.
class SubagentDeliveryManager final : public std::enable_shared_from_this<SubagentDeliveryManager>
{
 public:
  [[nodiscard]] static ava::core::Result<std::shared_ptr<SubagentDeliveryManager>> create(SubagentDeliveryManagerOptions options);
  ~SubagentDeliveryManager();

  SubagentDeliveryManager(SubagentDeliveryManager const&) = delete;
  SubagentDeliveryManager& operator=(SubagentDeliveryManager const&) = delete;

  [[nodiscard]] std::shared_ptr<ava::agent::SubagentCoordinator> const& coordinator() const noexcept;

  // Refreshes the exact parent capsule before each ordinary user prompt.
  // Callback-bearing fields are intentionally not copied.
  using CapsuleGeneration = std::uint64_t;

  [[nodiscard]] ava::core::Result<CapsuleGeneration> refresh_parent(runtime::session_ts const& unlocked_session, runtime::RunOptions const& options);
  // Refreshes configuration-only runtime state while preserving the current
  // detached callback-free policy snapshot. Missing capsules are a no-op.
  // Called from Session::refresh_parent_configuration that passes *this: session is already locked.
  [[nodiscard]] ava::core::VoidResult refresh_parent_configuration_1(runtime::Session const& session);
  void release_parent_if_unused(std::string_view parent_session_id, CapsuleGeneration generation);
  // Marks an explicit runtime attachment, preventing stale detach state from
  // releasing this visible parent's retained capsule.
  void attach_parent(std::string_view parent_session_id);
  // Called only after application navigation detaches the visible parent.
  // Retains any capsule still needed for a live job or delivery.
  void release_detached_parent(std::string_view parent_session_id);

  // Navigation uses this before pathname acquisition, allowing an inactive
  // retained parent to share its exact controller/append/read authority.
  [[nodiscard]] ava::core::Result<std::optional<runtime::Session>> retained_session(std::string_view session_id,
                                                                                    std::filesystem::path const& workspace_identity,
                                                                                    bool exact_session_id = false);

  void shutdown() noexcept;

  AVA_DEBUG_PRINT_MEMBERS_OPT_OUT

 private:
  struct ParentCapsule;
  explicit SubagentDeliveryManager(SubagentDeliveryManagerOptions options);
  void start();
  void enqueue(ava::agent::SubagentCoordinatorJobSnapshot const& snapshot) noexcept;
  void worker_loop(std::stop_token stop_token);
  void deliver(ava::agent::SubagentCoordinatorJobSnapshot snapshot, std::shared_ptr<ParentCapsule> const& capsule, std::stop_token stop_token);
  [[nodiscard]] bool parent_needed(std::string_view session_id) const;

  SubagentDeliveryManagerOptions options_;
  std::shared_ptr<ava::agent::SubagentCoordinator> coordinator_;
  mutable std::mutex mutex_;
  std::condition_variable_any changed_;
  std::unordered_map<std::string, std::shared_ptr<ParentCapsule>> parents_;
  std::unordered_set<std::string> detached_parents_;
  std::deque<ava::agent::SubagentCoordinatorJobSnapshot> queue_;
  bool accepting_ = true;
  CapsuleGeneration next_generation_ = 1;
  ava::core::JoinThread worker_;
};

}  // namespace ava::app
