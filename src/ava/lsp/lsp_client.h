#pragma once

#include "ava/core/result.h"

#include <chrono>
#include <filesystem>
#include <functional>
#include <memory>
#include <string>
#include <unordered_set>
#include <vector>

#include <sys/types.h>

namespace ava::lsp {

using CancelCallback = std::function<bool()>;

struct Diagnostic {
  int severity = 0;
  std::string message;
  int line = 0;
  int column = 0;
  std::string code;
};

struct Range {
  int start_line = 0;
  int start_column = 0;
  int end_line = 0;
  int end_column = 0;
};

struct Symbol {
  std::string name;
  int kind = 0;
  std::filesystem::path path;
  Range range;
  std::string container;
};

struct Location {
  std::filesystem::path path;
  Range range;
};

struct ServerConfig {
  std::vector<std::string> argv;
  std::filesystem::path workspace_root;
  std::filesystem::path process_cwd;
  std::chrono::milliseconds request_timeout{3000};
  std::string language_id = "plaintext";
};

class DiagnosticsProvider {
 public:
  DiagnosticsProvider() = default;
  DiagnosticsProvider(DiagnosticsProvider const&) = delete;
  DiagnosticsProvider& operator=(DiagnosticsProvider const&) = delete;
  DiagnosticsProvider(DiagnosticsProvider&&) = delete;
  DiagnosticsProvider& operator=(DiagnosticsProvider&&) = delete;
  virtual ~DiagnosticsProvider() = default;

  [[nodiscard]] virtual ava::core::Result<std::vector<Diagnostic>> diagnostics(
      std::filesystem::path const& path, CancelCallback cancel_requested = nullptr) = 0;
  [[nodiscard]] virtual ava::core::Result<std::vector<Symbol>> document_symbols(
      std::filesystem::path const& path, CancelCallback cancel_requested = nullptr);
  [[nodiscard]] virtual ava::core::Result<std::vector<Symbol>> workspace_symbols(
      std::string_view query, CancelCallback cancel_requested = nullptr);
  [[nodiscard]] virtual ava::core::Result<std::vector<Location>> definitions(
      std::filesystem::path const& path, int line, int column, CancelCallback cancel_requested = nullptr);
  [[nodiscard]] virtual ava::core::Result<std::vector<Location>> references(
      std::filesystem::path const& path, int line, int column, CancelCallback cancel_requested = nullptr);
  virtual void set_permission_request_ids(std::shared_ptr<std::vector<std::string>> ids);
};

class SubprocessLspClient final : public DiagnosticsProvider {
 public:
  explicit SubprocessLspClient(ServerConfig config);
  ~SubprocessLspClient() override;

  SubprocessLspClient(SubprocessLspClient const&) = delete;
  SubprocessLspClient& operator=(SubprocessLspClient const&) = delete;
  SubprocessLspClient(SubprocessLspClient&&) = delete;
  SubprocessLspClient& operator=(SubprocessLspClient&&) = delete;

  [[nodiscard]] static ava::core::Result<std::shared_ptr<SubprocessLspClient>> start(
      ServerConfig config, CancelCallback cancel_requested = nullptr);
  [[nodiscard]] ava::core::Result<std::vector<Diagnostic>> diagnostics(
      std::filesystem::path const& path, CancelCallback cancel_requested = nullptr) override;
  [[nodiscard]] ava::core::Result<std::vector<Symbol>> document_symbols(
      std::filesystem::path const& path, CancelCallback cancel_requested = nullptr) override;
  [[nodiscard]] ava::core::Result<std::vector<Symbol>> workspace_symbols(
      std::string_view query, CancelCallback cancel_requested = nullptr) override;
  [[nodiscard]] ava::core::Result<std::vector<Location>> definitions(
      std::filesystem::path const& path, int line, int column, CancelCallback cancel_requested = nullptr) override;
  [[nodiscard]] ava::core::Result<std::vector<Location>> references(
      std::filesystem::path const& path, int line, int column, CancelCallback cancel_requested = nullptr) override;
  [[nodiscard]] bool is_alive();

 private:
  [[nodiscard]] ava::core::VoidResult launch();
  [[nodiscard]] ava::core::VoidResult initialize(CancelCallback cancel_requested = nullptr);
  [[nodiscard]] ava::core::VoidResult send_notification(std::string_view method, std::string_view params_json,
                                                        CancelCallback cancel_requested = nullptr);
  [[nodiscard]] ava::core::VoidResult send_did_open(std::filesystem::path const& path,
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
  std::unordered_set<std::string> open_documents_;
};

}  // namespace ava::lsp
