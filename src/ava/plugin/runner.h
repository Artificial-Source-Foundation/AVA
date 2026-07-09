#pragma once

#include "ava/plugin/manifest.h"

#include "ava/core/result.h"

#include <chrono>
#include <cstddef>
#include <filesystem>
#include <functional>
#include <memory>
#include <string>
#include <string_view>

namespace ava::plugin {

using CancelCallback = std::function<bool()>;

struct PluginRunnerOptions {
  std::filesystem::path workspace_dir;
  std::chrono::milliseconds startup_timeout{3000};
  std::chrono::milliseconds request_timeout{5000};
  std::size_t max_record_bytes = 64 * 1024;
  std::size_t max_stderr_bytes = 64 * 1024;

  AVA_DEBUG_PRINT_MEMBERS_ON
};

struct PluginInitialization {
  std::string api_version;
  std::string plugin_version;
  std::string contributions_json;
  std::string raw_json;

  AVA_DEBUG_PRINT_MEMBERS_ON
};

struct PluginToolCallResult {
  bool ok = false;
  std::string content;
  std::string metadata_json;
  std::string raw_json;

  AVA_DEBUG_PRINT_MEMBERS_ON
};

struct PluginCommandCallResult {
  bool ok = false;
  std::string content;
  std::string metadata_json;
  std::string raw_json;

  AVA_DEBUG_PRINT_MEMBERS_ON
};

struct PluginEventObserveResult {
  bool ok = false;
  std::string content;
  std::string metadata_json;
  std::string raw_json;

  AVA_DEBUG_PRINT_MEMBERS_ON
};

struct PluginProxyRequest {
  std::string id;
  std::string operation;
  std::string arguments_json;
  std::string raw_json;

  AVA_DEBUG_PRINT_MEMBERS_ON
};

struct PluginProxyResponse {
  bool ok = false;
  std::string content;
  std::string metadata_json;
  std::string error_category;
  std::string error_message;
  std::string error_details;

  AVA_DEBUG_PRINT_MEMBERS_ON
};

using PluginProxyHandler =
    std::function<ava::core::Result<PluginProxyResponse>(PluginProxyRequest const&, CancelCallback)>;

class PluginProcess final {
 public:
  PluginProcess(PluginManifest manifest, PluginRunnerOptions options);
  ~PluginProcess();

  PluginProcess(PluginProcess const&) = delete;
  PluginProcess& operator=(PluginProcess const&) = delete;
  PluginProcess(PluginProcess&&) = delete;
  PluginProcess& operator=(PluginProcess&&) = delete;

  [[nodiscard]] static ava::core::Result<std::unique_ptr<PluginProcess>> start(
      PluginManifest manifest, PluginRunnerOptions options, CancelCallback cancel_requested = nullptr);

  [[nodiscard]] PluginManifest const& manifest() const noexcept;
  [[nodiscard]] PluginInitialization const& initialization() const noexcept;
  [[nodiscard]] std::string const& stderr_tail() const noexcept;
  [[nodiscard]] bool stderr_truncated() const noexcept;

  [[nodiscard]] ava::core::Result<PluginToolCallResult> call_tool(std::string_view tool_name,
                                                                  std::string_view arguments_json,
                                                                  std::string_view call_id = {},
                                                                  CancelCallback cancel_requested = nullptr,
                                                                  PluginProxyHandler proxy_handler = nullptr);
  [[nodiscard]] ava::core::Result<PluginCommandCallResult> call_command(std::string_view command_name,
                                                                        std::string_view arguments_json,
                                                                        std::string_view call_id = {},
                                                                        CancelCallback cancel_requested = nullptr,
                                                                        PluginProxyHandler proxy_handler = nullptr);
  [[nodiscard]] ava::core::Result<PluginEventObserveResult> observe_event(std::string_view event_name,
                                                                          std::string_view payload_json,
                                                                          std::string_view call_id = {},
                                                                          CancelCallback cancel_requested = nullptr,
                                                                          PluginProxyHandler proxy_handler = nullptr);
  [[nodiscard]] ava::core::VoidResult shutdown(std::chrono::milliseconds grace = std::chrono::milliseconds(250));

  AVA_DEBUG_PRINT_MEMBERS_ON

 private:
  [[nodiscard]] ava::core::VoidResult launch();
  [[nodiscard]] ava::core::VoidResult initialize(CancelCallback cancel_requested = nullptr);
  [[nodiscard]] ava::core::VoidResult write_record(std::string_view record,
                                                   std::chrono::steady_clock::time_point deadline,
                                                   std::chrono::milliseconds timeout, std::string_view timeout_message,
                                                   CancelCallback cancel_requested = nullptr);
  [[nodiscard]] ava::core::Result<std::string> read_record(std::chrono::steady_clock::time_point deadline,
                                                           std::chrono::milliseconds timeout,
                                                           std::string_view timeout_message,
                                                           std::string_view closed_message,
                                                           CancelCallback cancel_requested = nullptr);
  [[nodiscard]] ava::core::VoidResult wait_for_writable(std::chrono::steady_clock::time_point deadline,
                                                        std::chrono::milliseconds timeout,
                                                        std::string_view timeout_message,
                                                        CancelCallback cancel_requested = nullptr);
  [[nodiscard]] ava::core::Result<bool> handle_proxy_record(std::string_view record,
                                                            std::chrono::steady_clock::time_point deadline,
                                                            std::chrono::milliseconds timeout,
                                                            PluginProxyHandler const& proxy_handler,
                                                            CancelCallback cancel_requested = nullptr);
  [[nodiscard]] ava::core::Result<PluginProxyResponse> dispatch_proxy_request(
      PluginProxyRequest const& request, PluginProxyHandler const& proxy_handler,
      std::chrono::steady_clock::time_point deadline, CancelCallback cancel_requested = nullptr);
  [[nodiscard]] ava::core::VoidResult write_proxy_response(std::string_view request_id,
                                                           PluginProxyResponse const& response,
                                                           std::chrono::steady_clock::time_point deadline,
                                                           std::chrono::milliseconds timeout,
                                                           CancelCallback cancel_requested = nullptr);
  [[nodiscard]] ava::core::VoidResult drain_stdout();
  [[nodiscard]] ava::core::VoidResult drain_stderr();
  [[nodiscard]] ava::core::VoidResult reap_child();
  [[nodiscard]] ava::core::VoidResult set_pipe_nonblocking(int fd, std::string_view pipe_name);
  void append_stderr(std::string_view chunk);
  void close_fds() noexcept;
  void terminate_child() noexcept;
  void drain_available_noexcept() noexcept;

  PluginManifest manifest_;
  PluginRunnerOptions options_;
  PluginInitialization initialization_;
  int stdin_fd_ = -1;
  int stdout_fd_ = -1;
  int stderr_fd_ = -1;
  int pid_ = -1;
  int child_status_ = 0;
  bool child_exited_ = false;
  bool can_signal_group_ = false;
  bool stderr_truncated_ = false;
  std::size_t next_request_id_ = 2;
  std::string stdout_buffer_;
  std::string stderr_tail_;
};

}  // namespace ava::plugin
