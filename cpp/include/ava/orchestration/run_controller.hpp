#pragma once

#include <atomic>
#include <chrono>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>

namespace ava::orchestration {

class RunCancellationToken {
 public:
  RunCancellationToken() = default;
  RunCancellationToken(
      std::shared_ptr<std::atomic<bool>> cancelled_flag,
      std::shared_ptr<const std::optional<std::chrono::steady_clock::time_point>> deadline = nullptr
  )
      : cancelled_flag_(std::move(cancelled_flag)), deadline_(std::move(deadline)) {}

  [[nodiscard]] bool is_cancelled() const;
  [[nodiscard]] bool is_deadline_expired() const;

 private:
  std::shared_ptr<std::atomic<bool>> cancelled_flag_;
  std::shared_ptr<const std::optional<std::chrono::steady_clock::time_point>> deadline_;
};

class RunCancellationHandle {
 public:
  RunCancellationHandle() = default;
  RunCancellationHandle(
      std::shared_ptr<std::atomic<bool>> cancelled_flag,
      std::shared_ptr<const std::optional<std::chrono::steady_clock::time_point>> deadline = nullptr
  )
      : cancelled_flag_(std::move(cancelled_flag)), deadline_(std::move(deadline)) {}

  void cancel() const;
  [[nodiscard]] bool is_cancelled() const;
  [[nodiscard]] bool is_deadline_expired() const;
  [[nodiscard]] explicit operator bool() const { return static_cast<bool>(cancelled_flag_); }

 private:
  std::shared_ptr<std::atomic<bool>> cancelled_flag_;
  std::shared_ptr<const std::optional<std::chrono::steady_clock::time_point>> deadline_;
};

struct RunLease {
  std::string run_id;
  RunCancellationToken token;
  RunCancellationHandle handle;
};

class RunController {
 public:
  explicit RunController(std::string session_id);

  [[nodiscard]] RunLease begin_run(std::optional<std::chrono::steady_clock::time_point> deadline = std::nullopt);

 private:
  std::string session_id_;
  std::atomic<std::uint64_t> next_run_counter_{1};
};

}  // namespace ava::orchestration
