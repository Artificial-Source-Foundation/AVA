#pragma once

#include <atomic>
#include <cstdint>
#include <memory>
#include <string>

namespace ava::orchestration {

class RunCancellationToken {
 public:
  RunCancellationToken() = default;
  explicit RunCancellationToken(std::shared_ptr<std::atomic<bool>> cancelled_flag)
      : cancelled_flag_(std::move(cancelled_flag)) {}

  [[nodiscard]] bool is_cancelled() const;

 private:
  std::shared_ptr<std::atomic<bool>> cancelled_flag_;
};

class RunCancellationHandle {
 public:
  RunCancellationHandle() = default;
  explicit RunCancellationHandle(std::shared_ptr<std::atomic<bool>> cancelled_flag)
      : cancelled_flag_(std::move(cancelled_flag)) {}

  void cancel() const;
  [[nodiscard]] bool is_cancelled() const;
  [[nodiscard]] explicit operator bool() const { return static_cast<bool>(cancelled_flag_); }

 private:
  std::shared_ptr<std::atomic<bool>> cancelled_flag_;
};

struct RunLease {
  std::string run_id;
  RunCancellationToken token;
  RunCancellationHandle handle;
};

class RunController {
 public:
  explicit RunController(std::string session_id);

  [[nodiscard]] RunLease begin_run();

 private:
  std::string session_id_;
  std::atomic<std::uint64_t> next_run_counter_{1};
};

}  // namespace ava::orchestration
