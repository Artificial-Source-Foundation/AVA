#pragma once

#include <sys/types.h>

#include <chrono>
#include <filesystem>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "ava/core/result.h"

namespace ava::lsp {

using CancelCallback = std::function<bool()>;

struct Diagnostic {
  int severity = 0;
  std::string message;
  int line = 0;
  int column = 0;
  std::string code;
};

struct ServerConfig {
  std::vector<std::string> argv;
  std::filesystem::path workspace_root;
  std::chrono::milliseconds request_timeout{3000};
};

class DiagnosticsProvider {
 public:
  DiagnosticsProvider() = default;
  DiagnosticsProvider(const DiagnosticsProvider&) = delete;
  DiagnosticsProvider& operator=(const DiagnosticsProvider&) = delete;
  DiagnosticsProvider(DiagnosticsProvider&&) = delete;
  DiagnosticsProvider& operator=(DiagnosticsProvider&&) = delete;
  virtual ~DiagnosticsProvider() = default;

  [[nodiscard]] virtual ava::core::Result<std::vector<Diagnostic>> diagnostics(
      const std::filesystem::path& path, CancelCallback cancel_requested = nullptr) = 0;
};

class SubprocessLspClient final : public DiagnosticsProvider {
 public:
  explicit SubprocessLspClient(ServerConfig config);
  ~SubprocessLspClient() override;

  SubprocessLspClient(const SubprocessLspClient&) = delete;
  SubprocessLspClient& operator=(const SubprocessLspClient&) = delete;
  SubprocessLspClient(SubprocessLspClient&&) = delete;
  SubprocessLspClient& operator=(SubprocessLspClient&&) = delete;

  [[nodiscard]] static ava::core::Result<std::shared_ptr<SubprocessLspClient>> start(
      ServerConfig config, CancelCallback cancel_requested = nullptr);
  [[nodiscard]] ava::core::Result<std::vector<Diagnostic>> diagnostics(
      const std::filesystem::path& path, CancelCallback cancel_requested = nullptr) override;

 private:
  [[nodiscard]] ava::core::VoidResult launch();
  [[nodiscard]] ava::core::VoidResult initialize(CancelCallback cancel_requested = nullptr);
  [[nodiscard]] ava::core::VoidResult send_notification(std::string_view method, std::string_view params_json,
                                                        CancelCallback cancel_requested = nullptr);
  [[nodiscard]] ava::core::Result<std::string> request_response(std::string_view method, std::string_view params_json,
                                                                CancelCallback cancel_requested = nullptr);
  [[nodiscard]] ava::core::VoidResult write_message(std::string_view body, CancelCallback cancel_requested = nullptr);
  [[nodiscard]] ava::core::Result<std::string> read_message(std::chrono::steady_clock::time_point deadline,
                                                            CancelCallback cancel_requested = nullptr);
  [[nodiscard]] ava::core::VoidResult wait_for_readable(std::chrono::steady_clock::time_point deadline,
                                                        CancelCallback cancel_requested = nullptr);
  [[nodiscard]] ava::core::VoidResult wait_for_writable(std::chrono::steady_clock::time_point deadline,
                                                        CancelCallback cancel_requested = nullptr);
  [[nodiscard]] ava::core::VoidResult check_child_running();
  void close_fds() noexcept;
  void terminate_child() noexcept;

  ServerConfig config_;
  pid_t pid_ = -1;
  bool can_signal_group_ = false;
  int stdin_fd_ = -1;
  int stdout_fd_ = -1;
  int next_id_ = 1;
  std::string read_buffer_;
};

}  // namespace ava::lsp
