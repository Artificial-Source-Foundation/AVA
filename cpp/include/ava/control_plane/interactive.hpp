#pragma once

#include <cstdint>
#include <cstddef>
#include <deque>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace ava::control_plane {

enum class InteractiveRequestKind {
  Approval,
  Question,
  Plan,
};

enum class InteractiveRequestState {
  Pending,
  Resolved,
  Cancelled,
  TimedOut,
};

struct InteractiveRequestHandle {
  std::string request_id;
  InteractiveRequestKind kind{InteractiveRequestKind::Approval};
  InteractiveRequestState state{InteractiveRequestState::Pending};
  std::optional<std::string> run_id;
};

[[nodiscard]] std::string_view interactive_request_kind_to_string(InteractiveRequestKind kind);
[[nodiscard]] std::string_view interactive_request_state_to_string(InteractiveRequestState state);

class InteractiveRequestStore {
 public:
  explicit InteractiveRequestStore(InteractiveRequestKind kind);

  [[nodiscard]] InteractiveRequestKind kind() const { return kind_; }

  [[nodiscard]] InteractiveRequestHandle register_request(std::optional<std::string> run_id = std::nullopt);
  [[nodiscard]] std::optional<InteractiveRequestHandle> resolve(const std::string& request_id);
  [[nodiscard]] std::optional<InteractiveRequestHandle> cancel(const std::string& request_id);
  [[nodiscard]] std::optional<InteractiveRequestHandle> timeout(const std::string& request_id);

  [[nodiscard]] std::optional<InteractiveRequestHandle> current_pending() const;
  [[nodiscard]] std::vector<InteractiveRequestHandle> pending_requests() const;
  [[nodiscard]] std::optional<InteractiveRequestHandle> request_by_id(const std::string& request_id) const;

 private:
  [[nodiscard]] std::string next_request_id();
  [[nodiscard]] std::optional<InteractiveRequestHandle> transition(
      const std::string& request_id,
      InteractiveRequestState terminal_state
  );

  InteractiveRequestKind kind_;
  std::uint64_t next_request_counter_{1};
  static constexpr std::size_t kMaxTerminalRetention = 64;
  mutable std::mutex mutex_;
  std::deque<std::string> pending_order_;
  std::deque<std::string> terminal_order_;
  std::unordered_map<std::string, InteractiveRequestHandle> pending_by_id_;
  std::unordered_map<std::string, InteractiveRequestHandle> terminal_by_id_;
};

}  // namespace ava::control_plane
