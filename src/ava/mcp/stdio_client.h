#pragma once

#include "ava/mcp/config.h"
#include "ava/core/result.h"

#include <chrono>
#include <cstddef>
#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace ava::mcp {

using CancelCallback = std::function<bool()>;

struct McpStdioClientOptions
{
  std::filesystem::path workspace_dir;
  std::chrono::milliseconds startup_timeout{3000};
  std::chrono::milliseconds request_timeout{5000};
  std::size_t max_message_bytes = 64 * 1024;
  std::size_t max_stderr_bytes = 64 * 1024;
};

struct McpInitialization
{
  std::string server_name;
  std::string server_version;
  std::string capabilities_json;
  std::string raw_json;
};

struct McpToolDescription
{
  std::string name;
  std::string description;
  std::string input_schema_json;
};

struct McpPromptArgumentDescription
{
  std::string name;
  std::string description;
  bool required = false;
};

struct McpPromptDescription
{
  std::string name;
  std::string description;
  std::vector<McpPromptArgumentDescription> arguments;
};

struct McpToolCallResult
{
  bool is_error = false;
  std::string content;
  std::string raw_json;
};

struct McpPromptGetResult
{
  std::string content;
  std::string raw_json;
};

class McpStdioClient final
{
 public:
  McpStdioClient(McpServerConfig server, McpStdioClientOptions options);
  ~McpStdioClient();

  McpStdioClient(McpStdioClient const&) = delete;
  McpStdioClient& operator=(McpStdioClient const&) = delete;
  McpStdioClient(McpStdioClient&&) = delete;
  McpStdioClient& operator=(McpStdioClient&&) = delete;

  [[nodiscard]] static ava::core::Result<std::unique_ptr<McpStdioClient>> start(McpServerConfig server, McpStdioClientOptions options,
                                                                                CancelCallback cancel_requested = nullptr);

  [[nodiscard]] McpServerConfig const& server() const noexcept;
  [[nodiscard]] McpInitialization const& initialization() const noexcept;
  [[nodiscard]] std::string const& stderr_tail() const noexcept;
  [[nodiscard]] bool stderr_truncated() const noexcept;

  [[nodiscard]] ava::core::Result<std::vector<McpToolDescription>> list_tools(CancelCallback cancel_requested = nullptr);
  [[nodiscard]] ava::core::Result<McpToolCallResult> call_tool(std::string_view tool_name, std::string_view arguments_json,
                                                               CancelCallback cancel_requested = nullptr);
  [[nodiscard]] ava::core::Result<std::vector<McpPromptDescription>> list_prompts(CancelCallback cancel_requested = nullptr);
  [[nodiscard]] ava::core::Result<McpPromptGetResult> get_prompt(std::string_view prompt_name, std::string_view arguments_json,
                                                                 CancelCallback cancel_requested = nullptr);
  [[nodiscard]] ava::core::VoidResult shutdown(std::chrono::milliseconds grace = std::chrono::milliseconds(250));

 private:
  struct JsonRpcResponse
  {
    std::string result_json;
    std::string raw_json;
  };

  [[nodiscard]] ava::core::VoidResult launch();
  [[nodiscard]] ava::core::VoidResult initialize(CancelCallback cancel_requested = nullptr);
  [[nodiscard]] ava::core::Result<JsonRpcResponse> request(std::string_view method, std::string_view params_json, std::chrono::milliseconds timeout,
                                                           std::string_view timeout_message, CancelCallback cancel_requested = nullptr);
  [[nodiscard]] ava::core::VoidResult write_message(std::string_view message, std::chrono::steady_clock::time_point deadline, std::chrono::milliseconds timeout,
                                                    std::string_view timeout_message, CancelCallback cancel_requested = nullptr);
  [[nodiscard]] ava::core::Result<std::string> read_message(std::chrono::steady_clock::time_point deadline, std::chrono::milliseconds timeout,
                                                            std::string_view timeout_message, std::string_view closed_message,
                                                            CancelCallback cancel_requested = nullptr);
  [[nodiscard]] ava::core::Result<std::optional<std::string>> try_extract_message();
  [[nodiscard]] ava::core::VoidResult wait_for_writable(std::chrono::steady_clock::time_point deadline, std::chrono::milliseconds timeout,
                                                        std::string_view timeout_message, CancelCallback cancel_requested = nullptr);
  [[nodiscard]] ava::core::VoidResult drain_stdout();
  [[nodiscard]] ava::core::VoidResult drain_stderr();
  [[nodiscard]] ava::core::VoidResult reap_child();
  [[nodiscard]] ava::core::VoidResult set_pipe_nonblocking(int fd, std::string_view pipe_name);
  void append_stderr(std::string_view chunk);
  void close_fds() noexcept;
  void terminate_child() noexcept;

  McpServerConfig server_;
  McpStdioClientOptions options_;
  McpInitialization initialization_;
  int stdin_fd_ = -1;
  int stdout_fd_ = -1;
  int stderr_fd_ = -1;
  int pid_ = -1;
  int child_status_ = 0;
  bool child_exited_ = false;
  bool can_signal_group_ = false;
  bool stderr_truncated_ = false;
  std::size_t next_request_id_ = 1;
  std::string stdout_buffer_;
  std::string stderr_tail_;
};

}  // namespace ava::mcp
