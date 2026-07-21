#pragma once

#include "ava/debug/print_members_on.h"
#include "ava/agent/agent_loop_session.h"
#include "ava/config/model_config.h"
#include "ava/config/session_title_config.h"
#include "ava/config/xdg_paths.h"
#include "ava/session/session_store.h"
#include "ava/provider/provider.h"
#include "ava/core/AnchorSet.h"
#include "ava/core/result.h"

#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <deque>
#include <functional>
#include <memory>
#include <mutex>
#include <stop_token>
#include <string>
#include <string_view>
#include <thread>
#include <unordered_set>
#include <vector>

namespace ava::app {
class SessionRunController;
namespace runtime {
struct RunOptions;
struct Session;
}  // namespace runtime

inline constexpr std::size_t kMaxSessionTitleSourceBytes = 4096;
inline constexpr std::size_t kMaxSessionTitleProviderOutputBytes = 16 * 1024;

struct SessionTitleGenerationRequest
{
  ava::config::XdgPaths paths;
  ava::config::ModelInfo active_model;
  ava::config::SessionTitleConfig config;
  std::shared_ptr<ava::core::AnchorSet> anchor_set;
  std::string source_text;
  std::string access_token;
  std::string credential_type = "bearer";
  std::string account_id;
  bool openai_oauth = false;
  bool offline = false;

  AVA_DEBUG_PRINT_MEMBERS_OPT_OUT
};

using SessionTitleGenerator =
    std::function<ava::core::Result<std::string>(SessionTitleGenerationRequest&, std::stop_token, std::chrono::steady_clock::time_point)>;
using SessionTitleTransportFactory = std::function<std::unique_ptr<ava::provider::Transport>()>;

struct SessionTitleCoordinatorOptions
{
  ava::config::SessionTitleConfig config;
  SessionTitleGenerator generator = nullptr;
  SessionTitleTransportFactory transport_factory = nullptr;
  std::size_t worker_count = 1;
  std::size_t max_queued = 32;
  std::chrono::milliseconds request_deadline = std::chrono::seconds(10);

  AVA_DEBUG_PRINT_MEMBERS_OPT_OUT
};

// Application-scoped owner for best-effort title generation. Work snapshots
// the exact copyable read/append authorities and shared anchors; it never
// retains a mutable runtime session or reacquires session state by pathname.
class SessionTitleCoordinator final
{
 public:
  [[nodiscard]] static ava::core::Result<std::shared_ptr<SessionTitleCoordinator>> create(SessionTitleCoordinatorOptions options);
  ~SessionTitleCoordinator();

  SessionTitleCoordinator(SessionTitleCoordinator const&) = delete;
  SessionTitleCoordinator& operator=(SessionTitleCoordinator const&) = delete;

  // Admission completion calls this only after a durable ordinary turn. The
  // deterministic fallback is appended synchronously; provider refinement is
  // best-effort and can never alter that turn's result.
  void schedule(runtime::Session const& session, std::string_view original_user_text, std::string_view committed_turn_id,
                runtime::RunOptions const& run_options) noexcept;
  [[nodiscard]] bool wait_until_idle(std::chrono::milliseconds timeout);
  void shutdown() noexcept;

  AVA_DEBUG_PRINT_MEMBERS_OPT_OUT

 private:
  struct Work
  {
    std::string session_id;
    std::string committed_turn_id;
    std::string fallback_title;
    ava::session::SessionReadAuthority read_authority;
    // Retain the controller owner while queued/running so its serialized route
    // remains valid across visible-session navigation. No mutable Session is
    // retained, and coordinator shutdown still destroys the append authority.
    std::shared_ptr<SessionRunController> append_controller;
    ava::agent::SessionAppendSink append_route;
    SessionTitleGenerationRequest request;

    AVA_DEBUG_PRINT_MEMBERS_OPT_OUT
  };

  explicit SessionTitleCoordinator(SessionTitleCoordinatorOptions options);
  void start();
  void worker_loop(std::stop_token stop_token);
  void process(Work work, std::stop_token stop_token) noexcept;

  SessionTitleCoordinatorOptions options_;
  std::mutex mutex_;
  std::condition_variable_any changed_;
  std::deque<Work> queue_;
  std::unordered_set<std::string> active_session_ids_;
  std::unordered_set<std::string> admitted_session_ids_;
  bool accepting_ = true;
  std::vector<std::jthread> workers_;
};

[[nodiscard]] std::string normalize_session_title_source(std::string_view text);
[[nodiscard]] ava::core::Result<std::string> sanitize_generated_session_title(std::string_view text);
[[nodiscard]] std::string fallback_session_title(std::string_view normalized_source);

}  // namespace ava::app
