#include <sys/stat.h>
#include <termios.h>
#include <unistd.h>

#include <algorithm>
#include <chrono>
#include <climits>
#include <condition_variable>
#include <cstdlib>
#include <cwchar>
#include <deque>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <mutex>
#include <optional>
#include <sstream>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include "ava/agent/agent_loop.h"
#include "ava/agent/mode.h"
#include "ava/agent/tool_dispatcher.h"
#include "ava/app/command_catalog.h"
#include "ava/app/command_palette.h"
#include "ava/app/commands.h"
#include "ava/app/connect_openai.h"
#include "ava/app/events.h"
#include "ava/app/headless_policy.h"
#include "ava/app/print_mode.h"
#include "ava/app/reasoning_controls.h"
#include "ava/app/rpc/serialization.h"
#include "ava/app/rpc_mode.h"
#include "ava/app/runtime.h"
#include "ava/app/runtime_retry.h"
#include "ava/config/auth.h"
#include "ava/config/model_config.h"
#include "ava/config/openai_oauth.h"
#include "ava/config/prompt_config.h"
#include "ava/config/xdg_paths.h"
#include "ava/context/context_loader.h"
#include "ava/core/ids.h"
#include "ava/core/json.h"
#include "ava/permissions/permission.h"
#include "ava/provider/openai_provider.h"
#include "ava/session/compaction.h"
#include "ava/session/export.h"
#include "ava/session/session_store.h"
#include "ava/session/stats.h"
#include "ava/tools/bash_tool.h"
#include "ava/tools/file_tools.h"
#include "ava/tools/search_tools.h"
#include "ava/tui/composer.h"
#include "ava/tui/terminal.h"
#include "tests/support/fake_transport.h"
#include "tests/support/test_harness.h"

#ifndef AVA_FAKE_MCP_SERVER_PATH
#define AVA_FAKE_MCP_SERVER_PATH ""
#endif

namespace {

// ScopedStdinTerminalState snapshots the process stdin terminal attributes for
// tests that deliberately exercise interactive code with synthetic streams
// while still reporting `stdin_is_tty=true`.  It has no inputs beyond
// STDIN_FILENO, produces no values, and restores the saved attributes with
// TCSANOW when explicitly requested or when destroyed.  If the test is run
// without a real terminal, tcgetattr fails and the guard becomes a no-op.
class ScopedStdinTerminalState {
 public:
  ScopedStdinTerminalState() : active_(::tcgetattr(STDIN_FILENO, &original_) == 0) {}

  ScopedStdinTerminalState(const ScopedStdinTerminalState&) = delete;
  ScopedStdinTerminalState& operator=(const ScopedStdinTerminalState&) = delete;

  ~ScopedStdinTerminalState() { restore(); }

  void restore() noexcept {
    if (!active_) return;
    static_cast<void>(::tcsetattr(STDIN_FILENO, TCSANOW, &original_));
    active_ = false;
  }

 private:
  termios original_{};
  bool active_ = false;
};

ava::config::XdgPaths app_test_paths(const std::filesystem::path& root) {
  const auto config_home = root / "config";
  const auto state_home = root / "state";
  const auto data_home = root / "data";
  const auto ava_config = config_home / "ava";
  const auto ava_state = state_home / "ava";
  return ava::config::XdgPaths{.config_home = config_home,
                               .state_home = state_home,
                               .data_home = data_home,
                               .ava_config_dir = ava_config,
                               .ava_state_dir = ava_state,
                               .auth_file = ava_config / "auth.json",
                               .compaction_file = ava_config / "compaction.json",
                               .global_agents_file = ava_config / "AGENTS.md",
                               .models_file = ava_config / "models.json",
                               .prompts_dir = ava_config / "prompts",
                               .sessions_dir = ava_state / "sessions"};
}

std::string app_test_plugin_manifest_json(std::string_view id, std::string_view name = "Test Plugin") {
  return std::string("{\n") +
         "  \"schema_version\": 1,\n"
         "  \"id\": \"" +
         ava::core::json::escape(id) +
         "\",\n"
         "  \"name\": \"" +
         ava::core::json::escape(name) +
         "\",\n"
         "  \"version\": \"0.1.0\",\n"
         "  \"api_version\": \"ava.plugin.v1\",\n"
         "  \"description\": \"test plugin\",\n"
         "  \"entrypoint\": {\"command\": \"node\", \"args\": [\"plugin.js\", \"--safe\"]},\n"
         "  \"capabilities\": [\"tools\", \"commands\"],\n"
         "  \"contributes\": {\n"
         "    \"tools\": [{\"name\": \"todo_add\", \"description\": \"Add todo\", \"input_schema\": {\"type\": "
         "\"object\", \"additionalProperties\": false}}],\n"
         "    \"commands\": [{\"name\": \"todo\", \"description\": \"Show todos\"}]\n"
         "  }\n"
         "}";
}

std::string app_test_mcp_config_json(std::string_view id, std::string_view name, std::string_view command) {
  return std::string("{\"servers\":[{\"id\":\"") + ava::core::json::escape(id) + "\",\"name\":\"" +
         ava::core::json::escape(name) + "\",\"command\":\"" + ava::core::json::escape(command) +
         "\",\"enabled\":true}]}";
}

void write_app_test_file(const std::filesystem::path& path, const std::string& text) {
  std::filesystem::create_directories(path.parent_path());
  std::ofstream file(path, std::ios::binary | std::ios::trunc);
  file << text;
}

class BlockingInputBuf final : public std::streambuf {
 public:
  void push(std::string text) {
    {
      std::lock_guard lock(mutex_);
      for (const char ch : text) buffer_.push_back(ch);
    }
    cv_.notify_all();
  }

  void close() {
    {
      std::lock_guard lock(mutex_);
      closed_ = true;
    }
    cv_.notify_all();
  }

 protected:
  int underflow() override {
    std::unique_lock lock(mutex_);
    cv_.wait(lock, [&] { return closed_ || !buffer_.empty(); });
    if (buffer_.empty()) return traits_type::eof();
    current_ = buffer_.front();
    buffer_.pop_front();
    setg(&current_, &current_, &current_ + 1);
    return traits_type::to_int_type(current_);
  }

 private:
  std::mutex mutex_;
  std::condition_variable cv_;
  std::deque<char> buffer_;
  bool closed_ = false;
  char current_ = 0;
};

class ThreadSafeStringBuf final : public std::streambuf {
 public:
  std::string str() const {
    std::lock_guard lock(mutex_);
    return text_;
  }

  bool wait_contains(std::string_view value, std::chrono::milliseconds timeout) const {
    std::unique_lock lock(mutex_);
    return cv_.wait_for(lock, timeout, [&] { return text_.find(value) != std::string::npos; });
  }

 protected:
  int overflow(int ch) override {
    if (ch == traits_type::eof()) return traits_type::not_eof(ch);
    {
      std::lock_guard lock(mutex_);
      text_.push_back(static_cast<char>(ch));
    }
    cv_.notify_all();
    return ch;
  }

  std::streamsize xsputn(const char* s, std::streamsize count) override {
    {
      std::lock_guard lock(mutex_);
      text_.append(s, static_cast<std::size_t>(count));
    }
    cv_.notify_all();
    return count;
  }

 private:
  mutable std::mutex mutex_;
  mutable std::condition_variable cv_;
  std::string text_;
};

class ChunkedStreamingTransport final : public ava::provider::Transport {
 public:
  explicit ChunkedStreamingTransport(std::vector<std::string> chunks, int status_code = 200)
      : chunks_(std::move(chunks)), status_code_(status_code) {
    for (const auto& chunk : chunks_) response_body_ += chunk;
  }

  [[nodiscard]] ava::core::Result<ava::provider::HttpResponse> send(
      const ava::provider::HttpRequest& request) override {
    requests_.push_back(request);
    return ava::provider::HttpResponse{.status_code = status_code_, .headers = {}, .body = response_body_};
  }

  [[nodiscard]] bool supports_streaming() const noexcept override { return true; }

  [[nodiscard]] ava::core::Result<ava::provider::HttpResponse> send_streaming(
      const ava::provider::HttpRequest& request, BodyChunkSink on_body_chunk,
      CancelCallback cancel_requested = nullptr) override {
    requests_.push_back(request);
    for (const auto& chunk : chunks_) {
      if (cancel_requested && cancel_requested()) {
        return std::unexpected(ava::core::Error(ava::core::ErrorCategory::Unknown, "stream canceled"));
      }
      if (auto delivered = on_body_chunk(chunk); !delivered) return std::unexpected(std::move(delivered.error()));
    }
    return ava::provider::HttpResponse{.status_code = status_code_, .headers = {}, .body = response_body_};
  }

  [[nodiscard]] const std::vector<ava::provider::HttpRequest>& requests() const noexcept { return requests_; }

 private:
  std::vector<std::string> chunks_;
  int status_code_ = 200;
  std::string response_body_;
  std::vector<ava::provider::HttpRequest> requests_;
};

class BlockingResponseTransport final : public ava::provider::Transport {
 public:
  explicit BlockingResponseTransport(ava::provider::HttpResponse response) : response_(std::move(response)) {}

  [[nodiscard]] ava::core::Result<ava::provider::HttpResponse> send(
      const ava::provider::HttpRequest& request) override {
    {
      std::lock_guard lock(mutex_);
      requests_.push_back(request);
      requested_ = true;
    }
    cv_.notify_all();
    std::unique_lock lock(mutex_);
    cv_.wait(lock, [&] { return released_; });
    return response_;
  }

  bool wait_for_request(std::chrono::milliseconds timeout) const {
    std::unique_lock lock(mutex_);
    return cv_.wait_for(lock, timeout, [&] { return requested_; });
  }

  void release() {
    {
      std::lock_guard lock(mutex_);
      released_ = true;
    }
    cv_.notify_all();
  }

  [[nodiscard]] std::vector<ava::provider::HttpRequest> requests() const {
    std::lock_guard lock(mutex_);
    return requests_;
  }

 private:
  ava::provider::HttpResponse response_;
  mutable std::mutex mutex_;
  mutable std::condition_variable cv_;
  bool requested_ = false;
  bool released_ = false;
  std::vector<ava::provider::HttpRequest> requests_;
};

ava::provider::HttpResponse sse_response(std::string body) {
  return ava::provider::HttpResponse{.status_code = 200, .headers = {}, .body = std::move(body)};
}

std::string read_file_call_sse(std::string_view path = "note.txt") {
  return "data: {\"type\":\"response.function_call.added\",\"item_id\":\"call_read\",\"name\":\"read_file\"}\n\n"
         "data: "
         "{\"type\":\"response.function_call_arguments.delta\",\"item_id\":\"call_read\",\"delta\":\"{\\\"path\\\":"
         "\\\"" +
         ava::core::json::escape(path) +
         "\\\"}\"}\n\n"
         "data: [DONE]\n\n";
}

std::string write_file_call_sse(std::string_view path, std::string_view content) {
  const auto args =
      "{\"path\":\"" + ava::core::json::escape(path) + "\",\"content\":\"" + ava::core::json::escape(content) + "\"}";
  return "data: {\"type\":\"response.function_call.added\",\"item_id\":\"call_write\",\"name\":\"write_file\"}\n\n"
         "data: "
         "{\"type\":\"response.function_call_arguments.delta\",\"item_id\":\"call_write\",\"delta\":\"" +
         ava::core::json::escape(args) +
         "\"}\n\n"
         "data: [DONE]\n\n";
}

std::string question_call_sse() {
  return "data: {\"type\":\"response.function_call.added\",\"item_id\":\"call_question\",\"name\":\"question\"}\n\n"
         "data: "
         "{\"type\":\"response.function_call_arguments.delta\",\"item_id\":\"call_question\",\"delta\":\"{"
         "\\\"header\\\":\\\"Pick\\\",\\\"question\\\":\\\"Continue?\\\",\\\"options\\\":[{\\\"value\\\":\\\"yes\\\","
         "\\\"label\\\":\\\"Yes\\\"}],\\\"allow_custom\\\":true}\"}\n\n"
         "data: [DONE]\n\n";
}

std::string final_text_sse(std::string_view text) {
  return "data: {\"type\":\"response.output_text.delta\",\"delta\":\"" + ava::core::json::escape(text) +
         "\"}\n\n"
         "data: [DONE]\n\n";
}

std::string extract_json_string_field(std::string_view text, std::string_view key) {
  const std::string needle = "\"" + std::string(key) + "\":\"";
  const auto start = text.find(needle);
  if (start == std::string_view::npos) return "";
  const auto value_start = start + needle.size();
  const auto value_end = text.find('"', value_start);
  if (value_end == std::string_view::npos) return "";
  return std::string(text.substr(value_start, value_end - value_start));
}

std::string extract_last_json_string_field(std::string_view text, std::string_view key) {
  const std::string needle = "\"" + std::string(key) + "\":\"";
  const auto start = text.rfind(needle);
  if (start == std::string_view::npos) return "";
  const auto value_start = start + needle.size();
  const auto value_end = text.find('"', value_start);
  if (value_end == std::string_view::npos) return "";
  return std::string(text.substr(value_start, value_end - value_start));
}

std::size_t count_substrings(std::string_view text, std::string_view needle) {
  std::size_t count = 0;
  std::size_t position = 0;
  while ((position = text.find(needle, position)) != std::string_view::npos) {
    ++count;
    position += needle.size();
  }
  return count;
}

std::size_t count_compaction_entries(const std::vector<ava::session::SessionEntry>& entries) {
  return static_cast<std::size_t>(std::ranges::count_if(
      entries, [](const auto& entry) { return entry.type == ava::session::EntryType::Compaction; }));
}

std::optional<ava::session::SessionEntry> latest_compaction_entry(
    const std::vector<ava::session::SessionEntry>& entries) {
  for (auto iterator = entries.rbegin(); iterator != entries.rend(); ++iterator) {
    if (iterator->type == ava::session::EntryType::Compaction) return *iterator;
  }
  return std::nullopt;
}

class MutatingSummaryTransport final : public ava::provider::Transport {
 public:
  MutatingSummaryTransport(ava::session::SessionStore& store, std::vector<ava::provider::HttpResponse> responses,
                           std::size_t mutate_requests = 1)
      : store_(store), responses_(std::move(responses)), mutate_requests_(mutate_requests) {}

  ava::core::Result<ava::provider::HttpResponse> send(const ava::provider::HttpRequest& request) override {
    requests_.push_back(request);
    if (requests_.size() <= mutate_requests_) {
      static_cast<void>(
          store_.append(ava::session::SessionEntry{.id = "entry_concurrent_change_" + std::to_string(requests_.size()),
                                                   .parent_id = "",
                                                   .type = ava::session::EntryType::UserMessage,
                                                   .timestamp = ava::session::now_timestamp(),
                                                   .data_json = "{\"text\":\"concurrent change\"}"}));
    }
    if (responses_.empty()) {
      return std::unexpected(ava::core::Error(ava::core::ErrorCategory::Provider, "fake transport has no response"));
    }
    auto response = responses_.front();
    responses_.erase(responses_.begin());
    return response;
  }

  [[nodiscard]] const std::vector<ava::provider::HttpRequest>& requests() const noexcept { return requests_; }

 private:
  ava::session::SessionStore& store_;
  std::vector<ava::provider::HttpResponse> responses_;
  std::size_t mutate_requests_ = 1;
  std::vector<ava::provider::HttpRequest> requests_;
};

void test_command_classification() {
  expect(ava::permissions::classify_command("git status --short").action == ava::permissions::PermissionAction::Allow,
         "git status is allowed for non-TTY line shell inspection");
  expect(ava::permissions::classify_command("git diff").action == ava::permissions::PermissionAction::Allow,
         "git diff is allowed for non-TTY line shell inspection");
  expect(ava::permissions::classify_command("git log --oneline").action == ava::permissions::PermissionAction::Allow,
         "git log is allowed for non-TTY line shell inspection");
  expect(ava::permissions::classify_command("pwd").action == ava::permissions::PermissionAction::Allow,
         "pwd remains allowed as inert local inspection");
  expect(ava::permissions::classify_command("ls src").action == ava::permissions::PermissionAction::Allow,
         "ls remains allowed for safe relative paths");
  expect(ava::permissions::classify_command("rm -rf build").action == ava::permissions::PermissionAction::Deny,
         "rm -rf is denied");
  expect(ava::permissions::classify_command("git push origin main").action == ava::permissions::PermissionAction::Ask,
         "git push asks");
  expect(ava::permissions::classify_command("git diff --output=/tmp/ava-owned").action ==
             ava::permissions::PermissionAction::Ask,
         "git diff output paths are not auto-allowed");
  expect(ava::permissions::classify_command("git diff --output out.diff").action ==
             ava::permissions::PermissionAction::Ask,
         "git diff output option is not auto-allowed");
  expect(ava::permissions::classify_command("git diff --no-index empty .ssh/work_key").action ==
             ava::permissions::PermissionAction::Ask,
         "relative credential paths are not auto-allowed");
  expect(ava::permissions::classify_command("cmake --build build").action == ava::permissions::PermissionAction::Allow,
         "cmake build is allowed for non-TTY line shell verification");
  expect(
      ava::permissions::classify_command("ctest --test-dir build").action == ava::permissions::PermissionAction::Allow,
      "ctest is allowed for non-TTY line shell verification");
  expect(ava::permissions::classify_command("rg hello src").action == ava::permissions::PermissionAction::Allow,
         "rg is allowed for non-TTY line shell inspection");
  expect(ava::permissions::classify_command("rg --pre ./filter hello src").action ==
             ava::permissions::PermissionAction::Deny,
         "rg preprocessors remain denied because they execute commands");
  expect(
      ava::permissions::decide(ava::permissions::PermissionRequest{.operation = ava::permissions::Operation::RunCommand,
                                                                   .mode = ava::agent::Mode::Plan,
                                                                   .workspace_dir = std::filesystem::current_path(),
                                                                   .target_path = {},
                                                                   .command = "git status --short"})
              .action == ava::permissions::PermissionAction::Allow,
      "run-command decisions preserve safe command allows");
  expect(ava::permissions::classify_command("cmake -E cat ~/.config/ava/auth.json").action ==
             ava::permissions::PermissionAction::Deny,
         "cmake -E helper access is denied");
  expect(ava::permissions::classify_command("cmake -P docs/plan.md").action == ava::permissions::PermissionAction::Deny,
         "cmake -P script execution is denied");
  expect(ava::permissions::classify_command("cmake -E copy docs/plan.md src/new.cpp").action ==
             ava::permissions::PermissionAction::Deny,
         "cmake -E copy mutation is denied");
  expect(
      ava::permissions::classify_command("python3 scripts/run.py").action == ava::permissions::PermissionAction::Deny,
      "interpreters are denied");
  expect(ava::permissions::classify_command("bash -lc ls").action == ava::permissions::PermissionAction::Deny,
         "shell interpreters remain denied");
}

void test_app_event_serialization() {
  ava::app::RuntimeEvent session_event;
  session_event.type = ava::app::RuntimeEventType::SessionStart;
  session_event.timestamp = "2026-04-29T00:00:00Z";
  session_event.session_id = "session_1";
  session_event.mode = ava::agent::Mode::Plan;
  session_event.provider_id = "openai";
  session_event.model_id = "gpt-5.5";
  const auto jsonl = ava::app::serialize_event_jsonl(session_event);
  expect(jsonl ==
             "{\"type\":\"session_start\",\"timestamp\":\"2026-04-29T00:00:00Z\","
             "\"session_id\":\"session_1\",\"mode\":\"plan\",\"provider\":\"openai\","
             "\"model\":\"gpt-5.5\"}\n",
         "runtime event JSONL serialization is deterministic");

  ava::app::RuntimeEvent message_event;
  message_event.type = ava::app::RuntimeEventType::UserMessage;
  message_event.timestamp = "2026-04-29T00:00:01Z";
  message_event.session_id = "session_1";
  message_event.text = "hello\n\"ava\"";
  const auto message_jsonl = ava::app::serialize_event_jsonl(message_event);
  expect(message_jsonl.find("hello\\n\\\"ava\\\"") != std::string::npos, "runtime event JSONL escapes message text");
  expect(message_jsonl.ends_with('\n') &&
             message_jsonl.substr(0, message_jsonl.size() - 1).find('\n') == std::string::npos,
         "runtime event JSONL contains one terminating newline only");
}

void test_app_rpc_prompt_payload_serialization() {
  const auto permission_json = ava::app::rpc::permission_request_payload_json(
      "permission_1", ava::permissions::PermissionPrompt{.operation = ava::permissions::Operation::EditFile,
                                                         .mode = ava::agent::Mode::Build,
                                                         .workspace_dir = "/workspace",
                                                         .target_path = "/workspace/src/main.cpp",
                                                         .command = "",
                                                         .tool_name = "edit_file",
                                                         .reason = "needs approval",
                                                         .diff_preview = "--- a\n+++ b\n-old\n+new",
                                                         .diff_truncated = true});
  expect(permission_json.find("\"operation\":\"edit\"") != std::string::npos &&
             permission_json.find("\"target_path\":\"/workspace/src/main.cpp\"") != std::string::npos &&
             permission_json.find("\"diff_preview\":\"--- a\\n+++ b\\n-old\\n+new\"") != std::string::npos &&
             permission_json.find("\"diff_truncated\":true") != std::string::npos,
         "RPC permission request payload preserves semantic operation, target, reason, and diff preview data");

  const auto question_json = ava::app::rpc::question_request_payload_json(
      "question_1",
      ava::agent::QuestionPrompt{.header = "Choose",
                                 .question = "Pick providers",
                                 .options = {ava::agent::QuestionOption{.value = "openai", .label = "OpenAI"}},
                                 .multiple = true,
                                 .allow_custom = true,
                                 .secret = true,
                                 .modal = true,
                                 .searchable = true});
  expect(question_json.find("\"options\":[{\"value\":\"openai\",\"label\":\"OpenAI\"}]") != std::string::npos &&
             question_json.find("\"multiple\":true") != std::string::npos &&
             question_json.find("\"allow_custom\":true") != std::string::npos &&
             question_json.find("\"secret\":true") != std::string::npos &&
             question_json.find("\"modal\":true") != std::string::npos &&
             question_json.find("\"searchable\":true") != std::string::npos,
         "RPC question request payload preserves options, selection metadata, and local prompt flags");
}

void test_app_runtime_open_session_and_context_prompt() {
  const auto root = temp_root() / "app-runtime-open";
  std::error_code remove_error;
  std::filesystem::remove_all(root, remove_error);
  const auto workspace = root / "workspace";
  const auto current = workspace / "src";
  const auto paths = app_test_paths(root);
  std::filesystem::create_directories(current);
  std::filesystem::create_directories(paths.ava_config_dir);
  {
    std::ofstream file(workspace / "AGENTS.md", std::ios::binary | std::ios::trunc);
    file << "workspace runtime instructions\n";
  }
  {
    std::ofstream file(current / "AGENTS.md", std::ios::binary | std::ios::trunc);
    file << "nested runtime instructions\n";
  }
  {
    std::ofstream file(paths.global_agents_file, std::ios::binary | std::ios::trunc);
    file << "global runtime instructions\n";
  }

  ava::app::RuntimeOpenOptions open_options;
  open_options.workspace_dir = workspace;
  open_options.current_dir = current;
  open_options.mode = ava::agent::Mode::Plan;
  open_options.paths = paths;
  auto session = ava::app::open_runtime_session(open_options);
  expect(session.has_value(), "runtime session opens with selected model, prompt, and context");
  if (!session) return;

  expect(session->created && session->mode == ava::agent::Mode::Plan && session->model.model_id == "gpt-5.5",
         "runtime session records created state, mode, and model");
  expect(session->context_sources.size() == 3, "runtime session records workspace and global context metadata");
  expect(session->system_prompt.find("Plan before changing files") != std::string::npos &&
             session->system_prompt.find("workspace runtime instructions") != std::string::npos &&
             session->system_prompt.find("nested runtime instructions") != std::string::npos &&
             session->system_prompt.find("global runtime instructions") != std::string::npos,
         "runtime session system prompt combines selected prompt and formatted context");
  auto entries = session->store.load();
  expect(entries && entries->size() == 1 && (*entries)[0].type == ava::session::EntryType::SessionStart &&
             (*entries)[0].data_json.find("\"context_sources\":3") != std::string::npos,
         "runtime session appends session_start on creation");

  const auto session_id = session->store.session_id();
  ava::app::RuntimeOpenOptions reopen_options;
  reopen_options.workspace_dir = workspace;
  reopen_options.current_dir = current;
  reopen_options.requested_session_id = session_id.substr(0, 12);
  reopen_options.mode = ava::agent::Mode::Plan;
  reopen_options.paths = paths;
  auto reopened = ava::app::open_runtime_session(reopen_options);
  expect(reopened && !reopened->created && reopened->store.session_id() == session_id,
         "runtime session resolves requested session id prefixes without creating a new session");
  if (reopened) {
    auto reopened_entries = reopened->store.load();
    expect(reopened_entries && reopened_entries->size() == 1,
           "runtime reopened session does not append another session_start");
  }
}

void test_app_run_prompt_emits_events() {
  const auto root = temp_root() / "app-runtime-run";
  std::error_code remove_error;
  std::filesystem::remove_all(root, remove_error);
  const auto workspace = root / "workspace";
  const auto paths = app_test_paths(root);
  std::filesystem::create_directories(workspace);
  {
    std::ofstream file(workspace / "AGENTS.md", std::ios::binary | std::ios::trunc);
    file << "runtime run context\n";
  }

  ava::app::RuntimeOpenOptions open_options;
  open_options.workspace_dir = workspace;
  open_options.current_dir = workspace;
  open_options.mode = ava::agent::Mode::Build;
  open_options.paths = paths;
  auto session = ava::app::open_runtime_session(open_options);
  expect(session.has_value(), "runtime run test opens session");
  if (!session) return;

  const ava::provider::OpenAIProvider provider("https://api.example.test");
  ava::tests::FakeTransport transport({ava::provider::HttpResponse{
      .status_code = 200,
      .headers = {},
      .body = "data: {\"type\":\"response.output_text.delta\",\"delta\":\"runtime answer\"}\n\n"
              "data: [DONE]\n\n",
  }});
  std::vector<ava::app::RuntimeEvent> events;
  ava::app::RuntimeRunOptions run_options;
  run_options.access_token = "token";
  run_options.event_sink = [&events](const ava::app::RuntimeEvent& event) {
    events.push_back(event);
    return ava::core::VoidResult{};
  };
  auto result = ava::app::run_prompt(*session, "hello runtime", provider, transport, run_options);
  expect(result && result->final_text == "runtime answer", "runtime run_prompt returns agent loop result");
  expect(events.size() == 4 && events[0].type == ava::app::RuntimeEventType::SessionStart &&
             events[1].type == ava::app::RuntimeEventType::UserMessage &&
             events[2].type == ava::app::RuntimeEventType::AssistantMessage &&
             events[3].type == ava::app::RuntimeEventType::Done,
         "runtime run_prompt emits session, user, assistant, and done events");
  expect(events.size() == 4 && events[2].text == "runtime answer" && events[3].provider_iterations == 1,
         "runtime run_prompt events include final text and completion counters");
  expect(
      transport.requests().size() == 1 && transport.requests()[0].body.find("runtime run context") != std::string::npos,
      "runtime run_prompt sends context-augmented system prompt to provider");
  auto entries = session->store.load();
  expect(entries && entries->size() == 3 && (*entries)[1].type == ava::session::EntryType::UserMessage &&
             (*entries)[2].type == ava::session::EntryType::AssistantMessage,
         "runtime run_prompt persists user and assistant entries in the runtime session");
}

void test_app_run_prompt_emits_provider_retry_events_when_enabled() {
  const auto root = temp_root() / "app-runtime-provider-retry";
  std::error_code remove_error;
  std::filesystem::remove_all(root, remove_error);
  const auto workspace = root / "workspace";
  const auto paths = app_test_paths(root);
  std::filesystem::create_directories(workspace);

  ava::app::RuntimeOpenOptions open_options;
  open_options.workspace_dir = workspace;
  open_options.current_dir = workspace;
  open_options.mode = ava::agent::Mode::Build;
  open_options.paths = paths;
  auto session = ava::app::open_runtime_session(open_options);
  expect(session.has_value(), "runtime provider retry test opens session");
  if (!session) return;

  const ava::provider::OpenAIProvider provider("https://api.example.test");
  ava::tests::FakeTransport transport(
      {ava::provider::HttpResponse{
           .status_code = 429, .headers = {{"Retry-After", "0"}}, .body = "{\"error\":{\"message\":\"rate limited\"}}"},
       sse_response(final_text_sse("retried answer"))});
  std::vector<ava::app::RuntimeEvent> events;
  ava::app::RuntimeRunOptions run_options;
  run_options.access_token = "token";
  run_options.enable_transport_retries = true;
  run_options.event_sink = [&events](const ava::app::RuntimeEvent& event) {
    events.push_back(event);
    return ava::core::VoidResult{};
  };
  bool runtime_retry_cancel = false;
  run_options.cancel_requested = [&runtime_retry_cancel] { return runtime_retry_cancel; };

  auto result = ava::app::run_prompt(*session, "retry runtime", provider, transport, run_options);
  expect(result && result->final_text == "retried answer" && transport.requests().size() == 2,
         "runtime run_prompt retries transient provider transport failures when enabled");
  expect(std::ranges::any_of(events,
                             [](const ava::app::RuntimeEvent& event) {
                               return event.type == ava::app::RuntimeEventType::Retry &&
                                      event.trigger == "provider_transport" && event.reason == "rate_limited" &&
                                      event.attempt == 2 && event.max_attempts == 3 && event.delay_ms == 0 &&
                                      event.text == "HTTP status 429";
                             }),
         "runtime run_prompt emits provider retry metadata through the shared event sink");
  events.clear();
  auto retry_options = ava::app::runtime::runtime_retry_options(*session, run_options);
  expect(retry_options.on_retry != nullptr, "runtime retry options expose provider retry event mapping");
  runtime_retry_cancel = true;
  expect(retry_options.cancel_requested && retry_options.cancel_requested(),
         "runtime retry options preserve the active run cancellation callback");
  runtime_retry_cancel = false;
  if (retry_options.on_retry) {
    auto emitted_tick = retry_options.on_retry(ava::provider::RetryOptions::Event{.attempt = 2,
                                                                                  .max_attempts = 3,
                                                                                  .delay_ms = 1000,
                                                                                  .remaining_ms = 500,
                                                                                  .reason = "rate_limited",
                                                                                  .status_code = 429,
                                                                                  .streaming = true,
                                                                                  .countdown_tick = true});
    expect(emitted_tick.has_value() && events.size() == 1 && events[0].type == ava::app::RuntimeEventType::RetryTick &&
               events[0].trigger == "provider_transport" && events[0].remaining_ms == 500 &&
               events[0].delay_ms == 1000 && events[0].status == "streaming",
           "runtime retry options map provider countdown ticks to explicit backend retry_tick events");
  }
}

void test_app_run_prompt_emits_tool_progress_and_session_spill() {
  const auto root = temp_root() / "app-runtime-tool-progress";
  std::error_code remove_error;
  std::filesystem::remove_all(root, remove_error);
  const auto workspace = root / "workspace";
  const auto paths = app_test_paths(root);
  std::filesystem::create_directories(workspace);

  ava::app::RuntimeOpenOptions open_options;
  open_options.workspace_dir = workspace;
  open_options.current_dir = workspace;
  open_options.mode = ava::agent::Mode::Build;
  open_options.paths = paths;
  auto session = ava::app::open_runtime_session(open_options);
  expect(session.has_value(), "runtime tool progress test opens session");
  if (!session) return;

  const ava::provider::OpenAIProvider provider("https://api.example.test");
  ava::tests::FakeTransport transport({ava::provider::HttpResponse{
                                           .status_code = 200,
                                           .headers = {},
                                           .body = "data: {\"type\":\"response.function_call.added\",\"item_id\":"
                                                   "\"call_bash\",\"name\":\"bash\"}\n\n"
                                                   "data: {\"type\":\"response.function_call_arguments.delta\","
                                                   "\"item_id\":\"call_bash\",\"delta\":\"{\\\"command\\\":"
                                                   "\\\"pwd\\\",\\\"max_bytes\\\":4}\"}\n\n"
                                                   "data: [DONE]\n\n",
                                       },
                                       ava::provider::HttpResponse{
                                           .status_code = 200,
                                           .headers = {},
                                           .body = "data: {\"type\":\"response.output_text.delta\",\"delta\":"
                                                   "\"tool done\"}\n\n"
                                                   "data: [DONE]\n\n",
                                       }});
  std::vector<ava::app::RuntimeEvent> events;
  ava::app::RuntimeRunOptions run_options;
  run_options.access_token = "token";
  run_options.event_sink = [&events](const ava::app::RuntimeEvent& event) {
    events.push_back(event);
    return ava::core::VoidResult{};
  };

  auto result = ava::app::run_prompt(*session, "run pwd", provider, transport, run_options);
  const auto spill_dir = session->store.session_path().parent_path() / "spill";
  bool has_spill_file = false;
  std::error_code iter_error;
  for (std::filesystem::directory_iterator it(spill_dir, iter_error), end; !iter_error && it != end;
       it.increment(iter_error)) {
    has_spill_file = true;
    expect(it->path().parent_path() == spill_dir, "runtime spill file stays under the session-local spill directory");
    break;
  }
  expect(result && result->final_text == "tool done" &&
             std::ranges::any_of(events,
                                 [](const ava::app::RuntimeEvent& event) {
                                   return event.type == ava::app::RuntimeEventType::ToolProgress &&
                                          event.call_id == "call_bash" && event.tool_name == "bash" &&
                                          !event.text.empty();
                                 }),
         "runtime run_prompt emits additive tool_progress events from tool callbacks");
  expect(std::ranges::any_of(events,
                             [](const ava::app::RuntimeEvent& event) {
                               return event.type == ava::app::RuntimeEventType::ToolStart &&
                                      event.call_id == "call_bash" && event.tool_name == "bash" &&
                                      event.tool_arguments_json.find("\"command\":\"pwd\"") != std::string::npos;
                             }) &&
             std::ranges::any_of(events,
                                 [](const ava::app::RuntimeEvent& event) {
                                   return event.type == ava::app::RuntimeEventType::ToolResult &&
                                          event.call_id == "call_bash" && event.tool_name == "bash" &&
                                          event.truncated && event.total_bytes > 0 && !event.spill_path.empty() &&
                                          event.tool_result_json.find("\"spill_file\"") != std::string::npos;
                                 }),
         "runtime run_prompt emits semantic tool args, result, and spill metadata for frontend adapters");
  expect(has_spill_file, "runtime run_prompt configures session-local spill files for truncated tool output");
}

void test_app_run_prompt_event_sink_failure_cancels_before_next_provider_call() {
  const auto root = temp_root() / "app-runtime-event-sink-cancel";
  std::error_code remove_error;
  std::filesystem::remove_all(root, remove_error);
  const auto workspace = root / "workspace";
  const auto paths = app_test_paths(root);
  std::filesystem::create_directories(workspace);
  {
    std::ofstream file(workspace / "note.txt", std::ios::binary | std::ios::trunc);
    file << "event sink cancel data";
  }

  ava::app::RuntimeOpenOptions open_options;
  open_options.workspace_dir = workspace;
  open_options.current_dir = workspace;
  open_options.mode = ava::agent::Mode::Build;
  open_options.paths = paths;
  auto session = ava::app::open_runtime_session(open_options);
  expect(session.has_value(), "runtime event sink failure test opens session");
  if (!session) return;

  const ava::provider::OpenAIProvider provider("https://api.example.test");
  ava::tests::FakeTransport transport({ava::provider::HttpResponse{
                                           .status_code = 200,
                                           .headers = {},
                                           .body = "data: {\"type\":\"response.function_call.added\",\"item_id\":"
                                                   "\"call_read\",\"name\":\"read_file\"}\n\n"
                                                   "data: "
                                                   "{\"type\":\"response.function_call_arguments.delta\","
                                                   "\"item_id\":\"call_read\",\"delta\":\"{\\\"path\\\":"
                                                   "\\\"note.txt\\\"}\"}\n\n"
                                                   "data: [DONE]\n\n",
                                       },
                                       ava::provider::HttpResponse{
                                           .status_code = 200,
                                           .headers = {},
                                           .body = "data: {\"type\":\"response.output_text.delta\",\"delta\":"
                                                   "\"should not request\"}\n\n"
                                                   "data: [DONE]\n\n",
                                       }});
  ava::app::RuntimeRunOptions run_options;
  run_options.access_token = "token";
  run_options.event_sink = [](const ava::app::RuntimeEvent& event) {
    if (event.type == ava::app::RuntimeEventType::ToolStart) {
      return ava::core::VoidResult{
          std::unexpected(ava::core::Error(ava::core::ErrorCategory::Io, "event sink failed"))};
    }
    return ava::core::VoidResult{};
  };

  auto result = ava::app::run_prompt(*session, "read with failing sink", provider, transport, run_options);
  expect(!result && result.error().category() == ava::core::ErrorCategory::Io &&
             result.error().message() == "event sink failed",
         "runtime returns the event sink write failure");
  expect(transport.requests().size() == 1, "event sink failure cancels before the next provider request");
}

void test_app_print_prompt_merging() {
  auto explicit_only = ava::app::merge_print_prompt(
      ava::app::PrintPromptInputs{.explicit_prompt = std::string("explicit"), .stdin_prompt = std::nullopt});
  expect(explicit_only && *explicit_only == "explicit", "print prompt uses explicit prompt when stdin is absent");

  auto stdin_only = ava::app::merge_print_prompt(
      ava::app::PrintPromptInputs{.explicit_prompt = std::nullopt, .stdin_prompt = std::string("stdin")});
  expect(stdin_only && *stdin_only == "stdin", "print prompt uses stdin when explicit prompt is absent");

  auto merged = ava::app::merge_print_prompt(
      ava::app::PrintPromptInputs{.explicit_prompt = std::string("explicit"), .stdin_prompt = std::string("stdin")});
  expect(merged && *merged == "explicit\n\nstdin", "print prompt merges explicit and stdin prompts deterministically");

  auto missing = ava::app::merge_print_prompt(
      ava::app::PrintPromptInputs{.explicit_prompt = std::nullopt, .stdin_prompt = std::nullopt});
  expect(!missing && missing.error().message().find("requires a prompt") != std::string::npos,
         "print prompt rejects missing prompt input");
}

void test_headless_permission_policy() {
  const auto workspace = temp_root() / "headless-policy" / "workspace";
  const auto outside = temp_root() / "headless-policy" / "outside.txt";

  const ava::permissions::PermissionPrompt read_prompt{.operation = ava::permissions::Operation::ReadFile,
                                                       .mode = ava::agent::Mode::Build,
                                                       .workspace_dir = workspace,
                                                       .target_path = outside,
                                                       .command = "",
                                                       .tool_name = "read_file",
                                                       .reason = "target is outside the workspace"};
  const ava::permissions::PermissionPrompt search_prompt{.operation = ava::permissions::Operation::SearchFiles,
                                                         .mode = ava::agent::Mode::Build,
                                                         .workspace_dir = workspace,
                                                         .target_path = workspace,
                                                         .command = "",
                                                         .tool_name = "glob",
                                                         .reason = "search requires approval"};
  const ava::permissions::PermissionPrompt write_prompt{.operation = ava::permissions::Operation::EditFile,
                                                        .mode = ava::agent::Mode::Build,
                                                        .workspace_dir = workspace,
                                                        .target_path = outside,
                                                        .command = "",
                                                        .tool_name = "write_file",
                                                        .reason = "target is outside the workspace"};
  const ava::permissions::PermissionPrompt bash_prompt{.operation = ava::permissions::Operation::RunCommand,
                                                       .mode = ava::agent::Mode::Build,
                                                       .workspace_dir = workspace,
                                                       .target_path = workspace,
                                                       .command = "true",
                                                       .tool_name = "bash",
                                                       .reason = "command risk is unknown"};
  const ava::permissions::PermissionPrompt webfetch_prompt{.operation = ava::permissions::Operation::NetworkFetch,
                                                           .mode = ava::agent::Mode::Build,
                                                           .workspace_dir = workspace,
                                                           .target_path = {},
                                                           .command = "https://example.com",
                                                           .tool_name = "webfetch",
                                                           .reason = "network fetch requires explicit approval"};

  auto default_resolver = ava::app::build_headless_permission_resolver(ava::app::HeadlessPermissionPolicyOptions{});
  auto default_read = default_resolver(read_prompt);
  expect(default_read && *default_read == ava::permissions::PermissionResolution::Deny,
         "headless default resolver denies Ask prompts");

  ava::app::HeadlessPermissionPolicyOptions read_only_options;
  auto read_only_added = ava::app::add_headless_allow_policy(read_only_options, "read-only");
  expect(read_only_added.has_value(), "headless read-only allow value parses");
  auto read_only_resolver = ava::app::build_headless_permission_resolver(read_only_options);
  auto read_only_read = read_only_resolver(read_prompt);
  auto read_only_search = read_only_resolver(search_prompt);
  auto read_only_write = read_only_resolver(write_prompt);
  auto read_only_bash = read_only_resolver(bash_prompt);
  auto read_only_webfetch = read_only_resolver(webfetch_prompt);
  expect(read_only_read && *read_only_read == ava::permissions::PermissionResolution::Allow,
         "headless read-only policy allows read prompts");
  expect(read_only_search && *read_only_search == ava::permissions::PermissionResolution::Allow,
         "headless read-only policy allows search prompts");
  expect(read_only_write && *read_only_write == ava::permissions::PermissionResolution::Deny,
         "headless read-only policy denies write prompts");
  expect(read_only_bash && *read_only_bash == ava::permissions::PermissionResolution::Deny,
         "headless read-only policy denies bash prompts");
  expect(read_only_webfetch && *read_only_webfetch == ava::permissions::PermissionResolution::Deny,
         "headless read-only policy denies network prompts");

  ava::app::HeadlessPermissionPolicyOptions tool_options;
  auto tools_added = ava::app::add_headless_allowed_tools(tool_options, "glob,grep,read_file,webfetch");
  expect(tools_added.has_value() && tool_options.allowed_tools.size() == 4,
         "headless allow-tool parses supported comma-separated tool names");
  auto tool_resolver = ava::app::build_headless_permission_resolver(tool_options);
  const auto tool_read = tool_resolver(read_prompt);
  const auto tool_search = tool_resolver(search_prompt);
  const auto tool_webfetch = tool_resolver(webfetch_prompt);
  const ava::permissions::PermissionPrompt lower_layer_read_prompt{.operation = ava::permissions::Operation::ReadFile,
                                                                   .mode = ava::agent::Mode::Build,
                                                                   .workspace_dir = workspace,
                                                                   .target_path = outside,
                                                                   .command = "",
                                                                   .tool_name = "read",
                                                                   .reason = "target is outside the workspace"};
  const ava::permissions::PermissionPrompt mismatched_tool_prompt{.operation = ava::permissions::Operation::EditFile,
                                                                  .mode = ava::agent::Mode::Build,
                                                                  .workspace_dir = workspace,
                                                                  .target_path = outside,
                                                                  .command = "",
                                                                  .tool_name = "read_file",
                                                                  .reason = "target is outside the workspace"};
  const auto lower_layer_read = tool_resolver(lower_layer_read_prompt);
  const auto mismatched_tool = tool_resolver(mismatched_tool_prompt);
  expect(tool_read && *tool_read == ava::permissions::PermissionResolution::Allow,
         "headless allow-tool allows exact read_file prompts");
  expect(tool_search && *tool_search == ava::permissions::PermissionResolution::Allow,
         "headless allow-tool allows exact glob search prompts");
  expect(tool_webfetch && *tool_webfetch == ava::permissions::PermissionResolution::Allow,
         "headless allow-tool allows exact webfetch network prompts");
  expect(lower_layer_read && *lower_layer_read == ava::permissions::PermissionResolution::Deny,
         "headless allow-tool requires exact tool names");
  expect(mismatched_tool && *mismatched_tool == ava::permissions::PermissionResolution::Deny,
         "headless allow-tool does not allow unsafe operations with a safe tool name");

  auto invalid_allow = ava::app::add_headless_allow_policy(tool_options, "nope");
  auto invalid_tool = ava::app::add_headless_allowed_tools(tool_options, "glob,nope");
  auto empty_tool = ava::app::add_headless_allowed_tools(tool_options, "glob,");
  expect(!invalid_allow && invalid_allow.error().category() == ava::core::ErrorCategory::InvalidArgument,
         "headless --allow rejects unsupported values");
  expect(!invalid_tool && invalid_tool.error().category() == ava::core::ErrorCategory::InvalidArgument,
         "headless --allow-tool rejects unsupported values");
  expect(!empty_tool && empty_tool.error().category() == ava::core::ErrorCategory::InvalidArgument,
         "headless --allow-tool rejects empty tool names");
}

void test_app_print_text_mode_outputs_final_text_only() {
  const auto root = temp_root() / "app-print-text";
  std::error_code remove_error;
  std::filesystem::remove_all(root, remove_error);
  const auto workspace = root / "workspace";
  const auto paths = app_test_paths(root);
  std::filesystem::create_directories(workspace);

  ava::app::RuntimeOpenOptions open_options;
  open_options.workspace_dir = workspace;
  open_options.current_dir = workspace;
  open_options.mode = ava::agent::Mode::Build;
  open_options.paths = paths;
  auto session = ava::app::open_runtime_session(open_options);
  expect(session.has_value(), "print text test opens runtime session");
  if (!session) return;

  const ava::provider::OpenAIProvider provider("https://api.example.test");
  ava::tests::FakeTransport transport({ava::provider::HttpResponse{
      .status_code = 200,
      .headers = {},
      .body = "data: {\"type\":\"response.output_text.delta\",\"delta\":\"print answer\"}\n\n"
              "data: [DONE]\n\n",
  }});
  ava::app::RuntimeRunOptions runtime_options;
  runtime_options.access_token = "token";
  const ava::app::PrintModeRunOptions run_options{.output_format = ava::app::PrintOutputFormat::Text,
                                                  .runtime_options = runtime_options};
  std::ostringstream out;
  std::ostringstream err;
  auto result = ava::app::run_print_prompt(*session, "hello print", provider, transport, run_options, out, err);
  expect(result && result->final_text == "print answer", "print text mode returns agent result");
  expect(out.str() == "print answer" && err.str().empty(), "print text mode writes only final text to stdout");
  expect(transport.requests().size() == 1 && transport.requests()[0].body.find("hello print") != std::string::npos,
         "print text mode sends prompt through shared runtime");
}

void test_app_print_text_mode_with_streaming_keeps_stdout_final_only() {
  const auto root = temp_root() / "app-print-text-streaming";
  std::error_code remove_error;
  std::filesystem::remove_all(root, remove_error);
  const auto workspace = root / "workspace";
  const auto paths = app_test_paths(root);
  std::filesystem::create_directories(workspace);

  ava::app::RuntimeOpenOptions open_options;
  open_options.workspace_dir = workspace;
  open_options.current_dir = workspace;
  open_options.mode = ava::agent::Mode::Build;
  open_options.paths = paths;
  auto session = ava::app::open_runtime_session(open_options);
  expect(session.has_value(), "print text streaming test opens runtime session");
  if (!session) return;

  const ava::provider::OpenAIProvider provider("https://api.example.test");
  ChunkedStreamingTransport transport({"data: {\"type\":\"response.output_text.delta\",\"delta\":\"live \"}\n\n",
                                       "data: {\"type\":\"response.output_text.delta\",\"delta\":\"answer\"}\n\n",
                                       "data: [DONE]\n\n"});
  ava::app::RuntimeRunOptions runtime_options;
  runtime_options.access_token = "token";
  const ava::app::PrintModeRunOptions run_options{.output_format = ava::app::PrintOutputFormat::Text,
                                                  .runtime_options = runtime_options};
  std::ostringstream out;
  std::ostringstream err;
  auto result =
      ava::app::run_print_prompt(*session, "hello streaming print", provider, transport, run_options, out, err);
  expect(result && result->final_text == "live answer", "print text streaming mode returns final agent result");
  expect(out.str() == "live answer" && err.str().empty(), "print text streaming mode keeps stdout final-answer-only");
}

void test_app_print_text_mode_reports_stdout_write_failure() {
  const auto root = temp_root() / "app-print-text-write-failure";
  std::error_code remove_error;
  std::filesystem::remove_all(root, remove_error);
  const auto workspace = root / "workspace";
  const auto paths = app_test_paths(root);
  std::filesystem::create_directories(workspace);

  ava::app::RuntimeOpenOptions open_options;
  open_options.workspace_dir = workspace;
  open_options.current_dir = workspace;
  open_options.mode = ava::agent::Mode::Build;
  open_options.paths = paths;
  auto session = ava::app::open_runtime_session(open_options);
  expect(session.has_value(), "print text stdout failure test opens runtime session");
  if (!session) return;

  const ava::provider::OpenAIProvider provider("https://api.example.test");
  ava::tests::FakeTransport transport({ava::provider::HttpResponse{
      .status_code = 200,
      .headers = {},
      .body = "data: {\"type\":\"response.output_text.delta\",\"delta\":\"print answer\"}\n\n"
              "data: [DONE]\n\n",
  }});
  ava::app::RuntimeRunOptions runtime_options;
  runtime_options.access_token = "token";
  const ava::app::PrintModeRunOptions run_options{.output_format = ava::app::PrintOutputFormat::Text,
                                                  .runtime_options = runtime_options};
  FailingStreambuf failing_buffer;
  std::ostream out(&failing_buffer);
  std::ostringstream err;
  auto result = ava::app::run_print_prompt(*session, "hello print", provider, transport, run_options, out, err);
  expect(!result && result.error().category() == ava::core::ErrorCategory::Io &&
             result.error().message() == "failed to write print output",
         "print text mode reports stdout write failures");
}

void test_app_print_mode_uses_headless_permission_policy() {
  const auto root = temp_root() / "app-print-policy";
  std::error_code remove_error;
  std::filesystem::remove_all(root, remove_error);
  const auto workspace = root / "workspace";
  const auto outside_path = root / "outside.txt";
  const auto paths = app_test_paths(root);
  std::filesystem::create_directories(workspace);
  {
    std::ofstream file(outside_path, std::ios::binary | std::ios::trunc);
    file << "outside print policy";
  }

  ava::app::RuntimeOpenOptions open_options;
  open_options.workspace_dir = workspace;
  open_options.current_dir = workspace;
  open_options.mode = ava::agent::Mode::Build;
  open_options.paths = paths;
  auto session = ava::app::open_runtime_session(open_options);
  expect(session.has_value(), "print policy test opens runtime session");
  if (!session) return;

  const ava::provider::OpenAIProvider provider("https://api.example.test");
  ava::tests::FakeTransport transport({ava::provider::HttpResponse{
                                           .status_code = 200,
                                           .headers = {},
                                           .body = "data: {\"type\":\"response.function_call.added\",\"item_id\":"
                                                   "\"call_outside\",\"name\":\"read_file\"}\n\n"
                                                   "data: "
                                                   "{\"type\":\"response.function_call_arguments.delta\","
                                                   "\"item_id\":\"call_outside\",\"delta\":\"{"
                                                   "\\\"path\\\":\\\"" +
                                                   ava::core::json::escape(outside_path.generic_string()) +
                                                   "\\\"}\"}\n\n"
                                                   "data: [DONE]\n\n",
                                       },
                                       ava::provider::HttpResponse{
                                           .status_code = 200,
                                           .headers = {},
                                           .body = "data: {\"type\":\"response.output_text.delta\",\"delta\":"
                                                   "\"policy allowed\"}\n\n"
                                                   "data: [DONE]\n\n",
                                       }});

  ava::app::HeadlessPermissionPolicyOptions policy_options;
  auto allowed_tool = ava::app::add_headless_allowed_tools(policy_options, "read_file");
  expect(allowed_tool.has_value(), "print policy test configures read_file allow-tool");
  ava::app::RuntimeRunOptions runtime_options;
  runtime_options.access_token = "token";
  runtime_options.permission_resolver = ava::app::build_headless_permission_resolver(policy_options);
  const ava::app::PrintModeRunOptions run_options{.output_format = ava::app::PrintOutputFormat::Text,
                                                  .runtime_options = std::move(runtime_options)};
  std::ostringstream out;
  std::ostringstream err;
  auto result =
      ava::app::run_print_prompt(*session, "read outside in print", provider, transport, run_options, out, err);
  expect(result && result->final_text == "policy allowed" && result->tool_calls == 1,
         "print mode uses supplied headless permission resolver for tool asks");
  expect(transport.requests().size() == 2 &&
             transport.requests()[1].body.find("outside print policy") != std::string::npos,
         "print mode continuation includes allow-tool-approved read_file result");
}

void test_app_print_mode_refreshes_expired_oauth_before_provider_request() {
  const auto root = temp_root() / "app-print-oauth-refresh";
  std::error_code remove_error;
  std::filesystem::remove_all(root, remove_error);
  const auto workspace = root / "workspace";
  const auto paths = app_test_paths(root);
  std::filesystem::create_directories(workspace);
  auto stored = ava::config::store_openai_credential(
      paths, ava::config::OpenAICredential{.type = ava::config::OpenAICredentialType::OAuth,
                                           .access_token = "expired-print-access",
                                           .refresh_token = "print-refresh",
                                           .expires_at = 100,
                                           .account_id = "acct_old",
                                           .source_path = {}});
  expect(stored.has_value(), "print OAuth refresh test stores expired credential");

  const ava::provider::OpenAIProvider provider("https://api.example.test");
  ava::tests::FakeTransport transport({ava::provider::HttpResponse{
                                           .status_code = 200,
                                           .headers = {},
                                           .body = "{\"access_token\":\"print-refreshed-access\","
                                                   "\"refresh_token\":\"print-rotated-refresh\","
                                                   "\"expires_in\":3600,\"account_id\":\"acct_print\"}",
                                       },
                                       ava::provider::HttpResponse{
                                           .status_code = 200,
                                           .headers = {},
                                           .body = "data: {\"type\":\"response.output_text.delta\",\"delta\":"
                                                   "\"print refreshed answer\"}\n\n"
                                                   "data: [DONE]\n\n",
                                       }});

  ava::app::PrintModeOptions options;
  options.open_options.workspace_dir = workspace;
  options.open_options.current_dir = workspace;
  options.open_options.mode = ava::agent::Mode::Build;
  options.open_options.paths = paths;
  options.explicit_prompt = "hello refreshed print";
  options.provider_override = std::cref(provider);
  options.transport_override = std::ref(transport);

  std::istringstream in;
  std::ostringstream out;
  std::ostringstream err;
  const auto exit_code = ava::app::run_print_mode(options, in, out, err);
  expect(exit_code == 0 && out.str() == "print refreshed answer" && err.str().empty(),
         "print mode completes after refreshing expired OAuth credentials");
  expect(transport.requests().size() == 2 && transport.requests()[0].url == "https://auth.openai.com/oauth/token" &&
             transport.requests()[1].headers.at("Authorization") == "Bearer print-refreshed-access" &&
             transport.requests()[1].headers.at("ChatGPT-Account-Id") == "acct_print" &&
             transport.requests()[1].body.find("hello refreshed print") != std::string::npos,
         "print mode refreshes OAuth before sending provider request");
  auto persisted = ava::config::load_openai_credential(paths);
  expect(persisted && persisted->has_value() && (*persisted)->access_token == "print-refreshed-access" &&
             (*persisted)->refresh_token == "print-rotated-refresh",
         "print mode OAuth preflight persists refreshed credential before provider startup");
}

void test_app_connect_provider_credentials_headlessly() {
  ScopedStdinTerminalState terminal_state;
  const auto root = temp_root() / "app-connect-provider-credentials";
  std::error_code remove_error;
  std::filesystem::remove_all(root, remove_error);
  const auto paths = app_test_paths(root);

  std::istringstream anthropic_input("anthropic-api-key\n");
  std::ostringstream anthropic_out;
  std::ostringstream anthropic_err;
  const auto anthropic_exit = ava::app::run_connect_provider_credential(
      paths,
      ava::app::ConnectProviderCredentialOptions{.provider_id = "anthropic",
                                                 .credential_type = ava::app::ConnectCredentialType::ApiKey,
                                                 .env_var = std::nullopt},
      anthropic_input, anthropic_out, anthropic_err);
  expect(anthropic_exit == 0 && anthropic_err.str().empty() &&
             anthropic_out.str().find("Stored API key credential") != std::string::npos,
         "headless provider connect stores Anthropic API key from stdin");
  ava::tests::FakeTransport transport({});
  auto anthropic = ava::config::provider_credential_for_request(paths, "anthropic", transport);
  expect(anthropic && anthropic->has_value() && (*anthropic)->access_token == "anthropic-api-key" &&
             (*anthropic)->credential_type == "api_key",
         "headless provider connect writes loadable Anthropic API key auth");

  std::istringstream anthropic_oauth_input("anthropic-oauth-token\r\n");
  std::ostringstream anthropic_oauth_out;
  std::ostringstream anthropic_oauth_err;
  const auto anthropic_oauth_exit = ava::app::run_connect_provider_credential(
      paths,
      ava::app::ConnectProviderCredentialOptions{.provider_id = "anthropic",
                                                 .credential_type = ava::app::ConnectCredentialType::OAuthToken,
                                                 .env_var = std::nullopt},
      anthropic_oauth_input, anthropic_oauth_out, anthropic_oauth_err);
  expect(anthropic_oauth_exit == 0 && anthropic_oauth_err.str().empty() &&
             anthropic_oauth_out.str().find("Stored OAuth bearer token credential") != std::string::npos,
         "headless provider connect stores Anthropic OAuth bearer token from stdin");
  anthropic = ava::config::provider_credential_for_request(paths, "anthropic", transport);
  expect(anthropic && anthropic->has_value() && (*anthropic)->access_token == "anthropic-oauth-token" &&
             (*anthropic)->credential_type == "oauth",
         "headless provider connect replaces Anthropic API key with OAuth bearer token");

  ScopedEnvVar moonshot_key("AVA_TEST_MOONSHOT_KEY", "moonshot-api-key");
  std::istringstream moonshot_input;
  std::ostringstream moonshot_out;
  std::ostringstream moonshot_err;
  const auto moonshot_exit = ava::app::run_connect_provider_credential(
      paths,
      ava::app::ConnectProviderCredentialOptions{.provider_id = "moonshot",
                                                 .credential_type = ava::app::ConnectCredentialType::ApiKey,
                                                 .env_var = "AVA_TEST_MOONSHOT_KEY"},
      moonshot_input, moonshot_out, moonshot_err);
  expect(moonshot_exit == 0 && moonshot_err.str().empty(),
         "headless provider connect stores Moonshot API key from environment");
  auto moonshot = ava::config::provider_credential_for_request(paths, "moonshot", transport);
  expect(moonshot && moonshot->has_value() && (*moonshot)->access_token == "moonshot-api-key" &&
             (*moonshot)->credential_type == "api_key",
         "headless provider connect writes loadable Moonshot API key auth");
  anthropic = ava::config::provider_credential_for_request(paths, "anthropic", transport);
  expect(anthropic && anthropic->has_value() && (*anthropic)->access_token == "anthropic-oauth-token",
         "headless provider connect preserves existing provider credentials when adding another provider");

  std::istringstream invalid_env_input;
  std::ostringstream invalid_env_out;
  std::ostringstream invalid_env_err;
  const auto invalid_env_exit = ava::app::run_connect_provider_credential(
      paths,
      ava::app::ConnectProviderCredentialOptions{.provider_id = "anthropic",
                                                 .credential_type = ava::app::ConnectCredentialType::ApiKey,
                                                 .env_var = "sk-should-not-be-echoed"},
      invalid_env_input, invalid_env_out, invalid_env_err);
  expect(invalid_env_exit == 1 && invalid_env_out.str().empty() &&
             invalid_env_err.str().find("credential env var name is invalid") != std::string::npos &&
             invalid_env_err.str().find("sk-should-not-be-echoed") == std::string::npos,
         "headless provider connect rejects invalid env names without echoing secrets");

  std::istringstream missing_env_input;
  std::ostringstream missing_env_out;
  std::ostringstream missing_env_err;
  const auto missing_env_exit = ava::app::run_connect_provider_credential(
      paths,
      ava::app::ConnectProviderCredentialOptions{.provider_id = "anthropic",
                                                 .credential_type = ava::app::ConnectCredentialType::ApiKey,
                                                 .env_var = "SKSHOULDNOTBEECHOED"},
      missing_env_input, missing_env_out, missing_env_err);
  expect(missing_env_exit == 1 && missing_env_out.str().empty() &&
             missing_env_err.str().find("credential env var is not set") != std::string::npos &&
             missing_env_err.str().find("SKSHOULDNOTBEECHOED") == std::string::npos,
         "headless provider connect omits env var names from missing-env errors");

  std::istringstream empty_stdin_input("\r\n");
  std::ostringstream empty_stdin_out;
  std::ostringstream empty_stdin_err;
  const auto empty_stdin_exit = ava::app::run_connect_provider_credential(
      paths,
      ava::app::ConnectProviderCredentialOptions{.provider_id = "anthropic",
                                                 .credential_type = ava::app::ConnectCredentialType::ApiKey,
                                                 .env_var = std::nullopt},
      empty_stdin_input, empty_stdin_out, empty_stdin_err);
  expect(empty_stdin_exit == 1 && empty_stdin_out.str().empty() &&
             empty_stdin_err.str().find("credential stdin was empty") != std::string::npos,
         "headless provider connect rejects empty stdin credentials");

  const auto wizard_root = temp_root() / "app-connect-provider-wizard";
  std::error_code wizard_remove_error;
  std::filesystem::remove_all(wizard_root, wizard_remove_error);
  const auto wizard_paths = app_test_paths(wizard_root);
  std::istringstream wizard_input("anthropic\napi-key\nwizard-api-key\n");
  std::ostringstream wizard_out;
  std::ostringstream wizard_err;
  const auto wizard_exit = ava::app::run_connect_provider_wizard(
      wizard_paths,
      ava::app::ConnectProviderWizardOptions{
          .provider_id = std::nullopt, .credential_type = std::nullopt, .stdin_is_tty = true},
      wizard_input, wizard_out, wizard_err);
  expect(wizard_exit == 0 && wizard_err.str().empty() && wizard_out.str().find("Add credential") != std::string::npos &&
             wizard_out.str().find("Select provider") != std::string::npos &&
             wizard_out.str().find("Stored anthropic API key credential") != std::string::npos,
         "interactive provider wizard opens a searchable provider menu before prompting for method and secret");
  auto wizard_anthropic = ava::config::provider_credential_for_request(wizard_paths, "anthropic", transport);
  expect(wizard_anthropic && wizard_anthropic->has_value() && (*wizard_anthropic)->access_token == "wizard-api-key",
         "interactive provider wizard stores a loadable credential");

  std::istringstream cancelled_wizard_input("\x1b");
  std::ostringstream cancelled_wizard_out;
  std::ostringstream cancelled_wizard_err;
  const auto cancelled_wizard_exit = ava::app::run_connect_provider_wizard(
      wizard_paths,
      ava::app::ConnectProviderWizardOptions{
          .provider_id = std::nullopt, .credential_type = std::nullopt, .stdin_is_tty = true},
      cancelled_wizard_input, cancelled_wizard_out, cancelled_wizard_err);
  expect(cancelled_wizard_exit == 1 && cancelled_wizard_err.str().find("provider login cancelled") != std::string::npos,
         "interactive provider wizard cancels on standalone escape without waiting for more input");

  std::istringstream arrow_wizard_input("\x1b[B\napi-key\narrow-api-key\n");
  std::ostringstream arrow_wizard_out;
  std::ostringstream arrow_wizard_err;
  const auto arrow_wizard_exit = ava::app::run_connect_provider_wizard(
      wizard_paths,
      ava::app::ConnectProviderWizardOptions{
          .provider_id = std::nullopt, .credential_type = std::nullopt, .stdin_is_tty = true},
      arrow_wizard_input, arrow_wizard_out, arrow_wizard_err);
  expect(arrow_wizard_exit == 0 && arrow_wizard_err.str().empty(),
         "interactive provider wizard supports arrow-key provider selection");
  wizard_anthropic = ava::config::provider_credential_for_request(wizard_paths, "anthropic", transport);
  expect(wizard_anthropic && wizard_anthropic->has_value() && (*wizard_anthropic)->access_token == "arrow-api-key",
         "interactive provider wizard arrow selection stores the selected provider credential");

  std::istringstream ignored_escape_wizard_input("\x1b[Canthropic\napi-key\nright-arrow-api-key\n");
  std::ostringstream ignored_escape_wizard_out;
  std::ostringstream ignored_escape_wizard_err;
  const auto ignored_escape_wizard_exit = ava::app::run_connect_provider_wizard(
      wizard_paths,
      ava::app::ConnectProviderWizardOptions{
          .provider_id = std::nullopt, .credential_type = std::nullopt, .stdin_is_tty = true},
      ignored_escape_wizard_input, ignored_escape_wizard_out, ignored_escape_wizard_err);
  expect(ignored_escape_wizard_exit == 0 && ignored_escape_wizard_err.str().empty(),
         "interactive provider wizard ignores unsupported escape sequences without polluting search text");
  expect(ignored_escape_wizard_out.str().find("Search: anthropic") != std::string::npos,
         "interactive provider wizard keeps typed search text after unsupported escape sequence");
  wizard_anthropic = ava::config::provider_credential_for_request(wizard_paths, "anthropic", transport);
  expect(
      wizard_anthropic && wizard_anthropic->has_value() && (*wizard_anthropic)->access_token == "right-arrow-api-key",
      "interactive provider wizard stores typed provider after ignoring unsupported escape sequence");

  std::istringstream non_tty_wizard_input;
  std::ostringstream non_tty_wizard_out;
  std::ostringstream non_tty_wizard_err;
  const auto non_tty_wizard_exit = ava::app::run_connect_provider_wizard(
      wizard_paths,
      ava::app::ConnectProviderWizardOptions{.provider_id = "anthropic",
                                             .credential_type = ava::app::ConnectCredentialType::ApiKey,
                                             .stdin_is_tty = false},
      non_tty_wizard_input, non_tty_wizard_out, non_tty_wizard_err);
  expect(non_tty_wizard_exit == 2 && non_tty_wizard_out.str().empty() &&
             non_tty_wizard_err.str().find("interactive provider login requires a terminal") != std::string::npos,
         "interactive provider wizard refuses to prompt without a tty");
}

void test_app_print_json_mode_outputs_runtime_events() {
  const auto root = temp_root() / "app-print-json";
  std::error_code remove_error;
  std::filesystem::remove_all(root, remove_error);
  const auto workspace = root / "workspace";
  const auto paths = app_test_paths(root);
  std::filesystem::create_directories(workspace);

  ava::app::RuntimeOpenOptions open_options;
  open_options.workspace_dir = workspace;
  open_options.current_dir = workspace;
  open_options.mode = ava::agent::Mode::Plan;
  open_options.paths = paths;
  auto session = ava::app::open_runtime_session(open_options);
  expect(session.has_value(), "print json test opens runtime session");
  if (!session) return;

  const ava::provider::OpenAIProvider provider("https://api.example.test");
  ava::tests::FakeTransport transport({ava::provider::HttpResponse{
      .status_code = 200,
      .headers = {},
      .body = "data: {\"type\":\"response.output_text.delta\",\"delta\":\"json answer\"}\n\n"
              "data: [DONE]\n\n",
  }});
  ava::app::RuntimeRunOptions runtime_options;
  runtime_options.access_token = "token";
  const ava::app::PrintModeRunOptions run_options{.output_format = ava::app::PrintOutputFormat::Json,
                                                  .runtime_options = runtime_options};
  std::ostringstream out;
  std::ostringstream err;
  auto result = ava::app::run_print_prompt(*session, "json prompt", provider, transport, run_options, out, err);
  const auto jsonl = out.str();
  const auto last_break = jsonl.size() > 1 ? jsonl.rfind('\n', jsonl.size() - 2) : std::string::npos;
  const auto last_line = jsonl.substr(last_break == std::string::npos ? 0 : last_break + 1);
  expect(result && result->final_text == "json answer", "print json mode returns agent result");
  expect(err.str().empty(), "print json mode leaves diagnostics on stderr only when needed");
  expect(std::count(jsonl.begin(), jsonl.end(), '\n') == 4 && jsonl.find("\"schema_version\":1") != std::string::npos &&
             jsonl.find("\"event_id\":\"event_") != std::string::npos &&
             jsonl.find("\"name\":\"session_start\"") != std::string::npos &&
             jsonl.find("\"name\":\"user_message\"") != std::string::npos &&
             jsonl.find("\"name\":\"assistant_message\"") != std::string::npos &&
             jsonl.find("\"payload\":{\"text\":\"json answer\"}") != std::string::npos &&
             last_line.find("\"name\":\"done\"") != std::string::npos,
         "print json mode writes JSONL event envelopes ending in done");

  auto error_session = ava::app::open_runtime_session(open_options);
  expect(error_session.has_value(), "print json error test opens runtime session");
  if (!error_session) return;
  ava::tests::FakeTransport error_transport({ava::provider::HttpResponse{
      .status_code = 500,
      .headers = {},
      .body = "upstream failure",
  }});
  std::ostringstream error_out;
  std::ostringstream error_err;
  auto error_result = ava::app::run_print_prompt(*error_session, "json error", provider, error_transport, run_options,
                                                 error_out, error_err);
  const auto error_jsonl = error_out.str();
  const auto error_last_break =
      error_jsonl.size() > 1 ? error_jsonl.rfind('\n', error_jsonl.size() - 2) : std::string::npos;
  const auto error_last_line = error_jsonl.substr(error_last_break == std::string::npos ? 0 : error_last_break + 1);
  expect(!error_result && error_err.str().empty() && error_last_line.find("\"name\":\"error\"") != std::string::npos,
         "print json mode writes failed turns as JSONL envelopes ending in error");
}

void test_app_print_json_mode_streams_provider_deltas_before_final_message() {
  const auto root = temp_root() / "app-print-json-streaming";
  std::error_code remove_error;
  std::filesystem::remove_all(root, remove_error);
  const auto workspace = root / "workspace";
  const auto paths = app_test_paths(root);
  std::filesystem::create_directories(workspace);

  ava::app::RuntimeOpenOptions open_options;
  open_options.workspace_dir = workspace;
  open_options.current_dir = workspace;
  open_options.mode = ava::agent::Mode::Plan;
  open_options.paths = paths;
  auto session = ava::app::open_runtime_session(open_options);
  expect(session.has_value(), "print json streaming test opens runtime session");
  if (!session) return;

  const ava::provider::OpenAIProvider provider("https://api.example.test");
  ChunkedStreamingTransport transport({"data: {\"type\":\"response.output_text.delta\",\"delta\":\"json \"}\n\n",
                                       "data: {\"type\":\"response.output_text.delta\",\"delta\":\"stream\"}\n\n",
                                       "data: [DONE]\n\n"});
  ava::app::RuntimeRunOptions runtime_options;
  runtime_options.access_token = "token";
  const ava::app::PrintModeRunOptions run_options{.output_format = ava::app::PrintOutputFormat::Json,
                                                  .runtime_options = runtime_options};
  std::ostringstream out;
  std::ostringstream err;
  auto result =
      ava::app::run_print_prompt(*session, "json streaming prompt", provider, transport, run_options, out, err);
  const auto jsonl = out.str();
  const auto update_position = jsonl.find("\"name\":\"message_update\"");
  const auto final_position = jsonl.find("\"name\":\"assistant_message\"");
  expect(result && result->final_text == "json stream", "print json streaming mode returns accumulated final text");
  expect(update_position != std::string::npos && final_position != std::string::npos &&
             update_position < final_position && jsonl.find("\"name\":\"message_end\"") != std::string::npos,
         "print json mode emits streaming message deltas before final assistant message");
}

void test_app_command_dispatcher() {
  const auto root = temp_root() / "app-command-dispatcher";
  std::error_code remove_error;
  std::filesystem::remove_all(root, remove_error);
  const auto workspace = root / "workspace";
  const auto paths = app_test_paths(root);
  std::filesystem::create_directories(workspace / "src");
  std::filesystem::create_directories(paths.ava_config_dir);
  {
    std::ofstream file(workspace / "AGENTS.md", std::ios::binary | std::ios::trunc);
    file << "dispatcher context\n";
  }
  {
    std::ofstream file(workspace / "src" / "main.cpp", std::ios::binary | std::ios::trunc);
    file << "int main() { return 0; }\n";
  }
  write_app_test_file(paths.ava_config_dir / "plugins" / "com.example.global" / "plugin.json",
                      app_test_plugin_manifest_json("com.example.global", "Global Plugin"));
  write_app_test_file(workspace / ".ava" / "plugins" / "com.example.project" / "plugin.json",
                      app_test_plugin_manifest_json("com.example.project", "Project Plugin"));
  write_app_test_file(workspace / ".ava" / "plugins" / "com.example.bad" / "plugin.json", "{not-json");
  write_app_test_file(workspace / ".ava" / "mcp.json",
                      app_test_mcp_config_json("fs", "Filesystem Server", AVA_FAKE_MCP_SERVER_PATH));

  ava::app::RuntimeOpenOptions open_options;
  open_options.workspace_dir = workspace;
  open_options.current_dir = workspace;
  open_options.mode = ava::agent::Mode::Plan;
  open_options.paths = paths;
  auto session = ava::app::open_runtime_session(open_options);
  expect(session.has_value(), "command dispatcher test opens runtime session");
  if (!session) return;
  const auto plan_system_prompt = session->system_prompt;

  expect(
      ava::app::is_backend_command("/model") && ava::app::is_backend_command("/models") &&
          ava::app::is_backend_command("/hotkeys") && ava::app::is_backend_command("/details") &&
          ava::app::is_backend_command("/thinking") && ava::app::is_backend_command("/status") &&
          ava::app::is_backend_command("/plugins"),
      "command catalog classifies display toggles, status aliases, disabled aliases, and hotkeys as backend commands");

  const std::vector<ava::app::CommandHotkey> custom_hotkeys = {
      ava::app::CommandHotkey{.action = "submit", .description = "Submit custom", .keys = "Ctrl+M"},
      ava::app::CommandHotkey{.action = "variant_cycle", .description = "Cycle variants", .keys = "Ctrl+T"}};
  auto hotkeys =
      ava::app::run_command(*session, ava::app::CommandRequest{.command = "/hotkeys", .hotkeys = custom_hotkeys});
  expect(hotkeys && hotkeys->handled && !hotkeys->output.empty() &&
             hotkeys->output[0].find("Ctrl+M") != std::string::npos &&
             hotkeys->output[0].find("variant_cycle") != std::string::npos,
         "command dispatcher /hotkeys reports effective keybind metadata");
  auto details = ava::app::run_command(*session, ava::app::CommandRequest{.command = "/details"});
  expect(details && details->handled && !details->output.empty() &&
             details->output[0].find("TUI display toggle") != std::string::npos,
         "command dispatcher recognizes /details without inventing backend tool metadata");
  auto thinking = ava::app::run_command(*session, ava::app::CommandRequest{.command = "/thinking"});
  expect(thinking && thinking->handled && !thinking->output.empty() &&
             thinking->output[0].find("TUI display toggle") != std::string::npos &&
             thinking->output[0].find("does not change provider reasoning mode") != std::string::npos,
         "command dispatcher recognizes /thinking as display-only instead of changing backend reasoning mode");
  auto help = ava::app::run_command(*session, ava::app::CommandRequest{.command = "/help", .hotkeys = custom_hotkeys});
  expect(help && help->handled && !help->output.empty() && help->output[0].find("/hotkeys") != std::string::npos &&
             help->output[0].find("/connect") != std::string::npos &&
             help->output[0].find("/plugins") != std::string::npos &&
             help->output[0].find("Unavailable commands") != std::string::npos &&
             help->output[0].find("Ctrl+M") != std::string::npos,
         "command dispatcher /help includes catalog commands and effective hotkeys");

  auto plugins_usage = ava::app::run_command(*session, ava::app::CommandRequest{.command = "/plugins"});
  expect(plugins_usage && plugins_usage->handled && !plugins_usage->output.empty() &&
             plugins_usage->output[0].find("usage: /plugins") != std::string::npos,
         "command dispatcher /plugins without a subcommand reports usage");
  auto plugins = ava::app::run_command(*session, ava::app::CommandRequest{.command = "/plugins list"});
  expect(plugins && plugins->handled && !plugins->output.empty() &&
             plugins->output[0].find("com.example.global") != std::string::npos &&
             plugins->output[0].find("com.example.project") != std::string::npos &&
             plugins->output[0].find("Failures: 1") != std::string::npos,
         "command dispatcher /plugins list reports discovered plugins and diagnostics failures");
  auto inspect_plugin =
      ava::app::run_command(*session, ava::app::CommandRequest{.command = "/plugins inspect com.example.project"});
  expect(inspect_plugin && inspect_plugin->handled && !inspect_plugin->output.empty() &&
             inspect_plugin->output[0].find("entrypoint: node plugin.js --safe (not executed)") != std::string::npos &&
             inspect_plugin->output[0].find("no plugin process is started yet") != std::string::npos,
         "command dispatcher /plugins inspect shows manifest details without executing entrypoints");
  auto enable_plugin =
      ava::app::run_command(*session, ava::app::CommandRequest{.command = "/plugins enable com.example.project"});
  expect(enable_plugin && enable_plugin->handled && !enable_plugin->output.empty() &&
             enable_plugin->output[0].find("Enabled project plugin com.example.project") != std::string::npos &&
             enable_plugin->output[0].find("No plugin process was started") != std::string::npos,
         "command dispatcher /plugins enable records state without starting plugin processes");
  auto plugins_after_enable = ava::app::run_command(*session, ava::app::CommandRequest{.command = "/plugins list"});
  expect(plugins_after_enable && plugins_after_enable->handled && !plugins_after_enable->output.empty() &&
             plugins_after_enable->output[0].find("com.example.project  enabled") != std::string::npos,
         "command dispatcher /plugins list reflects enablement state");
  const auto slash_items = ava::app::command_catalog_slash_items(*session, custom_hotkeys);
  auto find_slash_item = [&slash_items](std::string_view command) -> const ava::tui::SlashCommandItem* {
    for (const auto& item : slash_items) {
      if (item.command == command) return &item;
    }
    return nullptr;
  };
  auto has_completion = [](const ava::tui::SlashCommandItem* item, std::size_t argument_index, std::string_view value,
                           std::vector<std::string> previous_args = {}) {
    return item != nullptr && std::ranges::any_of(item->argument_completions, [&](const auto& completion) {
             return completion.argument_index == argument_index && completion.value == value &&
                    completion.required_previous_args == previous_args;
           });
  };
  const auto* connect_item = find_slash_item("/connect");
  const auto* models_item = find_slash_item("/models");
  const auto* sessions_item = find_slash_item("/sessions");
  const auto* context_item = find_slash_item("/context");
  const auto* mcp_item = find_slash_item("/mcp");
  const auto* plugin_item = find_slash_item("/plugin");
  expect(has_completion(connect_item, 0, "openai") && has_completion(connect_item, 1, "api-key") &&
             has_completion(models_item, 0, "openai/gpt-5.5") &&
             has_completion(sessions_item, 0, session->store.session_id()) &&
             has_completion(context_item, 0, (workspace / "AGENTS.md").generic_string()) &&
             has_completion(mcp_item, 1, "fs", {"inspect"}) &&
             has_completion(plugin_item, 1, "com.example.project", {"run"}) &&
             has_completion(plugin_item, 2, "todo", {"run", "com.example.project"}),
         "command catalog argument completions are populated from backend provider, model, session, context, MCP, and "
         "plugin metadata");
  auto disable_plugin =
      ava::app::run_command(*session, ava::app::CommandRequest{.command = "/plugins disable com.example.project"});
  expect(disable_plugin && disable_plugin->handled && !disable_plugin->output.empty() &&
             disable_plugin->output[0].find("Disabled project plugin com.example.project") != std::string::npos &&
             disable_plugin->output[0].find("No plugin process was stopped") != std::string::npos,
         "command dispatcher /plugins disable records state without stopping plugin processes");
  auto validate_plugin = ava::app::run_command(
      *session, ava::app::CommandRequest{.command = "/plugins validate .ava/plugins/com.example.project/plugin.json"});
  expect(validate_plugin && validate_plugin->handled && !validate_plugin->output.empty() &&
             validate_plugin->output[0].find("Valid plugin manifest") != std::string::npos &&
             validate_plugin->output[0].find("no entrypoint was executed") != std::string::npos,
         "command dispatcher /plugins validate parses manifests without executing entrypoints");
  auto plugin_failures = ava::app::run_command(*session, ava::app::CommandRequest{.command = "/plugins failures"});
  expect(plugin_failures && plugin_failures->handled && !plugin_failures->output.empty() &&
             plugin_failures->output[0].find("com.example.bad") != std::string::npos,
         "command dispatcher /plugins failures reports invalid discovered manifests");
  auto models = ava::app::run_command(*session, ava::app::CommandRequest{.command = "/model"});
  expect(models && models->handled && !models->output.empty() &&
             models->output[0].find("Models:") != std::string::npos &&
             models->output[0].find("current ") != std::string::npos &&
             models->output[0].find("reasoning current") != std::string::npos &&
             models->output[0].find("reasoning levels: low, medium, high, xhigh") != std::string::npos &&
             models->output[0].find("reasoning params") != std::string::npos &&
             models->output[0].find("reasoning.effort=<level>") != std::string::npos &&
             models->output[0].find("reasoning.summary=auto") != std::string::npos &&
             models->output[0].find("reasoning format") != std::string::npos &&
             models->output[0].find("Ctrl+T cycles") != std::string::npos,
         "command dispatcher lists provider/model reasoning metadata and documents TUI reasoning cycling");
  auto filtered_models = ava::app::run_command(*session, ava::app::CommandRequest{.command = "/models gpt-5.5"});
  expect(filtered_models && filtered_models->handled && !filtered_models->output.empty() &&
             filtered_models->output[0].find("filter gpt-5.5") != std::string::npos &&
             filtered_models->output[0].find("gpt-5.5") != std::string::npos,
         "command dispatcher /models accepts backend-backed autocomplete query text without switching models");

  auto context = ava::app::run_command(*session, ava::app::CommandRequest{.command = "/context"});
  expect(context && context->handled && !context->output.empty() &&
             context->output[0].find("workspace") != std::string::npos &&
             context->output[0].find("AGENTS.md") != std::string::npos,
         "command dispatcher /context reports loaded context metadata");
  auto filtered_context = ava::app::run_command(*session, ava::app::CommandRequest{.command = "/context AGENTS"});
  expect(filtered_context && filtered_context->handled && !filtered_context->output.empty() &&
             filtered_context->output[0].find("AGENTS.md") != std::string::npos,
         "command dispatcher /context accepts backend context-source query text");
  auto filtered_sessions =
      ava::app::run_command(*session, ava::app::CommandRequest{.command = "/sessions " + session->store.session_id()});
  expect(filtered_sessions && filtered_sessions->handled && !filtered_sessions->output.empty() &&
             filtered_sessions->output[0].find(session->store.session_id()) != std::string::npos,
         "command dispatcher /sessions accepts backend session-id query text");

  auto mode = ava::app::run_command(*session, ava::app::CommandRequest{.command = "/mode"});
  expect(mode && mode->handled && session->mode == ava::agent::Mode::Build && !mode->output.empty() &&
             mode->output[0].find("build") != std::string::npos,
         "command dispatcher /mode toggles runtime mode");
  expect(session->system_prompt != plan_system_prompt &&
             session->system_prompt.find("Implement changes directly") != std::string::npos &&
             session->system_prompt.find("dispatcher context") != std::string::npos,
         "command dispatcher /mode rebuilds the mode-specific system prompt with context");

  bool saw_secret_prompt = false;
  auto connect = ava::app::run_command(
      *session, ava::app::CommandRequest{.command = "/login moonshot oauth",
                                         .question_resolver = [&](const ava::agent::QuestionPrompt& prompt) {
                                           saw_secret_prompt = prompt.modal && prompt.secret && prompt.allow_custom &&
                                                               prompt.question.find("moonshot") != std::string::npos;
                                           return ava::agent::QuestionAnswer{.selected_options = {},
                                                                             .custom_text = "slash-oauth-token"};
                                         }});
  expect(connect && connect->handled && saw_secret_prompt && !connect->output.empty() &&
             connect->output[0].find("Stored moonshot OAuth bearer token credential") != std::string::npos,
         "command dispatcher /login alias stores provider OAuth bearer credentials via masked prompt");
  ava::tests::FakeTransport credential_transport({});
  auto slash_moonshot = ava::config::provider_credential_for_request(session->paths, "moonshot", credential_transport);
  expect(slash_moonshot && slash_moonshot->has_value() && (*slash_moonshot)->access_token == "slash-oauth-token" &&
             (*slash_moonshot)->credential_type == "oauth",
         "slash provider connect writes loadable provider credential");

  std::size_t connect_prompt_count = 0;
  auto connect_modal = ava::app::run_command(
      *session,
      ava::app::CommandRequest{
          .command = "/connect", .question_resolver = [&](const ava::agent::QuestionPrompt& prompt) {
            if (connect_prompt_count == 0) {
              expect(prompt.modal && prompt.searchable && prompt.allow_custom && prompt.question == "Select provider",
                     "slash /connect opens provider selection as searchable modal");
              ++connect_prompt_count;
              return ava::agent::QuestionAnswer{.selected_options = {"anthropic"}, .custom_text = ""};
            }
            if (connect_prompt_count == 1) {
              expect(prompt.modal && !prompt.searchable && !prompt.secret,
                     "slash /connect opens credential type as modal");
              ++connect_prompt_count;
              return ava::agent::QuestionAnswer{.selected_options = {"api_key"}, .custom_text = ""};
            }
            expect(connect_prompt_count == 2 && prompt.modal && prompt.secret &&
                       prompt.question.find("anthropic") != std::string::npos,
                   "slash /connect opens secret prompt as masked modal");
            ++connect_prompt_count;
            return ava::agent::QuestionAnswer{.selected_options = {}, .custom_text = "slash-api-key"};
          }});
  expect(connect_modal && connect_modal->handled && connect_prompt_count == 3 && !connect_modal->output.empty() &&
             connect_modal->output[0].find("Stored anthropic API key credential") != std::string::npos,
         "command dispatcher /connect walks provider, method, and secret modals");
  auto slash_anthropic =
      ava::config::provider_credential_for_request(session->paths, "anthropic", credential_transport);
  expect(slash_anthropic && slash_anthropic->has_value() && (*slash_anthropic)->access_token == "slash-api-key" &&
             (*slash_anthropic)->credential_type == "api_key",
         "slash provider connect modal writes loadable API key credential");

  auto connect_without_tui = ava::app::run_command(*session, ava::app::CommandRequest{.command = "/connect anthropic"});
  expect(connect_without_tui && connect_without_tui->handled && !connect_without_tui->output.empty() &&
             connect_without_tui->output[0].find("--oauth-token-stdin") != std::string::npos &&
             connect_without_tui->output[0].find("--oauth-token-env") != std::string::npos,
         "command dispatcher /connect no-TUI error lists OAuth headless setup flags");

  std::vector<ava::app::RuntimeEvent> command_tool_events;
  auto glob = ava::app::run_command(
      *session, ava::app::CommandRequest{.command = "/glob **/*.cpp",
                                         .event_sink = [&command_tool_events](const ava::app::RuntimeEvent& event) {
                                           command_tool_events.push_back(event);
                                           return ava::core::VoidResult{};
                                         }});
  expect(glob && glob->handled && !glob->output.empty() && glob->output[0].find("src/main.cpp") != std::string::npos,
         "command dispatcher /glob runs existing safe file search command");
  expect(glob && glob->tool_timeline.size() == 2 &&
             glob->tool_timeline[0].status == ava::agent::ToolTimelineStatus::Running &&
             glob->tool_timeline[1].status == ava::agent::ToolTimelineStatus::Success &&
             glob->tool_timeline[1].structured_result_json.find("\"status\":\"success\"") != std::string::npos &&
             glob->tool_timeline[1].total_matches,
         "command dispatcher records running and completed timeline entries with structured result metadata");
  expect(command_tool_events.size() == 2 && command_tool_events[1].type == ava::app::RuntimeEventType::ToolResult &&
             !command_tool_events[1].tool_structured_result_json.empty() &&
             command_tool_events[1].tool_structured_result_json.find("\"tool\":\"glob\"") != std::string::npos &&
             command_tool_events[1].total_matches > 0,
         "command dispatcher emits structured tool result runtime events");

  std::size_t compact_generator_calls = 0;
  auto compact_generator = [&](const std::vector<ava::session::SessionEntry>& entries,
                               const ava::session::CompactionConfig& config, std::string_view instructions,
                               std::size_t estimated_tokens) -> ava::core::Result<std::string> {
    ++compact_generator_calls;
    static_cast<void>(instructions);
    expect(!entries.empty() && config.max_summary_bytes > 0,
           "command dispatcher /compact passes session source data to summary generator");
    static_cast<void>(estimated_tokens);
    return std::string(
        "# Goal\nKeep key facts\n# Constraints / Preferences\nNone noted.\n# Decisions\nNone noted.\n"
        "# Files Read or Modified\nsrc/main.cpp\n# Unresolved Tasks\nNone noted.\n# Next Steps\nContinue.");
  };
  auto compact =
      ava::app::run_command(*session, ava::app::CommandRequest{.command = "/compact Keep key facts",
                                                               .compaction_summary_generator = compact_generator});
  expect(compact && compact->handled && !compact->output.empty() &&
             compact->output[0].find("compaction summary recorded") != std::string::npos,
         "command dispatcher /compact records generated compaction summary");
  auto compact_empty = ava::app::run_command(
      *session, ava::app::CommandRequest{.command = "/compact", .compaction_summary_generator = compact_generator});
  expect(compact_empty && compact_empty->handled && !compact_empty->output.empty() &&
             compact_empty->output[0].find("compaction summary recorded") != std::string::npos,
         "command dispatcher /compact without instructions records generated compaction summary");
  auto compact_trailing = ava::app::run_command(
      *session, ava::app::CommandRequest{.command = "/compact ", .compaction_summary_generator = compact_generator});
  expect(compact_trailing && compact_trailing->handled && !compact_trailing->output.empty() &&
             compact_trailing->output[0].find("compaction summary recorded") != std::string::npos,
         "command dispatcher /compact with trailing space records generated compaction summary");
  expect(compact_generator_calls == 3, "command dispatcher /compact invokes the summary generator once per command");

  auto entries = session->store.load();
  expect(entries && std::ranges::any_of(*entries,
                                        [](const ava::session::SessionEntry& entry) {
                                          return entry.type == ava::session::EntryType::Compaction &&
                                                 entry.data_json.find("Keep key facts") != std::string::npos &&
                                                 entry.data_json.find("\"summary_unavailable\":false") !=
                                                     std::string::npos;
                                        }),
         "command dispatcher /compact persists generated summary and instructions");

  const auto compactions_before_stale = entries ? count_compaction_entries(*entries) : 0;
  std::mutex session_mutex;
  bool introduced_manual_stale_snapshot = false;
  std::size_t manual_stale_generator_calls = 0;
  auto stale_compact = ava::app::run_command(
      *session,
      ava::app::CommandRequest{
          .command = "/compact stale snapshot",
          .compaction_summary_generator = [&](const std::vector<ava::session::SessionEntry>&,
                                              const ava::session::CompactionConfig&, std::string_view,
                                              std::size_t) -> ava::core::Result<std::string> {
            ++manual_stale_generator_calls;
            if (!introduced_manual_stale_snapshot) {
              introduced_manual_stale_snapshot = true;
              static_cast<void>(session->store.append(
                  ava::session::SessionEntry{.id = "entry_manual_compact_concurrent_change",
                                             .parent_id = "",
                                             .type = ava::session::EntryType::UserMessage,
                                             .timestamp = ava::session::now_timestamp(),
                                             .data_json = "{\"text\":\"manual compact concurrent change\"}"}));
            }
            return std::string(
                "# Goal\nStale\n# Constraints / Preferences\nNone noted.\n# Decisions\nNone noted.\n"
                "# Files Read or Modified\nNone noted.\n# Unresolved Tasks\nNone noted.\n# Next Steps\nContinue.");
          },
          .session_mutex = &session_mutex});
  entries = session->store.load();
  expect(stale_compact && stale_compact->handled && !stale_compact->output.empty() &&
             stale_compact->output[0].find("compaction summary recorded") != std::string::npos,
         "command dispatcher /compact retries one stale snapshot and records a fresh summary");
  expect(manual_stale_generator_calls == 2, "manual /compact regenerates summary after a stale snapshot");
  expect(entries && count_compaction_entries(*entries) == compactions_before_stale + 1 &&
             std::ranges::any_of(*entries,
                                 [](const ava::session::SessionEntry& entry) {
                                   return entry.data_json.find("manual compact concurrent change") != std::string::npos;
                                 }),
         "manual /compact stale snapshot preserves concurrent changes and appends one retried compaction");

  auto exported = ava::app::run_command(*session, ava::app::CommandRequest{.command = "/export"});
  expect(exported && exported->handled && !exported->output.empty() &&
             exported->output[0].find("# AVA Session Export") != std::string::npos &&
             exported->output[0].find("## Compaction") != std::string::npos,
         "command dispatcher /export returns markdown for loaded session entries");

  auto seeded_stats_usage = session->store.append(
      ava::session::SessionEntry{.id = "entry_slash_stats_usage",
                                 .parent_id = "",
                                 .type = ava::session::EntryType::AssistantMessage,
                                 .timestamp = "2026-05-02T00:00:00Z",
                                 .data_json = "{\"text\":\"usage\",\"usage\":{\"input_tokens\":12,\"output_tokens\":7,"
                                              "\"total_tokens\":19,\"cost_usd\":0.0015}}"});
  expect(seeded_stats_usage.has_value(), "command dispatcher /stats test seeds usage metadata");
  auto stats = ava::app::run_command(*session, ava::app::CommandRequest{.command = "/stats"});
  expect(stats && stats->handled && !stats->output.empty() &&
             stats->output[0].find("Session stats") != std::string::npos &&
             stats->output[0].find("tokens: input=12 output=7 total=19") != std::string::npos &&
             stats->output[0].find("cost: $0.001500") != std::string::npos &&
             stats->output[0].find("compactions ") != std::string::npos &&
             stats->output[0].find("path:") == std::string::npos &&
             stats->output[0].find("export: /export   resume: ava --session ") != std::string::npos,
         "command dispatcher /stats renders compact session counts, usage, cost, and hints");
  auto status = ava::app::run_command(*session, ava::app::CommandRequest{.command = "/status"});
  expect(status && status->handled && !status->output.empty() && status->output[0] == stats->output[0],
         "command dispatcher /status aliases the backend-backed session stats surface");

  auto quit = ava::app::run_command(*session, ava::app::CommandRequest{.command = "/quit"});
  expect(quit && quit->handled && quit->quit, "command dispatcher /quit requests shell exit");
}

void test_app_compact_provider_summary_success() {
  const auto root = temp_root() / "app-compact-provider-success";
  std::error_code remove_error;
  std::filesystem::remove_all(root, remove_error);
  const auto workspace = root / "workspace";
  const auto paths = app_test_paths(root);
  std::filesystem::create_directories(workspace);

  ava::app::RuntimeOpenOptions open_options;
  open_options.workspace_dir = workspace;
  open_options.current_dir = workspace;
  open_options.paths = paths;
  auto session = ava::app::open_runtime_session(open_options);
  expect(session.has_value(), "provider-backed /compact test opens runtime session");
  if (!session) return;
  auto seeded =
      session->store.append(ava::session::SessionEntry{.id = "entry_user_compact_source",
                                                       .parent_id = "",
                                                       .type = ava::session::EntryType::UserMessage,
                                                       .timestamp = "2026-05-01T00:00:00Z",
                                                       .data_json = "{\"text\":\"Goal: refactor compaction\"}"});
  expect(seeded.has_value(), "provider-backed /compact test seeds source entry");
  auto seeded_reasoning = session->store.append(ava::session::SessionEntry{
      .id = "entry_reasoning_compact_source",
      .parent_id = "",
      .type = ava::session::EntryType::ReasoningBlock,
      .timestamp = "2026-05-01T00:00:01Z",
      .data_json =
          R"({"provider":"anthropic","model":"claude","format":"anthropic_thinking","text":"visible compact reasoning","signature":"compact-secret-signature","redacted_data":"opaque-compaction-redacted","redacted":false})"});
  expect(seeded_reasoning.has_value(), "provider-backed /compact test seeds reasoning source entry");
  auto seeded_redacted_reasoning = session->store.append(ava::session::SessionEntry{
      .id = "entry_redacted_reasoning_compact_source",
      .parent_id = "",
      .type = ava::session::EntryType::ReasoningBlock,
      .timestamp = "2026-05-01T00:00:02Z",
      .data_json =
          R"({"provider":"anthropic","model":"claude","format":"anthropic_thinking","text":"hidden redacted compact reasoning","signature":"redacted-compact-secret","redacted_data":"opaque-hidden-compaction-redacted","redacted": true })"});
  expect(seeded_redacted_reasoning.has_value(), "provider-backed /compact test seeds redacted reasoning source entry");

  const std::string summary =
      "# Goal\nShip compact\n# Constraints / Preferences\nKeep provider backed\n# Decisions\nUse callback\n"
      "# Files Read or Modified\nsrc/ava/app/commands.cpp\n# Unresolved Tasks\nNone noted.\n# Next Steps\nRun tests.";
  const ava::provider::OpenAIProvider provider("https://api.example.test");
  ava::tests::FakeTransport transport({ava::provider::HttpResponse{
      .status_code = 200, .headers = {}, .body = "{\"output_text\":\"" + ava::core::json::escape(summary) + "\"}"}});
  ava::app::RuntimeRunOptions run_options;
  run_options.access_token = "token";

  auto compact = ava::app::run_command(
      *session, ava::app::CommandRequest{
                    .command = "/compact Keep decisions",
                    .compaction_summary_generator = [&](const std::vector<ava::session::SessionEntry>& entries,
                                                        const ava::session::CompactionConfig& config,
                                                        std::string_view instructions, std::size_t estimated_tokens) {
                      return ava::app::generate_compaction_summary(*session, entries, config, instructions,
                                                                   estimated_tokens, provider, transport, run_options);
                    }});
  expect(compact && compact->handled && !compact->output.empty() &&
             compact->output[0].find("compaction summary recorded") != std::string::npos,
         "/compact records a provider-generated summary");
  expect(transport.requests().size() == 1 &&
             transport.requests()[0].body.find("Goal: refactor compaction") != std::string::npos &&
             transport.requests()[0].body.find("visible compact reasoning") != std::string::npos &&
             transport.requests()[0].body.find("hidden redacted compact reasoning") == std::string::npos &&
             transport.requests()[0].body.find("signature_present") != std::string::npos &&
             transport.requests()[0].body.find("compact-secret-signature") == std::string::npos &&
             transport.requests()[0].body.find("redacted-compact-secret") == std::string::npos &&
             transport.requests()[0].body.find("opaque-compaction-redacted") == std::string::npos &&
             transport.requests()[0].body.find("opaque-hidden-compaction-redacted") == std::string::npos &&
             transport.requests()[0].body.find("# Files Read or Modified") != std::string::npos &&
             transport.requests()[0].body.find("Keep decisions") != std::string::npos,
         "provider-backed /compact sends deterministic prompt with sanitized source data and required sections");

  auto entries = session->store.load();
  expect(entries && std::ranges::any_of(*entries,
                                        [&](const ava::session::SessionEntry& entry) {
                                          return entry.type == ava::session::EntryType::Compaction &&
                                                 ava::core::json::string_field(entry.data_json, "summary") == summary &&
                                                 entry.data_json.find("\"summary_unavailable\":false") !=
                                                     std::string::npos;
                                        }),
         "/compact appends returned summary with summary_unavailable false");
}

void test_app_compact_openai_oauth_streaming_summary_success() {
  const auto root = temp_root() / "app-compact-oauth-streaming";
  std::error_code remove_error;
  std::filesystem::remove_all(root, remove_error);
  const auto workspace = root / "workspace";
  const auto paths = app_test_paths(root);
  std::filesystem::create_directories(workspace);

  ava::app::RuntimeOpenOptions open_options;
  open_options.workspace_dir = workspace;
  open_options.current_dir = workspace;
  open_options.paths = paths;
  auto session = ava::app::open_runtime_session(open_options);
  expect(session.has_value(), "OAuth streaming /compact test opens runtime session");
  if (!session) return;

  const std::string summary = "# Goal\nLive compaction works.";
  const std::string sse_body = "data: {\"type\":\"response.output_text.delta\",\"delta\":\"" +
                               ava::core::json::escape(summary) +
                               "\"}\n\n"
                               "data: [DONE]\n\n";
  const ava::provider::OpenAIProvider provider("https://api.example.test");
  ava::tests::FakeTransport transport(
      {ava::provider::HttpResponse{.status_code = 200, .headers = {}, .body = sse_body}});
  ava::app::RuntimeRunOptions run_options;
  run_options.access_token = "token";
  run_options.openai_oauth = true;
  run_options.openai_account_id = "acct_test";

  auto config = ava::session::default_compaction_config();
  auto entries = session->store.load();
  expect(entries.has_value(), "OAuth streaming /compact test loads entries");
  if (!entries) return;
  auto generated =
      ava::app::generate_compaction_summary(*session, *entries, config, "live", 12, provider, transport, run_options);
  expect(generated && *generated == summary, "OAuth streaming compaction summary parses SSE text deltas");
  expect(transport.requests().size() == 1 &&
             transport.requests()[0].url == "https://chatgpt.com/backend-api/codex/responses" &&
             transport.requests()[0].body.find("\"stream\":true") != std::string::npos &&
             transport.requests()[0].body.find("\"store\":false") != std::string::npos,
         "OAuth compaction summary request uses Codex streaming request shape");
}

void test_app_compact_provider_failure_leaves_session_untouched() {
  const auto root = temp_root() / "app-compact-provider-failure";
  std::error_code remove_error;
  std::filesystem::remove_all(root, remove_error);
  const auto workspace = root / "workspace";
  const auto paths = app_test_paths(root);
  std::filesystem::create_directories(workspace);

  ava::app::RuntimeOpenOptions open_options;
  open_options.workspace_dir = workspace;
  open_options.current_dir = workspace;
  open_options.paths = paths;
  auto session = ava::app::open_runtime_session(open_options);
  expect(session.has_value(), "provider failure /compact test opens runtime session");
  if (!session) return;

  const ava::provider::OpenAIProvider provider("https://api.example.test");
  ava::tests::FakeTransport transport(
      {ava::provider::HttpResponse{.status_code = 500, .headers = {}, .body = "{\"error\":{\"message\":\"boom\"}}"}});
  ava::app::RuntimeRunOptions run_options;
  run_options.access_token = "token";

  auto compact = ava::app::run_command(
      *session, ava::app::CommandRequest{
                    .command = "/compact",
                    .compaction_summary_generator = [&](const std::vector<ava::session::SessionEntry>& entries,
                                                        const ava::session::CompactionConfig& config,
                                                        std::string_view instructions, std::size_t estimated_tokens) {
                      return ava::app::generate_compaction_summary(*session, entries, config, instructions,
                                                                   estimated_tokens, provider, transport, run_options);
                    }});
  auto entries = session->store.load();
  expect(compact && compact->handled && !compact->output.empty() &&
             compact->output[0].find("compaction summary request failed with status 500") != std::string::npos &&
             compact->output[0].find("boom") != std::string::npos,
         "provider-backed /compact reports provider failure with status and body details");
  expect(entries && std::ranges::none_of(*entries,
                                         [](const ava::session::SessionEntry& entry) {
                                           return entry.type == ava::session::EntryType::Compaction;
                                         }),
         "provider-backed /compact failure leaves session without compaction entry");
}

void test_app_compact_oversized_summary_leaves_session_untouched() {
  const auto root = temp_root() / "app-compact-oversized";
  std::error_code remove_error;
  std::filesystem::remove_all(root, remove_error);
  const auto workspace = root / "workspace";
  const auto paths = app_test_paths(root);
  std::filesystem::create_directories(workspace);
  std::filesystem::create_directories(paths.ava_config_dir);
  {
    std::ofstream file(paths.compaction_file, std::ios::binary | std::ios::trunc);
    file << "{\"max_summary_bytes\":8}";
  }

  ava::app::RuntimeOpenOptions open_options;
  open_options.workspace_dir = workspace;
  open_options.current_dir = workspace;
  open_options.paths = paths;
  auto session = ava::app::open_runtime_session(open_options);
  expect(session.has_value(), "oversized /compact test opens runtime session");
  if (!session) return;

  auto compact = ava::app::run_command(
      *session, ava::app::CommandRequest{
                    .command = "/compact",
                    .compaction_summary_generator = [](const std::vector<ava::session::SessionEntry>&,
                                                       const ava::session::CompactionConfig&, std::string_view,
                                                       std::size_t) -> ava::core::Result<std::string> {
                      return std::string("this summary is too large");
                    }});
  auto entries = session->store.load();
  expect(compact && compact->handled && !compact->output.empty() &&
             compact->output[0].find("generated compaction summary is too large") != std::string::npos,
         "/compact reports oversized generated summary");
  expect(entries && std::ranges::none_of(*entries,
                                         [](const ava::session::SessionEntry& entry) {
                                           return entry.type == ava::session::EntryType::Compaction;
                                         }),
         "oversized generated summary leaves session without compaction entry");
}

void test_app_compaction_prompt_builder_sections() {
  auto config = ava::session::default_compaction_config();
  const std::vector<ava::session::SessionEntry> entries = {
      ava::session::SessionEntry{.id = "entry_tool",
                                 .parent_id = "",
                                 .type = ava::session::EntryType::ToolResult,
                                 .timestamp = "2026-05-01T00:00:00Z",
                                 .data_json = "{\"name\":\"read\",\"result\":\"src/main.cpp contents\"}"},
      ava::session::SessionEntry{.id = "entry_replay",
                                 .parent_id = "",
                                 .type = ava::session::EntryType::UserMessage,
                                 .timestamp = "2026-05-01T00:00:01Z",
                                 .data_json = "{\"text\":\"duplicated active prompt\",\"internal_replay\":true,"
                                              "\"replay_of\":\"entry_user\"}"}};
  const auto prompt = ava::app::build_compaction_summary_prompt(entries, config, "preserve files", 42);
  expect(prompt.find("# Goal") != std::string::npos &&
             prompt.find("# Constraints / Preferences") != std::string::npos &&
             prompt.find("# Files Read or Modified") != std::string::npos &&
             prompt.find("src/main.cpp") != std::string::npos && prompt.find("preserve files") != std::string::npos &&
             prompt.find("internal_replay") == std::string::npos &&
             prompt.find("duplicated active prompt") == std::string::npos,
         "compaction prompt builder includes source data and skips internal replay messages");
}

void test_app_auto_compaction_appends_summary_and_rebuilds_context() {
  const auto root = temp_root() / "app-auto-compact";
  std::error_code remove_error;
  std::filesystem::remove_all(root, remove_error);
  const auto workspace = root / "workspace";
  const auto paths = app_test_paths(root);
  std::filesystem::create_directories(workspace);

  ava::app::RuntimeOpenOptions open_options;
  open_options.workspace_dir = workspace;
  open_options.current_dir = workspace;
  open_options.paths = paths;
  auto session = ava::app::open_runtime_session(open_options);
  expect(session.has_value(), "auto compaction test opens runtime session");
  if (!session) return;
  session->model.context_window_tokens = 100;

  const std::string old_context = "old context marker " + std::string(420, 'x');
  static_cast<void>(session->store.append(
      ava::session::SessionEntry{.id = "entry_old_user",
                                 .parent_id = "",
                                 .type = ava::session::EntryType::UserMessage,
                                 .timestamp = ava::session::now_timestamp(),
                                 .data_json = "{\"text\":\"" + ava::core::json::escape(old_context) + "\"}"}));
  for (int index = 0; index < 6; ++index) {
    static_cast<void>(session->store.append(
        ava::session::SessionEntry{.id = "entry_recent_" + std::to_string(index),
                                   .parent_id = "",
                                   .type = ava::session::EntryType::AssistantMessage,
                                   .timestamp = ava::session::now_timestamp(),
                                   .data_json = "{\"text\":\"recent filler " + std::to_string(index) + "\"}"}));
  }

  const ava::provider::OpenAIProvider provider("https://api.example.test");
  ava::tests::FakeTransport transport(
      {ava::provider::HttpResponse{.status_code = 200, .headers = {}, .body = "{\"output_text\":\"AUTO SUMMARY\"}"},
       sse_response(final_text_sse("compacted answer"))});
  ava::app::RuntimeRunOptions run_options;
  run_options.access_token = "token";

  auto result = ava::app::run_prompt(*session, "continue after compaction", provider, transport, run_options);
  auto entries = session->store.load();
  const auto compaction = entries ? latest_compaction_entry(*entries) : std::nullopt;
  expect(result && result->final_text == "compacted answer", "auto compaction prompt succeeds");
  expect(transport.requests().size() == 2, "auto compaction performs summary request then provider request");
  expect(
      transport.requests().size() == 2 && transport.requests()[0].body.find("old context marker") != std::string::npos,
      "auto compaction summary request sees pre-compaction context");
  expect(transport.requests().size() == 2 && transport.requests()[1].body.find("AUTO SUMMARY") != std::string::npos &&
             transport.requests()[1].body.find("\"content\":\"continue after compaction\"") != std::string::npos &&
             transport.requests()[1].body.find("old context marker") == std::string::npos,
         "provider request is rebuilt from the new compaction boundary with the active prompt as a normal user turn");
  expect(compaction && compaction->data_json.find("\"trigger\":\"auto\"") != std::string::npos &&
             compaction->data_json.find("\"summary\":\"AUTO SUMMARY\"") != std::string::npos &&
             compaction->data_json.find("\"threshold_tokens\":80") != std::string::npos &&
             compaction->data_json.find("\"keep_recent_messages\":6") != std::string::npos &&
             compaction->data_json.find("\"model\":\"gpt-5.5\"") != std::string::npos,
         "auto compaction entry records trigger, summary, threshold, retention, and model metadata");
}

void test_app_auto_compaction_recent_context_respects_token_budget() {
  const auto root = temp_root() / "app-auto-compact-recent-budget";
  std::error_code remove_error;
  std::filesystem::remove_all(root, remove_error);
  const auto workspace = root / "workspace";
  const auto paths = app_test_paths(root);
  std::filesystem::create_directories(workspace);
  std::filesystem::create_directories(paths.ava_config_dir);
  {
    std::ofstream file(paths.compaction_file, std::ios::binary | std::ios::trunc);
    file << "{\"auto_threshold_tokens\":1,\"keep_recent_tokens\":20,\"keep_recent_messages\":8}";
  }

  ava::app::RuntimeOpenOptions open_options;
  open_options.workspace_dir = workspace;
  open_options.current_dir = workspace;
  open_options.paths = paths;
  auto session = ava::app::open_runtime_session(open_options);
  expect(session.has_value(), "recent context token budget test opens runtime session");
  if (!session) return;
  session->model.context_window_tokens = 1000;
  for (int index = 0; index < 4; ++index) {
    static_cast<void>(session->store.append(ava::session::SessionEntry{
        .id = "entry_budget_" + std::to_string(index),
        .parent_id = "",
        .type = ava::session::EntryType::UserMessage,
        .timestamp = ava::session::now_timestamp(),
        .data_json = "{\"text\":\"budget filler " + std::to_string(index) + " " + std::string(160, 'b') + "\"}"}));
  }

  const ava::provider::OpenAIProvider provider("https://api.example.test");
  ava::tests::FakeTransport transport(
      {ava::provider::HttpResponse{.status_code = 200, .headers = {}, .body = "{\"output_text\":\"BUDGET SUMMARY\"}"},
       sse_response(final_text_sse("budget answer"))});
  ava::app::RuntimeRunOptions run_options;
  run_options.access_token = "token";

  auto result = ava::app::run_prompt(*session, "after budget compaction", provider, transport, run_options);
  auto entries = session->store.load();
  const auto compaction = entries ? latest_compaction_entry(*entries) : std::nullopt;
  const auto recent_context = compaction ? ava::core::json::string_field(compaction->data_json, "recent_context")
                                         : std::optional<std::string>{};
  expect(result && result->final_text == "budget answer", "recent context token budget prompt succeeds");
  expect(recent_context && recent_context->find("recent context tail truncated") != std::string::npos &&
             ava::session::estimate_tokens(*recent_context) <= 20,
         "auto compaction stores recent context bounded by keep_recent_tokens with an explicit marker");
}

void test_app_auto_compaction_recent_context_truncates_utf8_safely() {
  const auto root = temp_root() / "app-auto-compact-recent-utf8";
  std::error_code remove_error;
  std::filesystem::remove_all(root, remove_error);
  const auto workspace = root / "workspace";
  const auto paths = app_test_paths(root);
  std::filesystem::create_directories(workspace);
  std::filesystem::create_directories(paths.ava_config_dir);
  {
    std::ofstream file(paths.compaction_file, std::ios::binary | std::ios::trunc);
    file << "{\"auto_threshold_tokens\":1,\"keep_recent_tokens\":20,\"keep_recent_messages\":8}";
  }

  ava::app::RuntimeOpenOptions open_options;
  open_options.workspace_dir = workspace;
  open_options.current_dir = workspace;
  open_options.paths = paths;
  auto session = ava::app::open_runtime_session(open_options);
  expect(session.has_value(), "recent context UTF-8 truncation test opens runtime session");
  if (!session) return;
  session->model.context_window_tokens = 1000;

  std::string emoji_tail;
  for (int index = 0; index < 80; ++index) emoji_tail += "\xF0\x9F\x98\x80";
  static_cast<void>(session->store.append(
      ava::session::SessionEntry{.id = "entry_utf8_budget",
                                 .parent_id = "",
                                 .type = ava::session::EntryType::UserMessage,
                                 .timestamp = ava::session::now_timestamp(),
                                 .data_json = "{\"text\":\"" + ava::core::json::escape(emoji_tail) + "\"}"}));

  const ava::provider::OpenAIProvider provider("https://api.example.test");
  ava::tests::FakeTransport transport(
      {ava::provider::HttpResponse{.status_code = 200, .headers = {}, .body = "{\"output_text\":\"UTF8 SUMMARY\"}"},
       sse_response(final_text_sse("utf8 answer"))});
  ava::app::RuntimeRunOptions run_options;
  run_options.access_token = "token";

  auto result = ava::app::run_prompt(*session, "after utf8 compaction", provider, transport, run_options);
  auto entries = session->store.load();
  const auto compaction = entries ? latest_compaction_entry(*entries) : std::nullopt;
  const auto recent_context = compaction ? ava::core::json::string_field(compaction->data_json, "recent_context")
                                         : std::optional<std::string>{};
  expect(result && result->final_text == "utf8 answer", "recent context UTF-8 prompt succeeds");
  const auto marker_end = recent_context ? recent_context->find('\n') : std::string::npos;
  const bool suffix_starts_on_codepoint =
      recent_context && marker_end != std::string::npos && marker_end + 1 < recent_context->size() &&
      (static_cast<unsigned char>((*recent_context)[marker_end + 1]) & 0xC0U) != 0x80U;
  expect(recent_context && recent_context->find("recent context tail truncated") != std::string::npos &&
             suffix_starts_on_codepoint,
         "recent context truncation starts UTF-8 suffix on a code point boundary");
}

void test_app_auto_compaction_explicit_zero_disables() {
  const auto root = temp_root() / "app-auto-compact-disabled";
  std::error_code remove_error;
  std::filesystem::remove_all(root, remove_error);
  const auto workspace = root / "workspace";
  const auto paths = app_test_paths(root);
  std::filesystem::create_directories(workspace);
  std::filesystem::create_directories(paths.ava_config_dir);
  {
    std::ofstream file(paths.compaction_file, std::ios::binary | std::ios::trunc);
    file << "{\"auto_threshold_tokens\":0}";
  }

  ava::app::RuntimeOpenOptions open_options;
  open_options.workspace_dir = workspace;
  open_options.current_dir = workspace;
  open_options.paths = paths;
  auto session = ava::app::open_runtime_session(open_options);
  expect(session.has_value(), "disabled auto compaction test opens runtime session");
  if (!session) return;
  session->model.context_window_tokens = 10;
  static_cast<void>(
      session->store.append(ava::session::SessionEntry{.id = "entry_big_user",
                                                       .parent_id = "",
                                                       .type = ava::session::EntryType::UserMessage,
                                                       .timestamp = ava::session::now_timestamp(),
                                                       .data_json = "{\"text\":\"" + std::string(240, 'd') + "\"}"}));

  const ava::provider::OpenAIProvider provider("https://api.example.test");
  ava::tests::FakeTransport transport({sse_response(final_text_sse("no compact answer"))});
  ava::app::RuntimeRunOptions run_options;
  run_options.access_token = "token";

  auto result = ava::app::run_prompt(*session, "do not compact", provider, transport, run_options);
  auto entries = session->store.load();
  expect(result && result->final_text == "no compact answer", "explicit disabled auto compaction prompt succeeds");
  expect(transport.requests().size() == 1, "explicit disabled auto compaction does not call summary provider");
  expect(entries && count_compaction_entries(*entries) == 0, "explicit disabled auto compaction appends no compaction");
}

void test_app_auto_compaction_uses_default_threshold_without_context_window_metadata() {
  const auto root = temp_root() / "app-auto-compact-default-threshold";
  std::error_code remove_error;
  std::filesystem::remove_all(root, remove_error);
  const auto workspace = root / "workspace";
  const auto paths = app_test_paths(root);
  std::filesystem::create_directories(workspace);

  ava::app::RuntimeOpenOptions open_options;
  open_options.workspace_dir = workspace;
  open_options.current_dir = workspace;
  open_options.paths = paths;
  auto session = ava::app::open_runtime_session(open_options);
  expect(session.has_value(), "default threshold auto compaction test opens runtime session");
  if (!session) return;
  session->model.context_window_tokens = std::nullopt;

  const auto config = ava::session::default_compaction_config();
  const auto threshold = ava::session::effective_auto_threshold_tokens(config, std::nullopt);
  static_cast<void>(session->store.append(
      ava::session::SessionEntry{.id = "entry_default_threshold_big",
                                 .parent_id = "",
                                 .type = ava::session::EntryType::UserMessage,
                                 .timestamp = ava::session::now_timestamp(),
                                 .data_json = "{\"text\":\"" + std::string(threshold * 4, 'f') + "\"}"}));

  const ava::provider::OpenAIProvider provider("https://api.example.test");
  ava::tests::FakeTransport transport(
      {ava::provider::HttpResponse{.status_code = 200, .headers = {}, .body = "{\"output_text\":\"DEFAULT SUMMARY\"}"},
       sse_response(final_text_sse("default compact answer"))});
  ava::app::RuntimeRunOptions run_options;
  run_options.access_token = "token";

  auto result = ava::app::run_prompt(*session, "default threshold prompt", provider, transport, run_options);
  auto entries = session->store.load();
  expect(result && result->final_text == "default compact answer", "default threshold auto compaction prompt succeeds");
  expect(transport.requests().size() == 2,
         "default threshold auto compaction performs a summary request before provider request");
  expect(entries && count_compaction_entries(*entries) == 1,
         "default threshold auto compaction appends a compaction entry without model context metadata");
  expect(transport.requests().size() == 2 &&
             transport.requests()[1].body.find("\"content\":\"default threshold prompt\"") != std::string::npos,
         "default threshold auto compaction keeps the active prompt as a normal user message");
}

void test_app_auto_compaction_retries_stale_snapshot_before_append() {
  const auto root = temp_root() / "app-auto-compact-revalidate";
  std::error_code remove_error;
  std::filesystem::remove_all(root, remove_error);
  const auto workspace = root / "workspace";
  const auto paths = app_test_paths(root);
  std::filesystem::create_directories(workspace);

  ava::app::RuntimeOpenOptions open_options;
  open_options.workspace_dir = workspace;
  open_options.current_dir = workspace;
  open_options.paths = paths;
  auto session = ava::app::open_runtime_session(open_options);
  expect(session.has_value(), "auto compaction revalidation test opens runtime session");
  if (!session) return;
  session->model.context_window_tokens = 100;

  static_cast<void>(
      session->store.append(ava::session::SessionEntry{.id = "entry_revalidate_big",
                                                       .parent_id = "",
                                                       .type = ava::session::EntryType::UserMessage,
                                                       .timestamp = ava::session::now_timestamp(),
                                                       .data_json = "{\"text\":\"" + std::string(420, 'r') + "\"}"}));

  const ava::provider::OpenAIProvider provider("https://api.example.test");
  MutatingSummaryTransport transport(
      session->store,
      {ava::provider::HttpResponse{.status_code = 200, .headers = {}, .body = "{\"output_text\":\"STALE SUMMARY\"}"},
       ava::provider::HttpResponse{.status_code = 200, .headers = {}, .body = "{\"output_text\":\"RETRIED SUMMARY\"}"},
       sse_response(final_text_sse("retry after stale"))});
  ava::app::RuntimeRunOptions run_options;
  run_options.access_token = "token";

  auto result = ava::app::run_prompt(*session, "retry stale summary", provider, transport, run_options);
  auto entries = session->store.load();
  expect(result && result->final_text == "retry after stale",
         "auto compaction retries a stale snapshot and continues after a fresh summary");
  expect(transport.requests().size() == 3, "stale auto compaction regenerates one summary before the provider request");
  expect(entries && count_compaction_entries(*entries) == 1,
         "stale auto compaction appends only the summary generated from the fresh snapshot");
  const auto compaction = entries ? latest_compaction_entry(*entries) : std::nullopt;
  expect(compaction && compaction->data_json.find("RETRIED SUMMARY") != std::string::npos &&
             compaction->data_json.find("STALE SUMMARY") == std::string::npos,
         "auto compaction discards the stale summary instead of recording it");
  expect(entries && std::ranges::any_of(*entries,
                                        [](const ava::session::SessionEntry& entry) {
                                          return entry.data_json.find("concurrent change") != std::string::npos;
                                        }),
         "auto compaction retry test introduced a concurrent session change");
}

void test_app_auto_compaction_repeated_stale_snapshot_fails_without_append() {
  const auto root = temp_root() / "app-auto-compact-repeated-stale";
  std::error_code remove_error;
  std::filesystem::remove_all(root, remove_error);
  const auto workspace = root / "workspace";
  const auto paths = app_test_paths(root);
  std::filesystem::create_directories(workspace);

  ava::app::RuntimeOpenOptions open_options;
  open_options.workspace_dir = workspace;
  open_options.current_dir = workspace;
  open_options.paths = paths;
  auto session = ava::app::open_runtime_session(open_options);
  expect(session.has_value(), "repeated stale auto compaction test opens runtime session");
  if (!session) return;
  session->model.context_window_tokens = 100;

  static_cast<void>(
      session->store.append(ava::session::SessionEntry{.id = "entry_repeated_stale_big",
                                                       .parent_id = "",
                                                       .type = ava::session::EntryType::UserMessage,
                                                       .timestamp = ava::session::now_timestamp(),
                                                       .data_json = "{\"text\":\"" + std::string(420, 's') + "\"}"}));

  const ava::provider::OpenAIProvider provider("https://api.example.test");
  MutatingSummaryTransport transport(
      session->store,
      {ava::provider::HttpResponse{.status_code = 200, .headers = {}, .body = "{\"output_text\":\"STALE ONE\"}"},
       ava::provider::HttpResponse{.status_code = 200, .headers = {}, .body = "{\"output_text\":\"STALE TWO\"}"}},
      2);
  ava::app::RuntimeRunOptions run_options;
  run_options.access_token = "token";

  auto result = ava::app::run_prompt(*session, "repeated stale", provider, transport, run_options);
  auto entries = session->store.load();
  expect(!result && result.error().message().find("session changed during context compaction") != std::string::npos,
         "auto compaction returns a clear stale snapshot error after bounded retries are exhausted");
  expect(transport.requests().size() == 2, "repeated stale auto compaction stops after two summary attempts");
  expect(entries && count_compaction_entries(*entries) == 0,
         "repeated stale auto compaction appends no summary from stale snapshots");
}

void test_app_context_overflow_compacts_and_retries_once_successfully() {
  const auto root = temp_root() / "app-context-overflow-retry";
  std::error_code remove_error;
  std::filesystem::remove_all(root, remove_error);
  const auto workspace = root / "workspace";
  const auto paths = app_test_paths(root);
  std::filesystem::create_directories(workspace);

  ava::app::RuntimeOpenOptions open_options;
  open_options.workspace_dir = workspace;
  open_options.current_dir = workspace;
  open_options.paths = paths;
  auto session = ava::app::open_runtime_session(open_options);
  expect(session.has_value(), "context overflow retry test opens runtime session");
  if (!session) return;

  const ava::provider::OpenAIProvider provider("https://api.example.test");
  ava::tests::FakeTransport transport(
      {ava::provider::HttpResponse{.status_code = 400,
                                   .headers = {},
                                   .body = "{\"error\":{\"message\":\"context length exceeded the token limit\"}}"},
       ava::provider::HttpResponse{.status_code = 200, .headers = {}, .body = "{\"output_text\":\"OVERFLOW SUMMARY\"}"},
       sse_response(final_text_sse("retry answer"))});
  ava::app::RuntimeRunOptions run_options;
  run_options.access_token = "token";
  std::vector<ava::app::RuntimeEvent> events;
  run_options.event_sink = [&events](const ava::app::RuntimeEvent& event) {
    events.push_back(event);
    return ava::core::VoidResult{};
  };

  auto result = ava::app::run_prompt(*session, "overflow prompt", provider, transport, run_options);
  auto entries = session->store.load();
  const auto compaction = entries ? latest_compaction_entry(*entries) : std::nullopt;
  expect(result && result->final_text == "retry answer", "context overflow retry succeeds after compaction");
  expect(transport.requests().size() == 3, "context overflow performs original call, compaction, and one retry");
  expect(compaction && compaction->data_json.find("\"trigger\":\"context_overflow\"") != std::string::npos &&
             compaction->data_json.find("OVERFLOW SUMMARY") != std::string::npos,
         "context overflow retry appends a context_overflow compaction summary");
  expect(std::ranges::any_of(events,
                             [](const ava::app::RuntimeEvent& event) {
                               return event.type == ava::app::RuntimeEventType::Retry &&
                                      event.reason == "context_overflow" && event.attempt == 1 &&
                                      event.max_attempts == 1;
                             }) &&
             std::ranges::any_of(events,
                                 [](const ava::app::RuntimeEvent& event) {
                                   return event.type == ava::app::RuntimeEventType::CompactionStart &&
                                          event.trigger == "context_overflow" && event.attempt == 1 &&
                                          event.max_attempts == 2;
                                 }) &&
             std::ranges::any_of(events,
                                 [](const ava::app::RuntimeEvent& event) {
                                   return event.type == ava::app::RuntimeEventType::CompactionEnd &&
                                          event.summary_bytes == std::string("OVERFLOW SUMMARY").size() &&
                                          event.attempt == 1 && event.max_attempts == 2;
                                 }),
         "context overflow retry emits backend lifecycle events for replaying TUI compaction and retry state");
  expect(transport.requests().size() == 3 &&
             transport.requests()[2].body.find("OVERFLOW SUMMARY") != std::string::npos &&
             transport.requests()[2].body.find("\"content\":\"overflow prompt\"") != std::string::npos &&
             count_substrings(transport.requests()[2].body, "overflow prompt") == 1,
         "context overflow retry rebuilds provider context with one active prompt replay");
  const auto recent_context = compaction ? ava::core::json::string_field(compaction->data_json, "recent_context")
                                         : std::optional<std::string>{};
  expect(recent_context && recent_context->find("overflow prompt") == std::string::npos,
         "context overflow compaction excludes active prompts that will be replayed from recent context");
  expect(entries && std::ranges::count_if(*entries, ava::session::is_internal_replay_user_message) == 1,
         "context overflow compaction stores active prompt replay as an internal user message");
  const auto markdown = entries ? ava::session::format_session_markdown(*entries) : std::string{};
  const auto stats = entries ? ava::session::compute_session_stats(*entries) : ava::session::SessionStats{};
  expect(markdown.find("internal_replay") == std::string::npos && count_substrings(markdown, "overflow prompt") == 1 &&
             stats.counts.user_message == 1,
         "consumer-facing export and stats hide internal active prompt replays");
}

void test_app_context_overflow_compaction_failure_leaves_no_partial_entry() {
  const auto root = temp_root() / "app-context-overflow-compaction-fails";
  std::error_code remove_error;
  std::filesystem::remove_all(root, remove_error);
  const auto workspace = root / "workspace";
  const auto paths = app_test_paths(root);
  std::filesystem::create_directories(workspace);

  ava::app::RuntimeOpenOptions open_options;
  open_options.workspace_dir = workspace;
  open_options.current_dir = workspace;
  open_options.paths = paths;
  auto session = ava::app::open_runtime_session(open_options);
  expect(session.has_value(), "context overflow compaction failure test opens runtime session");
  if (!session) return;

  const ava::provider::OpenAIProvider provider("https://api.example.test");
  ava::tests::FakeTransport transport(
      {ava::provider::HttpResponse{.status_code = 400,
                                   .headers = {},
                                   .body = "{\"error\":{\"message\":\"too many tokens for context window\"}}"},
       ava::provider::HttpResponse{
           .status_code = 429, .headers = {}, .body = "{\"error\":{\"message\":\"summary quota exhausted\"}}"}});
  ava::app::RuntimeRunOptions run_options;
  run_options.access_token = "token";

  auto result = ava::app::run_prompt(*session, "overflow then summary fails", provider, transport, run_options);
  auto entries = session->store.load();
  expect(!result && result.error().message() == "context overflow compaction failed",
         "context overflow returns clear compaction failure");
  expect(!result && result.error().format().find("429") != std::string::npos &&
             result.error().format().find("summary quota exhausted") != std::string::npos,
         "context overflow compaction failure preserves provider status and error body details");
  expect(transport.requests().size() == 2, "failed context overflow compaction does not retry provider call");
  expect(entries && count_compaction_entries(*entries) == 0,
         "failed context overflow compaction leaves no partial compaction entry");
}

void test_app_non_overflow_provider_error_does_not_compact_or_retry() {
  const auto root = temp_root() / "app-non-overflow-error";
  std::error_code remove_error;
  std::filesystem::remove_all(root, remove_error);
  const auto workspace = root / "workspace";
  const auto paths = app_test_paths(root);
  std::filesystem::create_directories(workspace);

  ava::app::RuntimeOpenOptions open_options;
  open_options.workspace_dir = workspace;
  open_options.current_dir = workspace;
  open_options.paths = paths;
  auto session = ava::app::open_runtime_session(open_options);
  expect(session.has_value(), "non-overflow provider error test opens runtime session");
  if (!session) return;

  const ava::provider::OpenAIProvider provider("https://api.example.test");
  ava::tests::FakeTransport transport(
      {ava::provider::HttpResponse{.status_code = 500, .headers = {}, .body = "server unavailable"}});
  ava::app::RuntimeRunOptions run_options;
  run_options.access_token = "token";

  auto result = ava::app::run_prompt(*session, "server error", provider, transport, run_options);
  auto entries = session->store.load();
  expect(!result && result.error().message().find("OpenAI HTTP request failed") != std::string::npos,
         "non-overflow provider error is returned");
  expect(transport.requests().size() == 1, "non-overflow provider error does not retry");
  expect(entries && count_compaction_entries(*entries) == 0, "non-overflow provider error does not compact");

  auto context_error = ava::core::Error(ava::core::ErrorCategory::Provider, "too many tokens for context window");
  auto auth_error = ava::core::Error(ava::core::ErrorCategory::Provider, "authentication failed");
  expect(
      ava::provider::is_context_overflow_error(context_error) && !ava::provider::is_context_overflow_error(auth_error),
      "context overflow helper distinguishes token-window errors from unrelated provider errors");
}

void test_app_context_overflow_retry_is_bounded() {
  const auto root = temp_root() / "app-context-overflow-bounded";
  std::error_code remove_error;
  std::filesystem::remove_all(root, remove_error);
  const auto workspace = root / "workspace";
  const auto paths = app_test_paths(root);
  std::filesystem::create_directories(workspace);

  ava::app::RuntimeOpenOptions open_options;
  open_options.workspace_dir = workspace;
  open_options.current_dir = workspace;
  open_options.paths = paths;
  auto session = ava::app::open_runtime_session(open_options);
  expect(session.has_value(), "bounded overflow retry test opens runtime session");
  if (!session) return;

  const ava::provider::OpenAIProvider provider("https://api.example.test");
  ava::tests::FakeTransport transport(
      {ava::provider::HttpResponse{.status_code = 400,
                                   .headers = {},
                                   .body = "{\"error\":{\"message\":\"context length exceeded token limit\"}}"},
       ava::provider::HttpResponse{.status_code = 200, .headers = {}, .body = "{\"output_text\":\"BOUNDED SUMMARY\"}"},
       ava::provider::HttpResponse{.status_code = 400,
                                   .headers = {},
                                   .body = "{\"error\":{\"message\":\"context length exceeded token limit again\"}}"}});
  ava::app::RuntimeRunOptions run_options;
  run_options.access_token = "token";

  auto result = ava::app::run_prompt(*session, "overflow twice", provider, transport, run_options);
  auto entries = session->store.load();
  expect(!result && ava::provider::is_context_overflow_error(result.error()),
         "second context overflow is returned instead of retried indefinitely");
  expect(transport.requests().size() == 3, "context overflow retry is attempted at most once");
  expect(entries && count_compaction_entries(*entries) == 1,
         "bounded context overflow retry appends one compaction entry");
}

void test_app_rpc_parsing_and_response_serialization() {
  auto command = ava::app::parse_rpc_command_line(
      "{\"id\":\"1\",\"type\":\"prompt\",\"message\":\"hello\\nava\",\"instructions\":\"keep\"}");
  expect(command && command->id == "1" && command->type == "prompt" && command->message &&
             *command->message == "hello\nava" && command->instructions && *command->instructions == "keep",
         "RPC parser extracts string envelope fields and unescapes JSON strings");

  auto malformed = ava::app::parse_rpc_command_line("{\"id\":\"bad\",\"type\":\"prompt\"");
  expect(!malformed && malformed.error().category() == ava::core::ErrorCategory::InvalidArgument,
         "RPC parser rejects malformed JSON object lines");

  auto oversized_id =
      ava::app::parse_rpc_command_line("{\"id\":\"" + std::string(257, 'x') + "\",\"type\":\"prompt\"}");
  expect(!oversized_id && oversized_id.error().message() == "RPC identifier is too long",
         "RPC parser rejects oversized request identifiers before queueing");

  const auto success = ava::app::serialize_rpc_success_jsonl("a\"b", "{\"value\":1}");
  expect(success == "{\"id\":\"a\\\"b\",\"type\":\"response\",\"success\":true,\"result\":{\"value\":1}}\n",
         "RPC success response serializes deterministic JSONL with escaped id");

  const auto error = ava::app::serialize_rpc_error_jsonl(
      "e1", ava::core::Error(ava::core::ErrorCategory::InvalidArgument, "bad \"request\""));
  expect(error.find("\"success\":false") != std::string::npos &&
             error.find("bad \\\"request\\\"") != std::string::npos && error.ends_with('\n'),
         "RPC error response serializes JSONL error details");
}

void test_app_rpc_identifier_validation() {
  auto allowed = ava::app::parse_rpc_command_line(
      R"JSON({"id":"rpc.1","type":"set_model","request_id":"req_1","correlation_id":"corr-1",)JSON"
      R"JSON("provider":"openai","model":"openai/gpt-5.5","plugin_id":"com.example.rpc",)JSON"
      R"JSON("name":"demo-server_1","server_id":"demo-server_1"})JSON");
  expect(allowed && allowed->model && *allowed->model == "openai/gpt-5.5" && allowed->plugin_id &&
             *allowed->plugin_id == "com.example.rpc" && allowed->server_id && *allowed->server_id == "demo-server_1",
         "RPC parser allows practical dotted, dashed, underscored, and slash-delimited identifiers");

  auto control = ava::app::parse_rpc_command_line(R"JSON({"id":"bad\u001f","type":"prompt"})JSON");
  expect(!control && control.error().message() == "RPC identifier contains invalid character",
         "RPC parser rejects escaped control bytes in identifiers");

  std::string del_line = R"JSON({"id":"bad)JSON";
  del_line.push_back(static_cast<char>(0x7F));
  del_line += R"JSON(","type":"prompt"})JSON";
  auto del = ava::app::parse_rpc_command_line(del_line);
  expect(!del && del.error().message() == "RPC identifier contains invalid character",
         "RPC parser rejects DEL bytes in identifiers");

  auto whitespace = ava::app::parse_rpc_command_line(R"JSON({"id":"bad id","type":"prompt"})JSON");
  expect(!whitespace && whitespace.error().message() == "RPC identifier contains invalid character",
         "RPC parser rejects ASCII whitespace in identifiers");

  auto metachar =
      ava::app::parse_rpc_command_line(R"JSON({"id":"ok","type":"inspect_plugin","plugin_id":"bad;id"})JSON");
  expect(!metachar && metachar.error().message() == "RPC identifier contains invalid character",
         "RPC parser rejects command-ambiguous metacharacters in slash-command identifiers");

  auto path = ava::app::parse_rpc_command_line(
      R"JSON({"id":"path-ok","type":"validate_plugin","path":"./plugins/bad; path.json"})JSON");
  expect(path && path->path && *path->path == "./plugins/bad; path.json",
         "RPC parser leaves validate_plugin path validation to the plugin path handler");
}

void test_app_rpc_prompt_with_fake_transport_streams_events() {
  const auto root = temp_root() / "app-rpc-prompt";
  std::error_code remove_error;
  std::filesystem::remove_all(root, remove_error);
  const auto workspace = root / "workspace";
  const auto paths = app_test_paths(root);
  std::filesystem::create_directories(workspace);

  ava::app::RuntimeOpenOptions open_options;
  open_options.workspace_dir = workspace;
  open_options.current_dir = workspace;
  open_options.mode = ava::agent::Mode::Build;
  open_options.paths = paths;
  auto session = ava::app::open_runtime_session(open_options);
  expect(session.has_value(), "RPC prompt test opens runtime session");
  if (!session) return;

  const ava::provider::OpenAIProvider provider("https://api.example.test");
  ava::tests::FakeTransport transport({ava::provider::HttpResponse{
      .status_code = 200,
      .headers = {},
      .body = "data: {\"type\":\"response.output_text.delta\",\"delta\":\"rpc answer\"}\n\n"
              "data: [DONE]\n\n",
  }});
  ava::app::RuntimeRunOptions runtime_options;
  runtime_options.access_token = "token";
  BlockingInputBuf input_buffer;
  std::istream in(&input_buffer);
  ThreadSafeStringBuf output_buffer;
  std::ostream out(&output_buffer);
  ava::core::VoidResult result;
  std::jthread rpc_thread(
      [&] { result = ava::app::run_rpc_loop(*session, open_options, provider, transport, runtime_options, in, out); });
  input_buffer.push("{\"id\":\"p1\",\"type\":\"prompt\",\"message\":\"hello rpc\"}\n");
  const bool completed = output_buffer.wait_contains("rpc answer", std::chrono::seconds(2));
  input_buffer.close();
  rpc_thread.join();
  const auto jsonl = output_buffer.str();
  expect(result.has_value(), "RPC prompt loop completes successfully");
  expect(transport.requests().size() == 1 && transport.requests()[0].body.find("hello rpc") != std::string::npos,
         "RPC prompt sends command message through shared runtime");
  expect(jsonl.find("\"name\":\"session_start\"") != std::string::npos &&
             jsonl.find("\"name\":\"assistant_message\"") != std::string::npos &&
             jsonl.find("\"request_id\":\"p1\"") != std::string::npos && completed &&
             jsonl.find("\"id\":\"p1\"") != std::string::npos && jsonl.find("\"success\":true") != std::string::npos &&
             jsonl.find("rpc answer") != std::string::npos,
         "RPC prompt streams runtime event envelopes and ends with a successful response");
}

void test_app_rpc_prompt_streams_provider_deltas_before_final_response() {
  const auto root = temp_root() / "app-rpc-prompt-streaming";
  std::error_code remove_error;
  std::filesystem::remove_all(root, remove_error);
  const auto workspace = root / "workspace";
  const auto paths = app_test_paths(root);
  std::filesystem::create_directories(workspace);

  ava::app::RuntimeOpenOptions open_options;
  open_options.workspace_dir = workspace;
  open_options.current_dir = workspace;
  open_options.mode = ava::agent::Mode::Build;
  open_options.paths = paths;
  auto session = ava::app::open_runtime_session(open_options);
  expect(session.has_value(), "RPC streaming prompt test opens runtime session");
  if (!session) return;

  const ava::provider::OpenAIProvider provider("https://api.example.test");
  ChunkedStreamingTransport transport({"data: {\"type\":\"response.output_text.delta\",\"delta\":\"rpc \"}\n\n",
                                       "data: {\"type\":\"response.output_text.delta\",\"delta\":\"stream\"}\n\n",
                                       "data: [DONE]\n\n"});
  ava::app::RuntimeRunOptions runtime_options;
  runtime_options.access_token = "token";
  BlockingInputBuf input_buffer;
  std::istream in(&input_buffer);
  ThreadSafeStringBuf output_buffer;
  std::ostream out(&output_buffer);
  ava::core::VoidResult result;
  std::jthread rpc_thread(
      [&] { result = ava::app::run_rpc_loop(*session, open_options, provider, transport, runtime_options, in, out); });
  input_buffer.push("{\"id\":\"p1\",\"type\":\"prompt\",\"message\":\"hello rpc stream\"}\n");
  const bool completed = output_buffer.wait_contains("\"success\":true", std::chrono::seconds(2));
  input_buffer.close();
  rpc_thread.join();
  const auto jsonl = output_buffer.str();
  const auto update_position = jsonl.find("\"name\":\"message_update\"");
  const auto final_position = jsonl.find("\"name\":\"assistant_message\"");
  const auto response_position = jsonl.find("\"type\":\"response\"");
  expect(result.has_value(), "RPC streaming prompt loop completes successfully");
  expect(update_position != std::string::npos && final_position != std::string::npos && completed &&
             response_position != std::string::npos && update_position < final_position &&
             final_position < response_position && jsonl.find("rpc stream") != std::string::npos,
         "RPC prompt emits live provider deltas before final assistant event and command response");
}

void test_app_rpc_prompt_refreshes_expired_oauth_before_provider_request() {
  const auto root = temp_root() / "app-rpc-oauth-refresh";
  std::error_code remove_error;
  std::filesystem::remove_all(root, remove_error);
  const auto workspace = root / "workspace";
  const auto paths = app_test_paths(root);
  std::filesystem::create_directories(workspace);
  auto stored = ava::config::store_openai_credential(
      paths, ava::config::OpenAICredential{.type = ava::config::OpenAICredentialType::OAuth,
                                           .access_token = "expired-rpc-access",
                                           .refresh_token = "rpc-refresh",
                                           .expires_at = 100,
                                           .account_id = "acct_old",
                                           .source_path = {}});
  expect(stored.has_value(), "RPC OAuth refresh test stores expired credential");

  ava::app::RuntimeOpenOptions open_options;
  open_options.workspace_dir = workspace;
  open_options.current_dir = workspace;
  open_options.mode = ava::agent::Mode::Build;
  open_options.paths = paths;
  auto session = ava::app::open_runtime_session(open_options);
  expect(session.has_value(), "RPC OAuth refresh test opens runtime session");
  if (!session) return;

  const ava::provider::OpenAIProvider provider("https://api.example.test");
  ava::tests::FakeTransport transport({ava::provider::HttpResponse{
                                           .status_code = 200,
                                           .headers = {},
                                           .body = "{\"access_token\":\"rpc-refreshed-access\","
                                                   "\"refresh_token\":\"rpc-rotated-refresh\","
                                                   "\"expires_in\":3600,\"account_id\":\"acct_rpc\"}",
                                       },
                                       ava::provider::HttpResponse{
                                           .status_code = 200,
                                           .headers = {},
                                           .body = "data: {\"type\":\"response.output_text.delta\",\"delta\":"
                                                   "\"rpc refreshed answer\"}\n\n"
                                                   "data: [DONE]\n\n",
                                       }});
  BlockingInputBuf input_buffer;
  std::istream in(&input_buffer);
  ThreadSafeStringBuf output_buffer;
  std::ostream out(&output_buffer);
  ava::core::VoidResult result;
  std::jthread rpc_thread([&] {
    result =
        ava::app::run_rpc_loop(*session, open_options, provider, transport, ava::app::RuntimeRunOptions{}, in, out);
  });
  input_buffer.push("{\"id\":\"p1\",\"type\":\"prompt\",\"message\":\"hello refreshed rpc\"}\n");
  const bool completed = output_buffer.wait_contains("\"success\":true", std::chrono::seconds(2));
  input_buffer.close();
  rpc_thread.join();
  const auto jsonl = output_buffer.str();
  expect(result.has_value(), "RPC prompt with expired OAuth completes after refresh");
  expect(transport.requests().size() == 2 && transport.requests()[0].url == "https://auth.openai.com/oauth/token" &&
             transport.requests()[1].headers.at("Authorization") == "Bearer rpc-refreshed-access" &&
             transport.requests()[1].headers.at("ChatGPT-Account-Id") == "acct_rpc",
         "RPC prompt refreshes OAuth before sending provider request");
  expect(completed && jsonl.find("rpc refreshed answer") != std::string::npos &&
             jsonl.find("\"success\":true") != std::string::npos,
         "RPC prompt returns refreshed OAuth provider response");
  auto persisted = ava::config::load_openai_credential(paths);
  expect(persisted && persisted->has_value() && (*persisted)->access_token == "rpc-refreshed-access" &&
             (*persisted)->refresh_token == "rpc-rotated-refresh",
         "RPC OAuth preflight persists refreshed credential before provider startup");
}

void test_app_rpc_malformed_line_recovery_and_unknown_command() {
  const auto root = temp_root() / "app-rpc-recovery";
  std::error_code remove_error;
  std::filesystem::remove_all(root, remove_error);
  const auto workspace = root / "workspace";
  const auto paths = app_test_paths(root);
  std::filesystem::create_directories(workspace);

  ava::app::RuntimeOpenOptions open_options;
  open_options.workspace_dir = workspace;
  open_options.current_dir = workspace;
  open_options.paths = paths;
  auto session = ava::app::open_runtime_session(open_options);
  expect(session.has_value(), "RPC recovery test opens runtime session");
  if (!session) return;

  const ava::provider::OpenAIProvider provider("https://api.example.test");
  ava::tests::FakeTransport transport({});
  std::istringstream in(
      "not json\n{\"id\":\"s1\",\"type\":\"get_state\"}\n"
      "{\"id\":\"u1\",\"type\":\"unknown\"}\n");
  std::ostringstream out;
  auto result =
      ava::app::run_rpc_loop(*session, open_options, provider, transport, ava::app::RuntimeRunOptions{}, in, out);
  const auto jsonl = out.str();
  expect(result.has_value(), "RPC loop continues after malformed and unknown commands");
  expect(std::count(jsonl.begin(), jsonl.end(), '\n') == 3 && jsonl.find("\"id\":\"\"") != std::string::npos &&
             jsonl.find("malformed RPC JSON object") != std::string::npos &&
             jsonl.find("\"id\":\"s1\"") != std::string::npos && jsonl.find("\"session_id\":\"") != std::string::npos &&
             jsonl.find("\"id\":\"u1\"") != std::string::npos &&
             jsonl.find("unknown RPC command type") != std::string::npos,
         "RPC loop writes error responses and recovers for subsequent JSONL records");
}

void test_app_rpc_state_list_sessions_and_open_session() {
  const auto root = temp_root() / "app-rpc-state";
  std::error_code remove_error;
  std::filesystem::remove_all(root, remove_error);
  const auto workspace = root / "workspace";
  const auto paths = app_test_paths(root);
  std::filesystem::create_directories(workspace);

  ava::app::RuntimeOpenOptions open_options;
  open_options.workspace_dir = workspace;
  open_options.current_dir = workspace;
  open_options.paths = paths;
  auto first = ava::app::open_runtime_session(open_options);
  auto second = ava::app::open_runtime_session(open_options);
  expect(first.has_value() && second.has_value(), "RPC state test opens multiple sessions");
  if (!first || !second) return;
  const auto first_id = first->store.session_id();
  const auto second_id = second->store.session_id();

  const ava::provider::OpenAIProvider provider("https://api.example.test");
  ava::tests::FakeTransport transport({});
  std::istringstream in(
      "{\"id\":\"state\",\"type\":\"get_state\"}\n"
      "{\"id\":\"list\",\"type\":\"list_sessions\"}\n"
      "{\"id\":\"open\",\"type\":\"open_session\",\"session_id\":\"" +
      first_id + "\"}\n");
  std::ostringstream out;
  auto result =
      ava::app::run_rpc_loop(*second, open_options, provider, transport, ava::app::RuntimeRunOptions{}, in, out);
  const auto jsonl = out.str();
  expect(result.has_value(), "RPC state/list/open loop completes successfully");
  expect(jsonl.find("\"id\":\"state\"") != std::string::npos && jsonl.find(second_id) != std::string::npos &&
             jsonl.find("\"id\":\"list\"") != std::string::npos && jsonl.find(first_id) != std::string::npos &&
             jsonl.find("\"id\":\"open\"") != std::string::npos,
         "RPC state, list_sessions, and open_session return session metadata");
  expect(second->store.session_id() == first_id, "RPC open_session switches the active runtime session");
}

void test_app_runtime_model_switch_persists_and_reopens() {
  const auto root = temp_root() / "app-runtime-model-switch";
  std::error_code remove_error;
  std::filesystem::remove_all(root, remove_error);
  const auto workspace = root / "workspace";
  const auto paths = app_test_paths(root);
  std::filesystem::create_directories(workspace);
  std::filesystem::create_directories(paths.ava_config_dir);
  {
    std::ofstream file(paths.models_file, std::ios::binary | std::ios::trunc);
    file << R"JSON({
      "default_provider":"openai",
      "default_model":"gpt-5.5",
      "models":[{
        "provider":"anthropic",
        "id":"claude-test",
        "name":"Claude Test",
        "family":"claude-test",
        "api_family":"anthropic_messages",
        "context_window_tokens":999,
        "max_output_tokens":123,
        "supports_tools":false,
        "supports_streaming":true,
        "supports_reasoning":false,
        "reports_usage":true,
        "input_modalities":["text"],
        "output_modalities":["text"],
        "compatibility_quirks":["test_quirk"]
      }]
    })JSON";
  }

  ava::app::RuntimeOpenOptions open_options;
  open_options.workspace_dir = workspace;
  open_options.current_dir = workspace;
  open_options.paths = paths;
  auto session = ava::app::open_runtime_session(open_options);
  expect(session.has_value(), "runtime model switch test opens runtime session");
  if (!session) return;
  const auto session_id = session->store.session_id();

  auto model = ava::app::resolve_runtime_model(paths, "anthropic", "claude-test");
  expect(model.has_value(), "runtime resolves configured Anthropic model");
  if (!model) return;
  auto switched = ava::app::switch_runtime_model(*session, *model);
  expect(switched.has_value() && *switched, "runtime model switch reports a change");
  expect(session->model.provider_id == "anthropic" && session->model.model_id == "claude-test",
         "runtime model switch updates active session model");

  auto entries = session->store.load();
  expect(entries.has_value(), "runtime model switch loads session entries");
  bool saw_model_change = false;
  if (entries) {
    for (const auto& entry : *entries) {
      saw_model_change =
          saw_model_change || (entry.type == ava::session::EntryType::ModelChange &&
                               entry.data_json.find("\"previous_provider\":\"openai\"") != std::string::npos &&
                               entry.data_json.find("\"provider\":\"anthropic\"") != std::string::npos);
    }
  }
  expect(saw_model_change, "runtime model switch appends model_change entry");

  auto appended_escaped_model_change = session->store.append(ava::session::SessionEntry{
      .id = ava::core::make_id("entry"),
      .parent_id = "",
      .type = ava::session::EntryType::ModelChange,
      .timestamp = ava::session::now_timestamp(),
      .data_json =
          R"JSON({"previous_provider":"anthropic","previous_model":"claude-test","provider":"anthropic","model":"claude-test","display_name":"Claude Test","family":"claude-test","api_family":"anthropic_messages","input_modalities":["text"],"output_modalities":["text"],"reasoning_levels":[],"compatibility_quirks":["test_quirk","\uD83D\uDE00"],"context_window_tokens":999,"max_output_tokens":123,"supports_tools":false,"supports_streaming":true,"supports_reasoning":false,"reports_usage":true})JSON"});
  expect(appended_escaped_model_change.has_value(), "runtime model switch test seeds escaped unicode metadata");

  ava::app::RuntimeOpenOptions reopen_options = open_options;
  reopen_options.requested_session_id = session_id;
  std::filesystem::remove(paths.models_file, remove_error);
  auto reopened = ava::app::open_runtime_session(reopen_options);
  expect(reopened.has_value(), "runtime model switch reopens persisted session");
  expect(reopened && reopened->model.provider_id == "anthropic" && reopened->model.model_id == "claude-test",
         "runtime reopen restores latest persisted model_change");
  bool restored_emoji_quirk = false;
  if (reopened) {
    const auto emoji_quirk = std::string("\xF0\x9F\x98\x80");
    restored_emoji_quirk = std::ranges::find(reopened->model.compatibility_quirks, emoji_quirk) !=
                           reopened->model.compatibility_quirks.end();
  }
  expect(restored_emoji_quirk, "runtime reopen decodes escaped supplementary-plane metadata");
  if (reopened) {
    const ava::provider::OpenAIProvider provider("https://api.example.test");
    ava::tests::FakeTransport transport({});
    std::istringstream in("{\"id\":\"list\",\"type\":\"list_models\"}\n");
    std::ostringstream out;
    auto result =
        ava::app::run_rpc_loop(*reopened, reopen_options, provider, transport, ava::app::RuntimeRunOptions{}, in, out);
    const auto jsonl = out.str();
    const auto restored_position = jsonl.find("\"model\":\"claude-test\"");
    expect(result.has_value() && restored_position != std::string::npos,
           "RPC list_models includes restored removed current model");
    expect(restored_position != std::string::npos &&
               jsonl.find("\"selectable\":false", restored_position) != std::string::npos,
           "RPC list_models marks restored removed current model as not selectable");
    expect(restored_position != std::string::npos &&
               jsonl.find("\"context_window_tokens\":999", restored_position) != std::string::npos &&
               jsonl.find("\"max_output_tokens\":123", restored_position) != std::string::npos &&
               jsonl.find("\"supports_streaming\":true", restored_position) != std::string::npos &&
               jsonl.find("\"supports_tools\":false", restored_position) != std::string::npos &&
               jsonl.find("\"reports_usage\":true", restored_position) != std::string::npos &&
               jsonl.find("test_quirk", restored_position) != std::string::npos,
           "RPC list_models preserves capability metadata for restored removed models");
  }
}

void test_app_runtime_model_switch_rejects_incompatible_history() {
  const auto root = temp_root() / "app-runtime-model-switch-compatibility";
  std::error_code remove_error;
  std::filesystem::remove_all(root, remove_error);
  const auto workspace = root / "workspace";
  const auto paths = app_test_paths(root);
  std::filesystem::create_directories(workspace);
  std::filesystem::create_directories(paths.ava_config_dir);
  {
    std::ofstream file(paths.models_file, std::ios::binary | std::ios::trunc);
    file << R"JSON({
      "default_provider":"openai",
      "default_model":"gpt-5.5",
      "models":[{
        "provider":"openai",
        "id":"no-tools",
        "name":"No Tools",
        "family":"test",
        "api_family":"openai_responses",
        "supports_streaming":true,
        "input_modalities":["text"],
        "output_modalities":["text"]
      },{
        "provider":"anthropic",
        "id":"claude-replay",
        "name":"Claude Replay",
        "family":"claude-test",
        "api_family":"anthropic_messages",
        "supports_tools":true,
        "supports_streaming":true,
        "input_modalities":["text"],
        "output_modalities":["text"]
      }]
    })JSON";
  }

  ava::app::RuntimeOpenOptions open_options;
  open_options.workspace_dir = workspace;
  open_options.current_dir = workspace;
  open_options.paths = paths;
  auto session = ava::app::open_runtime_session(open_options);
  expect(session.has_value(), "runtime model switch compatibility test opens runtime session");
  if (!session) return;

  auto appended_tool_call = session->store.append(ava::session::SessionEntry{.id = ava::core::make_id("entry"),
                                                                             .parent_id = "",
                                                                             .type = ava::session::EntryType::ToolCall,
                                                                             .timestamp = ava::session::now_timestamp(),
                                                                             .data_json = "{\"call_id\":\"call_1\","
                                                                                          "\"name\":\"read_file\","
                                                                                          "\"arguments\":{}}"});
  auto appended_tool_result =
      session->store.append(ava::session::SessionEntry{.id = ava::core::make_id("entry"),
                                                       .parent_id = "",
                                                       .type = ava::session::EntryType::ToolResult,
                                                       .timestamp = ava::session::now_timestamp(),
                                                       .data_json = "{\"call_id\":\"call_1\",\"content\":\"ok\"}"});
  expect(appended_tool_call.has_value() && appended_tool_result.has_value(),
         "model switch compatibility test seeds tool history");

  auto no_tools_model = ava::app::resolve_runtime_model(paths, "openai", "no-tools");
  expect(no_tools_model.has_value(), "runtime resolves no-tools model");
  if (!no_tools_model) return;
  auto rejected_tools = ava::app::switch_runtime_model(*session, *no_tools_model);
  expect(!rejected_tools.has_value(), "runtime rejects switch to model without tool support after tool history");
  expect(!rejected_tools && rejected_tools.error().format().find("tool support") != std::string::npos,
         "runtime tool-history switch error explains missing tool support");
  expect(session->model.provider_id == "openai" && session->model.model_id == "gpt-5.5",
         "rejected tool-history switch leaves active model unchanged");

  auto appended_reasoning =
      session->store.append(ava::session::SessionEntry{.id = ava::core::make_id("entry"),
                                                       .parent_id = "",
                                                       .type = ava::session::EntryType::ReasoningBlock,
                                                       .timestamp = ava::session::now_timestamp(),
                                                       .data_json = "{\"provider\":\"anthropic\","
                                                                    "\"model\":\"claude-sonnet-4-5\","
                                                                    "\"format\":\"anthropic_thinking\","
                                                                    "\"text\":\"visible reasoning\","
                                                                    "\"signature\":\"sig-1\"}"});
  expect(appended_reasoning.has_value(), "model switch compatibility test seeds reasoning history");

  auto anthropic_replay = ava::app::resolve_runtime_model(paths, "anthropic", "claude-replay");
  expect(anthropic_replay.has_value(), "runtime resolves Anthropic replay model");
  if (!anthropic_replay) return;
  auto switched_anthropic = ava::app::switch_runtime_model(*session, *anthropic_replay);
  expect(switched_anthropic.has_value() && *switched_anthropic,
         "runtime allows switch to Anthropic model that can replay Anthropic reasoning");
  expect(session->model.provider_id == "anthropic" && session->model.model_id == "claude-replay",
         "compatible reasoning switch updates active model");

  auto kimi_model = ava::app::resolve_runtime_model(paths, "kimi", "kimi-k2-thinking");
  expect(kimi_model.has_value(), "runtime resolves Kimi model");
  if (!kimi_model) return;
  auto rejected_reasoning = ava::app::switch_runtime_model(*session, *kimi_model);
  expect(!rejected_reasoning.has_value(), "runtime rejects incompatible reasoning provider switch");
  expect(!rejected_reasoning && rejected_reasoning.error().format().find("anthropic_thinking") != std::string::npos,
         "runtime reasoning switch error includes incompatible reasoning format");
  expect(session->model.provider_id == "anthropic" && session->model.model_id == "claude-replay",
         "rejected reasoning switch leaves active model unchanged");

  auto appended_compaction =
      session->store.append(ava::session::SessionEntry{.id = ava::core::make_id("entry"),
                                                       .parent_id = "",
                                                       .type = ava::session::EntryType::Compaction,
                                                       .timestamp = ava::session::now_timestamp(),
                                                       .data_json = "{\"summary\":\"old history\"}"});
  expect(appended_compaction.has_value(), "model switch compatibility test seeds compaction boundary");
  auto switched_no_tools_after_compaction = ava::app::switch_runtime_model(*session, *no_tools_model);
  expect(switched_no_tools_after_compaction.has_value() && *switched_no_tools_after_compaction,
         "runtime ignores pre-compaction native history for switch compatibility");
  expect(session->model.provider_id == "openai" && session->model.model_id == "no-tools",
         "post-compaction switch updates active model");

  auto appended_kimi_reasoning =
      session->store.append(ava::session::SessionEntry{.id = ava::core::make_id("entry"),
                                                       .parent_id = "",
                                                       .type = ava::session::EntryType::ReasoningBlock,
                                                       .timestamp = ava::session::now_timestamp(),
                                                       .data_json = "{\"provider\":\"kimi\","
                                                                    "\"model\":\"kimi-k2-thinking\","
                                                                    "\"format\":\"reasoning_content\","
                                                                    "\"text\":\"compatible kimi reasoning\"}"});
  expect(appended_kimi_reasoning.has_value(), "model switch compatibility test seeds Kimi reasoning history");

  auto switched_kimi = ava::app::switch_runtime_model(*session, *kimi_model);
  expect(switched_kimi.has_value() && *switched_kimi,
         "runtime allows switch to Kimi model with explicit reasoning preservation support");
  expect(session->model.provider_id == "kimi" && session->model.model_id == "kimi-k2-thinking",
         "Kimi reasoning-compatible switch updates active model");

  auto moonshot_model = ava::app::resolve_runtime_model(paths, "moonshot", "kimi-k2.6");
  expect(moonshot_model.has_value(), "runtime resolves Moonshot model");
  if (!moonshot_model) return;
  auto rejected_moonshot = ava::app::switch_runtime_model(*session, *moonshot_model);
  expect(!rejected_moonshot.has_value(), "runtime rejects reasoning_content switch without preservation quirk");
  expect(session->model.provider_id == "kimi" && session->model.model_id == "kimi-k2-thinking",
         "rejected Moonshot reasoning switch leaves active model unchanged");

  auto entries = session->store.load();
  expect(entries.has_value(), "model switch compatibility test reloads entries");
  if (entries) {
    const auto model_changes = std::ranges::count_if(
        *entries, [](const auto& entry) { return entry.type == ava::session::EntryType::ModelChange; });
    expect(model_changes == 3, "rejected model switches do not append model_change entries");
  }
}

void test_app_runtime_reasoning_selection_persists_and_requests() {
  const auto root = temp_root() / "app-runtime-reasoning-selection";
  std::error_code remove_error;
  std::filesystem::remove_all(root, remove_error);
  const auto workspace = root / "workspace";
  const auto paths = app_test_paths(root);
  std::filesystem::create_directories(workspace);
  std::filesystem::create_directories(paths.ava_config_dir);
  {
    std::ofstream file(paths.models_file, std::ios::binary | std::ios::trunc);
    file << R"JSON({
      "default_provider":"openai",
      "default_model":"gpt-5.5",
      "models":[{
        "provider":"openai",
        "id":"no-reasoning-levels",
        "name":"No Reasoning Levels",
        "family":"test",
        "api_family":"openai_responses",
        "supports_tools":true,
        "supports_streaming":true,
        "supports_reasoning":true,
        "input_modalities":["text"],
        "output_modalities":["text"]
      },{
        "provider":"anthropic",
        "id":"claude-default-max",
        "name":"Claude Default Max",
        "family":"claude",
        "api_family":"anthropic_messages",
        "supports_tools":true,
        "supports_streaming":true,
        "supports_reasoning":true,
        "reasoning_levels":["enabled"],
        "input_modalities":["text"],
        "output_modalities":["text"]
      },{
        "provider":"anthropic-proxy",
        "id":"claude-proxy",
        "name":"Claude Proxy",
        "family":"claude",
        "api_family":"anthropic_messages",
        "max_output_tokens":8192,
        "supports_tools":true,
        "supports_streaming":true,
        "supports_reasoning":true,
        "reasoning_levels":["enabled"],
        "input_modalities":["text"],
        "output_modalities":["text"]
      }]
    })JSON";
  }

  ava::app::RuntimeOpenOptions open_options;
  open_options.workspace_dir = workspace;
  open_options.current_dir = workspace;
  open_options.paths = paths;
  auto session = ava::app::open_runtime_session(open_options);
  expect(session.has_value(), "runtime reasoning test opens runtime session");
  if (!session) return;
  const auto session_id = session->store.session_id();

  auto selected = ava::app::set_runtime_reasoning(
      *session, ava::app::RuntimeReasoningSelection{.level = " low ", .budget_tokens = std::nullopt, .display = ""});
  expect(selected.has_value() && *selected && session->reasoning && session->reasoning->level == "low",
         "runtime reasoning selection validates, normalizes, and updates state");

  auto duplicate = ava::app::set_runtime_reasoning(
      *session, ava::app::RuntimeReasoningSelection{.level = "low", .budget_tokens = std::nullopt, .display = ""});
  expect(duplicate.has_value() && !*duplicate, "runtime reasoning selection is idempotent when unchanged");

  auto invalid = ava::app::set_runtime_reasoning(
      *session, ava::app::RuntimeReasoningSelection{.level = "ultra", .budget_tokens = std::nullopt, .display = ""});
  expect(!invalid.has_value(), "runtime reasoning selection rejects unsupported model levels");

  const ava::provider::OpenAIProvider provider("https://api.example.test");
  ava::tests::FakeTransport transport({ava::provider::HttpResponse{
      .status_code = 200,
      .headers = {},
      .body = "data: {\"type\":\"response.output_text.delta\",\"delta\":\"reasoned answer\"}\n\n"
              "data: [DONE]\n\n",
  }});
  ava::app::RuntimeRunOptions run_options;
  run_options.access_token = "token";
  auto result = ava::app::run_prompt(*session, "use reasoning", provider, transport, run_options);
  expect(result && result->final_text == "reasoned answer", "runtime reasoning prompt completes");
  expect(transport.requests().size() == 1, "runtime reasoning test sends one provider request");
  if (!transport.requests().empty()) {
    expect(transport.requests()[0].body.find("\"reasoning\"") != std::string::npos &&
               transport.requests()[0].body.find("\"effort\":\"low\"") != std::string::npos &&
               transport.requests()[0].body.find("\"summary\":\"auto\"") != std::string::npos,
           "runtime reasoning selection is sent to the provider request with visible summary request");
  }

  auto entries = session->store.load();
  expect(entries.has_value(), "runtime reasoning test reloads session entries");
  if (entries) {
    const auto reasoning_changes = std::ranges::count_if(
        *entries, [](const auto& entry) { return entry.type == ava::session::EntryType::ReasoningChange; });
    expect(reasoning_changes == 1, "runtime reasoning selection appends one durable reasoning_change entry");
  }

  ava::app::RuntimeOpenOptions reopen_options = open_options;
  reopen_options.requested_session_id = session_id;
  auto reopened = ava::app::open_runtime_session(reopen_options);
  expect(reopened.has_value() && reopened->reasoning && reopened->reasoning->level == "low",
         "runtime reopen restores latest reasoning selection");

  auto cleared = ava::app::set_runtime_reasoning(*session, std::nullopt);
  expect(cleared.has_value() && *cleared && !session->reasoning, "runtime reasoning selection can be cleared");

  auto reselected = ava::app::set_runtime_reasoning(
      *session, ava::app::RuntimeReasoningSelection{.level = "low", .budget_tokens = std::nullopt, .display = ""});
  expect(reselected.has_value() && *reselected, "runtime reasoning test re-enables reasoning before switch boundary");
  auto kimi_model = ava::app::resolve_runtime_model(paths, "kimi", "kimi-k2-thinking");
  auto openai_model = ava::app::resolve_runtime_model(paths, "openai", "gpt-5.5");
  expect(kimi_model.has_value() && openai_model.has_value(), "runtime reasoning test resolves switch boundary models");
  if (kimi_model && openai_model) {
    auto switched_away = ava::app::switch_runtime_model(*session, *kimi_model);
    expect(switched_away.has_value() && *switched_away, "runtime reasoning test switches to Kimi model");
    auto kimi_budget = ava::app::set_runtime_reasoning(
        *session,
        ava::app::RuntimeReasoningSelection{.level = "enabled", .budget_tokens = 1024, .display = "summarized"});
    expect(!kimi_budget.has_value() &&
               kimi_budget.error().format().find("Kimi reasoning supports level only") != std::string::npos,
           "runtime reasoning selection rejects unsupported OpenAI-compatible budget/display controls");
    auto switched_back = ava::app::switch_runtime_model(*session, *openai_model);
    expect(switched_back.has_value() && *switched_back && !session->reasoning,
           "runtime model switches clear active reasoning selection");
    auto reopened_after_switch = ava::app::open_runtime_session(reopen_options);
    expect(reopened_after_switch.has_value() && !reopened_after_switch->reasoning,
           "runtime reopen does not resurrect reasoning across model_change boundaries");
  }

  auto no_levels_model = ava::app::resolve_runtime_model(paths, "openai", "no-reasoning-levels");
  expect(no_levels_model.has_value(), "runtime reasoning test resolves no-level custom model");
  if (no_levels_model) {
    auto switched = ava::app::switch_runtime_model(*session, *no_levels_model);
    expect(switched.has_value() && *switched, "runtime reasoning test switches to no-level custom model");
    auto no_level_selection = ava::app::set_runtime_reasoning(
        *session, ava::app::RuntimeReasoningSelection{.level = "low", .budget_tokens = std::nullopt, .display = ""});
    expect(!no_level_selection.has_value() &&
               no_level_selection.error().format().find("supported reasoning levels") != std::string::npos,
           "runtime reasoning selection rejects models without declared reasoning levels");
  }

  auto anthropic_default_max = ava::app::resolve_runtime_model(paths, "anthropic", "claude-default-max");
  expect(anthropic_default_max.has_value(), "runtime reasoning test resolves Anthropic default max model");
  if (anthropic_default_max) {
    auto switched = ava::app::switch_runtime_model(*session, *anthropic_default_max);
    expect(switched.has_value() && *switched, "runtime reasoning test switches to Anthropic default max model");
    auto over_budget = ava::app::set_runtime_reasoning(
        *session,
        ava::app::RuntimeReasoningSelection{.level = "enabled", .budget_tokens = 4096, .display = "summarized"});
    expect(!over_budget.has_value() && over_budget.error().format().find(
                                           "reasoning budget must be below max output tokens") != std::string::npos,
           "runtime reasoning selection validates Anthropic budget against provider default max tokens");
  }

  auto proxy_registry = ava::config::load_model_registry(paths);
  expect(proxy_registry.has_value(), "runtime reasoning test loads registry for custom Anthropic-compatible model");
  auto anthropic_proxy = proxy_registry ? ava::config::find_model(*proxy_registry, "anthropic-proxy", "claude-proxy")
                                        : std::optional<ava::config::ModelInfo>{};
  expect(anthropic_proxy.has_value(), "runtime reasoning test finds custom Anthropic-compatible model");
  if (anthropic_proxy) {
    session->model = *anthropic_proxy;
    session->reasoning.reset();
    auto cycled = ava::app::cycle_runtime_reasoning(*session);
    expect(cycled.has_value() && session->reasoning && session->reasoning->level == "enabled" &&
               session->reasoning->budget_tokens && *session->reasoning->budget_tokens == 4096,
           "runtime reasoning cycling uses API-family fallback profile for custom Anthropic-compatible models");
    auto missing_budget = ava::app::set_runtime_reasoning(
        *session,
        ava::app::RuntimeReasoningSelection{.level = "enabled", .budget_tokens = std::nullopt, .display = ""});
    expect(!missing_budget.has_value() &&
               missing_budget.error().format().find("Anthropic-proxy enabled reasoning requires budget_tokens") !=
                   std::string::npos,
           "runtime reasoning validation labels missing-budget errors with the custom provider id");
    auto too_large_budget = ava::app::set_runtime_reasoning(
        *session, ava::app::RuntimeReasoningSelection{.level = "enabled", .budget_tokens = 8192, .display = ""});
    expect(!too_large_budget.has_value() &&
               too_large_budget.error().format().find("reasoning budget must be below max output tokens") !=
                   std::string::npos,
           "runtime reasoning validation applies fallback budget limits to custom providers");
  }
}

void test_app_rpc_model_commands() {
  const auto root = temp_root() / "app-rpc-model-commands";
  std::error_code remove_error;
  std::filesystem::remove_all(root, remove_error);
  const auto workspace = root / "workspace";
  const auto paths = app_test_paths(root);
  std::filesystem::create_directories(workspace);

  ava::app::RuntimeOpenOptions open_options;
  open_options.workspace_dir = workspace;
  open_options.current_dir = workspace;
  open_options.paths = paths;
  auto session = ava::app::open_runtime_session(open_options);
  expect(session.has_value(), "RPC model command test opens runtime session");
  if (!session) return;

  const ava::provider::OpenAIProvider provider("https://api.example.test");
  ava::tests::FakeTransport transport({});
  std::istringstream in(
      "{\"id\":\"list\",\"type\":\"list_models\"}\n"
      "{\"id\":\"set\",\"type\":\"set_model\",\"provider\":\"anthropic\","
      "\"model\":\"claude-sonnet-4-5\"}\n"
      "{\"id\":\"state\",\"type\":\"get_state\"}\n"
      "{\"id\":\"stats\",\"type\":\"get_session_stats\"}\n"
      "{\"id\":\"cycle\",\"type\":\"cycle_model\"}\n");
  std::ostringstream out;
  auto result =
      ava::app::run_rpc_loop(*session, open_options, provider, transport, ava::app::RuntimeRunOptions{}, in, out);
  const auto jsonl = out.str();
  expect(result.has_value(), "RPC model command loop completes successfully");
  expect(jsonl.find("\"id\":\"list\"") != std::string::npos && jsonl.find("\"models\"") != std::string::npos &&
             jsonl.find("claude-sonnet-4-5") != std::string::npos,
         "RPC list_models returns configured model catalog");
  expect(jsonl.find("\"id\":\"set\"") != std::string::npos &&
             jsonl.find("\"provider\":\"anthropic\"") != std::string::npos &&
             jsonl.find("\"model\":\"claude-sonnet-4-5\"") != std::string::npos,
         "RPC set_model returns updated Anthropic state");
  expect(jsonl.find("\"id\":\"state\"") != std::string::npos &&
             jsonl.find("\"model\":\"claude-sonnet-4-5\"") != std::string::npos,
         "RPC get_state reflects selected model after set_model");
  expect(jsonl.find("\"id\":\"stats\"") != std::string::npos && jsonl.find("\"model_change\":1") != std::string::npos,
         "RPC get_session_stats reports model_change count");
  expect(
      jsonl.find("\"id\":\"cycle\"") != std::string::npos && jsonl.find("\"provider\":\"kimi\"") != std::string::npos,
      "RPC cycle_model advances to the next configured provider model");
  expect(session->model.provider_id == "kimi", "RPC cycle_model updates active session model");
}

void test_app_rpc_reasoning_commands() {
  const auto root = temp_root() / "app-rpc-reasoning-commands";
  std::error_code remove_error;
  std::filesystem::remove_all(root, remove_error);
  const auto workspace = root / "workspace";
  const auto paths = app_test_paths(root);
  std::filesystem::create_directories(workspace);

  ava::app::RuntimeOpenOptions open_options;
  open_options.workspace_dir = workspace;
  open_options.current_dir = workspace;
  open_options.paths = paths;
  auto session = ava::app::open_runtime_session(open_options);
  expect(session.has_value(), "RPC reasoning command test opens runtime session");
  if (!session) return;

  const ava::provider::OpenAIProvider provider("https://api.example.test");
  ava::tests::FakeTransport transport({});
  std::istringstream in(
      "{\"id\":\"set\",\"type\":\"set_reasoning\",\"reasoning_level\":\"medium\"}\n"
      "{\"id\":\"state\",\"type\":\"get_state\"}\n"
      "{\"id\":\"invalid\",\"type\":\"set_reasoning\",\"reasoning_level\":\"ultra\"}\n"
      "{\"id\":\"clear\",\"type\":\"clear_reasoning\"}\n");
  std::ostringstream out;
  auto result =
      ava::app::run_rpc_loop(*session, open_options, provider, transport, ava::app::RuntimeRunOptions{}, in, out);
  const auto jsonl = out.str();
  expect(result.has_value(), "RPC reasoning command loop completes successfully");
  expect(jsonl.find("\"id\":\"set\"") != std::string::npos &&
             jsonl.find("\"reasoning_enabled\":true") != std::string::npos &&
             jsonl.find("\"reasoning_level\":\"medium\"") != std::string::npos,
         "RPC set_reasoning returns enabled reasoning state");
  expect(jsonl.find("\"id\":\"state\"") != std::string::npos &&
             jsonl.find("\"reasoning_level\":\"medium\"") != std::string::npos,
         "RPC get_state reflects selected reasoning");
  expect(jsonl.find("\"id\":\"invalid\"") != std::string::npos &&
             jsonl.find("reasoning level is not supported") != std::string::npos,
         "RPC set_reasoning reports invalid reasoning levels");
  expect(jsonl.find("\"id\":\"clear\"") != std::string::npos &&
             jsonl.rfind("\"reasoning_enabled\":false") != std::string::npos,
         "RPC clear_reasoning disables reasoning state");
  expect(!session->reasoning, "RPC clear_reasoning updates active session state");

  auto entries = session->store.load();
  expect(entries.has_value(), "RPC reasoning command test reloads entries");
  if (entries) {
    const auto reasoning_changes = std::ranges::count_if(
        *entries, [](const auto& entry) { return entry.type == ava::session::EntryType::ReasoningChange; });
    expect(reasoning_changes == 2, "RPC reasoning commands persist set and clear reasoning_change entries");
  }
}

void test_app_rpc_protocol_version_and_session_commands() {
  const auto root = temp_root() / "app-rpc-protocol-session";
  std::error_code remove_error;
  std::filesystem::remove_all(root, remove_error);
  const auto workspace = root / "workspace";
  const auto paths = app_test_paths(root);
  std::filesystem::create_directories(workspace);

  ava::app::RuntimeOpenOptions open_options;
  open_options.workspace_dir = workspace;
  open_options.current_dir = workspace;
  open_options.paths = paths;
  auto session = ava::app::open_runtime_session(open_options);
  expect(session.has_value(), "RPC protocol/session test opens runtime session");
  if (!session) return;
  const auto initial_id = session->store.session_id();

  auto appended_user = session->store.append(ava::session::SessionEntry{.id = ava::core::make_id("entry"),
                                                                        .parent_id = "",
                                                                        .type = ava::session::EntryType::UserMessage,
                                                                        .timestamp = ava::session::now_timestamp(),
                                                                        .data_json = "{\"text\":\"hello\"}"});
  auto appended_internal_replay =
      session->store.append(ava::session::SessionEntry{.id = ava::core::make_id("entry"),
                                                       .parent_id = "",
                                                       .type = ava::session::EntryType::UserMessage,
                                                       .timestamp = ava::session::now_timestamp(),
                                                       .data_json = "{\"text\":\"hidden rpc replay\","
                                                                    "\"internal_replay\":true,"
                                                                    "\"replay_of\":\"entry_user\","
                                                                    "\"reason\":\"test\"}"});
  auto appended_assistant = session->store.append(
      ava::session::SessionEntry{.id = ava::core::make_id("entry"),
                                 .parent_id = "",
                                 .type = ava::session::EntryType::AssistantMessage,
                                 .timestamp = ava::session::now_timestamp(),
                                 .data_json = "{\"text\":\"answer\",\"usage\":{\"input_tokens\":1,"
                                              "\"output_tokens\":1,\"total_tokens\":2,"
                                              "\"cost_usd\":0.001,"
                                              "\"source\":\"provider\"}}"});
  auto appended_unpriced_assistant =
      session->store.append(ava::session::SessionEntry{.id = ava::core::make_id("entry"),
                                                       .parent_id = "",
                                                       .type = ava::session::EntryType::AssistantMessage,
                                                       .timestamp = ava::session::now_timestamp(),
                                                       .data_json = "{\"text\":\"unknown cost\",\"usage\":{"
                                                                    "\"input_tokens\":1,\"cache_read_tokens\":1,"
                                                                    "\"total_tokens\":1,"
                                                                    "\"source\":\"provider\"}}"});
  auto appended_reasoning =
      session->store.append(ava::session::SessionEntry{.id = ava::core::make_id("entry"),
                                                       .parent_id = "",
                                                       .type = ava::session::EntryType::ReasoningBlock,
                                                       .timestamp = ava::session::now_timestamp(),
                                                       .data_json = "{\"provider\":\"anthropic\","
                                                                    "\"model\":\"claude-sonnet-4-5\","
                                                                    "\"format\":\"anthropic_thinking\","
                                                                    "\"text\":\"visible reasoning\","
                                                                    "\"signature\":\"rpc-secret-signature\"}"});
  auto appended_redacted_reasoning =
      session->store.append(ava::session::SessionEntry{.id = ava::core::make_id("entry"),
                                                       .parent_id = "",
                                                       .type = ava::session::EntryType::ReasoningBlock,
                                                       .timestamp = ava::session::now_timestamp(),
                                                       .data_json = "{\"provider\":\"anthropic\","
                                                                    "\"model\":\"claude-sonnet-4-5\","
                                                                    "\"format\":\"anthropic_thinking\","
                                                                    "\"text\":\"hidden redacted rpc reasoning\","
                                                                    "\"signature\":\"rpc-redacted-secret-signature\","
                                                                    "\"redacted\": true }"});
  auto appended_mode = session->store.append(ava::session::SessionEntry{.id = ava::core::make_id("entry"),
                                                                        .parent_id = "",
                                                                        .type = ava::session::EntryType::ModeChange,
                                                                        .timestamp = ava::session::now_timestamp(),
                                                                        .data_json = "{\"mode\":\"build\"}"});
  auto appended_compaction =
      session->store.append(ava::session::SessionEntry{.id = ava::core::make_id("entry"),
                                                       .parent_id = "",
                                                       .type = ava::session::EntryType::Compaction,
                                                       .timestamp = ava::session::now_timestamp(),
                                                       .data_json = "{\"summary\":\"prior\"}"});
  auto appended_cancel = session->store.append(ava::session::SessionEntry{.id = ava::core::make_id("entry"),
                                                                          .parent_id = "",
                                                                          .type = ava::session::EntryType::Cancel,
                                                                          .timestamp = ava::session::now_timestamp(),
                                                                          .data_json = "{}"});
  expect(appended_user.has_value() && appended_internal_replay.has_value() && appended_assistant.has_value() &&
             appended_unpriced_assistant.has_value() && appended_reasoning.has_value() &&
             appended_redacted_reasoning.has_value() && appended_mode.has_value() && appended_compaction.has_value() &&
             appended_cancel.has_value(),
         "RPC protocol/session test appends messages and stats foundation entries");

  const ava::provider::OpenAIProvider provider("https://api.example.test");
  ava::tests::FakeTransport transport({});
  std::istringstream in(
      "{\"id\":\"proto\",\"type\":\"get_protocol\",\"protocol_version\":1}\n"
      "{\"id\":\"messages\",\"type\":\"get_messages\"}\n"
      "{\"id\":\"stats\",\"type\":\"get_session_stats\"}\n"
      "{\"id\":\"validate\",\"type\":\"validate_session\"}\n"
      "{\"id\":\"new\",\"type\":\"new_session\"}\n"
      "{\"id\":\"switch\",\"type\":\"switch_session\",\"session_id\":\"" +
      initial_id + "\"}\n");
  std::ostringstream out;
  ava::app::RuntimeRunOptions runtime_options;
  runtime_options.access_token = "token";
  auto result = ava::app::run_rpc_loop(*session, open_options, provider, transport, runtime_options, in, out);
  const auto jsonl = out.str();
  expect(result.has_value(), "RPC protocol/session loop completes successfully");
  expect(jsonl.find("\"id\":\"proto\"") != std::string::npos &&
             jsonl.find("\"protocol_version\":1") != std::string::npos &&
             jsonl.find("\"supported_protocol_versions\":[1]") != std::string::npos,
         "RPC get_protocol reports supported protocol version");
  expect(jsonl.find("\"id\":\"messages\"") != std::string::npos && jsonl.find("\"messages\"") != std::string::npos &&
             jsonl.find("\"version\":2") != std::string::npos && jsonl.find("hello") != std::string::npos &&
             jsonl.find("answer") != std::string::npos && jsonl.find("visible reasoning") != std::string::npos &&
             jsonl.find("hidden redacted rpc reasoning") == std::string::npos &&
             jsonl.find("\"signature_present\":true") != std::string::npos &&
             jsonl.find("hidden rpc replay") == std::string::npos &&
             jsonl.find("rpc-secret-signature") == std::string::npos &&
             jsonl.find("rpc-redacted-secret-signature") == std::string::npos,
         "RPC get_messages returns consumer-visible durable message entries without reasoning signatures");
  expect(jsonl.find("\"id\":\"stats\"") != std::string::npos && jsonl.find("\"entry_count\":10") != std::string::npos &&
             jsonl.find("\"user_message\":1") != std::string::npos &&
             jsonl.find("\"assistant_message\":2") != std::string::npos &&
             jsonl.find("\"reasoning_block\":2") != std::string::npos &&
             jsonl.find("\"mode_change\":1") != std::string::npos &&
             jsonl.find("\"compaction\":1") != std::string::npos && jsonl.find("\"cancel\":1") != std::string::npos,
         "RPC get_session_stats returns session counters");
  expect(jsonl.find("\"known_cost_usd\":0.001") != std::string::npos &&
             jsonl.find("\"cost_complete\":false") != std::string::npos &&
             jsonl.find("\"unknown_cost_entries\":1") != std::string::npos &&
             jsonl.find("\"total_cost_usd\"") == std::string::npos,
         "RPC get_session_stats omits incomplete total cost and reports known cost metadata");
  expect(jsonl.find("\"id\":\"validate\"") != std::string::npos && jsonl.find("\"ok\":true") != std::string::npos &&
             jsonl.find("\"error_count\":0") != std::string::npos,
         "RPC validate_session reports a clean replay audit for the active session");
  expect(jsonl.find("\"id\":\"new\"") != std::string::npos && jsonl.find("\"created\":true") != std::string::npos,
         "RPC new_session creates and switches to a new active session");
  expect(session->store.session_id() == initial_id && jsonl.find("\"id\":\"switch\"") != std::string::npos,
         "RPC switch_session switches back to the requested session");
}

void test_app_rpc_protocol_version_and_resolver_reply_errors() {
  const auto root = temp_root() / "app-rpc-protocol-errors";
  std::error_code remove_error;
  std::filesystem::remove_all(root, remove_error);
  const auto workspace = root / "workspace";
  const auto paths = app_test_paths(root);
  std::filesystem::create_directories(workspace);

  ava::app::RuntimeOpenOptions open_options;
  open_options.workspace_dir = workspace;
  open_options.current_dir = workspace;
  open_options.paths = paths;
  auto session = ava::app::open_runtime_session(open_options);
  expect(session.has_value(), "RPC protocol error test opens runtime session");
  if (!session) return;

  const ava::provider::OpenAIProvider provider("https://api.example.test");
  ava::tests::FakeTransport transport({});
  std::string input =
      "{\"id\":\"bad-version\",\"type\":\"get_state\",\"protocol_version\":999}\n"
      "{\"id\":\"reply-missing\",\"type\":\"permission_reply\"}\n"
      "{\"id\":\"bad-version-type\",\"type\":\"get_state\",\"protocol_version\":\"1\"}\n"
      "{\"id\":\"oversized-reply\",\"type\":\"permission_reply\",\"request_id\":\"" +
      std::string(257, 'r') +
      "\",\"correlation_id\":\"p1\",\"decision\":\"allow\"}\n"
      "{\"id\":\"reply\",\"type\":\"question_reply\",\"request_id\":\"question_1\",\"correlation_id\":\"p1\","
      "\"answer\":\"ok\"}\n"
      "{\"id\":\"state\",\"type\":\"get_state\",\"protocol_version\":1}\n";
  std::istringstream in(input);
  std::ostringstream out;
  auto result =
      ava::app::run_rpc_loop(*session, open_options, provider, transport, ava::app::RuntimeRunOptions{}, in, out);
  const auto jsonl = out.str();
  expect(result.has_value(), "RPC protocol error loop recovers after unsupported commands");
  expect(jsonl.find("unsupported RPC protocol version") != std::string::npos &&
             jsonl.find("RPC protocol_version must be an integer") != std::string::npos &&
             jsonl.find("permission_reply requires request_id") != std::string::npos &&
             jsonl.find("\"id\":\"oversized-reply\"") != std::string::npos &&
             jsonl.find("RPC identifier is too long") != std::string::npos &&
             jsonl.find("RPC resolver reply has no matching pending request") != std::string::npos &&
             jsonl.find("\"id\":\"state\"") != std::string::npos && jsonl.find("\"success\":true") != std::string::npos,
         "RPC version and resolver reply errors are in-band and recoverable");
}

void test_app_rpc_mcp_command_responses() {
  expect(!std::string_view(AVA_FAKE_MCP_SERVER_PATH).empty(), "RPC MCP command test has fake server path");
  if (std::string_view(AVA_FAKE_MCP_SERVER_PATH).empty()) return;

  const auto root = temp_root() / "app-rpc-mcp-commands";
  std::error_code remove_error;
  std::filesystem::remove_all(root, remove_error);
  const auto workspace = root / "workspace";
  const auto paths = app_test_paths(root);
  std::filesystem::create_directories(workspace);
  write_app_test_file(workspace / ".ava" / "mcp.json",
                      app_test_mcp_config_json("demo", "Demo MCP", AVA_FAKE_MCP_SERVER_PATH));

  ava::app::RuntimeOpenOptions open_options;
  open_options.workspace_dir = workspace;
  open_options.current_dir = workspace;
  open_options.paths = paths;
  auto session = ava::app::open_runtime_session(open_options);
  expect(session.has_value(), "RPC MCP command test opens runtime session");
  if (!session) return;

  std::vector<ava::permissions::PermissionPrompt> prompts;
  ava::app::RuntimeRunOptions runtime_options;
  runtime_options.permission_resolver = [&prompts](const ava::permissions::PermissionPrompt& prompt)
      -> ava::core::Result<ava::permissions::PermissionResolution> {
    prompts.push_back(prompt);
    return ava::permissions::PermissionResolution::Allow;
  };

  const ava::provider::OpenAIProvider provider("https://api.example.test");
  ava::tests::FakeTransport transport({});
  std::istringstream in(
      "{\"id\":\"mcp-list\",\"type\":\"list_mcp_servers\"}\n"
      "{\"id\":\"mcp-inspect\",\"type\":\"inspect_mcp_server\",\"server_id\":\"demo\"}\n"
      "{\"id\":\"mcp-tools\",\"type\":\"list_mcp_tools\",\"server_id\":\"demo\"}\n"
      "{\"id\":\"mcp-restart\",\"type\":\"restart_mcp_server\",\"server_id\":\"demo\"}\n");
  std::ostringstream out;
  auto result = ava::app::run_rpc_loop(*session, open_options, provider, transport, runtime_options, in, out);
  const auto jsonl = out.str();

  const auto has_id = [&jsonl](std::string_view id) {
    return jsonl.find("\"id\":\"" + std::string(id) + "\"") != std::string::npos;
  };
  expect(result.has_value(), "RPC MCP command loop completes successfully");
  expect(has_id("mcp-list") && has_id("mcp-inspect") && has_id("mcp-tools") && has_id("mcp-restart"),
         "RPC MCP command responses include all request ids");
  expect(jsonl.find("MCP servers:") != std::string::npos && jsonl.find("Demo MCP") != std::string::npos &&
             jsonl.find("MCP server demo") != std::string::npos &&
             jsonl.find("MCP tools for demo") != std::string::npos && jsonl.find("fake-mcp") != std::string::npos &&
             jsonl.find("echo") != std::string::npos && jsonl.find("mcp_demo_echo") != std::string::npos &&
             jsonl.find("next discovery or tool call will launch a fresh process") != std::string::npos,
         "RPC MCP command responses expose list, inspect, tools, and restart output");

  const auto has_launch_prompt = std::ranges::any_of(prompts, [](const auto& prompt) {
    return prompt.operation == ava::permissions::Operation::McpServerLaunch && prompt.tool_name == "mcp_tools";
  });
  const auto has_connect_prompt = std::ranges::any_of(prompts, [](const auto& prompt) {
    return prompt.operation == ava::permissions::Operation::McpServerConnect && prompt.tool_name == "mcp_tools" &&
           prompt.command == "demo";
  });
  expect(has_launch_prompt && has_connect_prompt,
         "RPC list_mcp_tools requests MCP launch and connect permissions before allowing discovery");
}

void test_app_rpc_command_responses_for_context_compact_export() {
  const auto root = temp_root() / "app-rpc-commands";
  std::error_code remove_error;
  std::filesystem::remove_all(root, remove_error);
  const auto workspace = root / "workspace";
  const auto paths = app_test_paths(root);
  std::filesystem::create_directories(workspace);
  {
    std::ofstream file(workspace / "AGENTS.md", std::ios::binary | std::ios::trunc);
    file << "rpc command context\n";
  }
  write_app_test_file(workspace / ".ava" / "plugins" / "com.example.rpc" / "plugin.json",
                      app_test_plugin_manifest_json("com.example.rpc", "RPC Plugin"));
  write_app_test_file(workspace / ".ava" / "plugins" / "com.example.rpcbad" / "plugin.json", "{not-json");

  ava::app::RuntimeOpenOptions open_options;
  open_options.workspace_dir = workspace;
  open_options.current_dir = workspace;
  open_options.paths = paths;
  auto session = ava::app::open_runtime_session(open_options);
  expect(session.has_value(), "RPC command test opens runtime session");
  if (!session) return;

  const ava::provider::OpenAIProvider provider("https://api.example.test");
  const std::string rpc_summary =
      "# Goal\nRemember RPC facts\n# Constraints / Preferences\nNone noted.\n# Decisions\nNone noted.\n"
      "# Files Read or Modified\nNone noted.\n# Unresolved Tasks\nNone noted.\n# Next Steps\nContinue.";
  ava::tests::FakeTransport transport(
      {ava::provider::HttpResponse{.status_code = 200,
                                   .headers = {},
                                   .body = "{\"output_text\":\"" + ava::core::json::escape(rpc_summary) + "\"}"}});
  std::istringstream in(
      "{\"id\":\"plugins\",\"type\":\"list_plugins\"}\n"
      "{\"id\":\"plugin-enable\",\"type\":\"enable_plugin\",\"plugin_id\":\"com.example.rpc\"}\n"
      "{\"id\":\"plugin-inspect\",\"type\":\"inspect_plugin\",\"plugin_id\":\"com.example.rpc\"}\n"
      "{\"id\":\"plugin-validate\",\"type\":\"validate_plugin\",\"path\":\".ava/plugins/com.example.rpc/"
      "plugin.json\"}\n"
      "{\"id\":\"plugin-failures\",\"type\":\"plugin_failures\"}\n"
      "{\"id\":\"ctx\",\"type\":\"context\"}\n"
      "{\"id\":\"cmp\",\"type\":\"compact\",\"instructions\":\"remember rpc facts\"}\n"
      "{\"id\":\"exp\",\"type\":\"export\"}\n");
  std::ostringstream out;
  ava::app::RuntimeRunOptions runtime_options;
  runtime_options.access_token = "token";
  auto result = ava::app::run_rpc_loop(*session, open_options, provider, transport, runtime_options, in, out);
  const auto jsonl = out.str();
  expect(result.has_value(), "RPC context/compact/export loop completes successfully");
  expect(jsonl.find("\"id\":\"plugins\"") != std::string::npos && jsonl.find("com.example.rpc") != std::string::npos &&
             jsonl.find("\"id\":\"plugin-enable\"") != std::string::npos &&
             jsonl.find("No plugin process was started") != std::string::npos &&
             jsonl.find("\"id\":\"plugin-inspect\"") != std::string::npos &&
             jsonl.find("status: enabled") != std::string::npos &&
             jsonl.find("\"id\":\"plugin-validate\"") != std::string::npos &&
             jsonl.find("Valid plugin manifest") != std::string::npos &&
             jsonl.find("\"id\":\"plugin-failures\"") != std::string::npos &&
             jsonl.find("com.example.rpcbad") != std::string::npos &&
             jsonl.find("\"id\":\"ctx\"") != std::string::npos && jsonl.find("AGENTS.md") != std::string::npos &&
             jsonl.find("\"id\":\"cmp\"") != std::string::npos &&
             jsonl.find("\"name\":\"compaction_start\"") != std::string::npos &&
             jsonl.find("\"name\":\"compaction_end\"") != std::string::npos &&
             jsonl.find("compaction summary recorded") != std::string::npos &&
             jsonl.find("\"id\":\"exp\"") != std::string::npos &&
             jsonl.find("# AVA Session Export") != std::string::npos &&
             jsonl.find("remember rpc facts") != std::string::npos,
         "RPC command responses expose command dispatcher output as JSONL protocol records");
}

void test_app_rpc_compact_provider_failure_is_error_response() {
  const auto root = temp_root() / "app-rpc-compact-failure";
  std::error_code remove_error;
  std::filesystem::remove_all(root, remove_error);
  const auto workspace = root / "workspace";
  const auto paths = app_test_paths(root);
  std::filesystem::create_directories(workspace);

  ava::app::RuntimeOpenOptions open_options;
  open_options.workspace_dir = workspace;
  open_options.current_dir = workspace;
  open_options.paths = paths;
  auto session = ava::app::open_runtime_session(open_options);
  expect(session.has_value(), "RPC compact failure test opens runtime session");
  if (!session) return;

  const ava::provider::OpenAIProvider provider("https://api.example.test");
  ava::tests::FakeTransport transport({ava::provider::HttpResponse{
      .status_code = 500, .headers = {}, .body = "{\"error\":{\"message\":\"summary failed\"}}"}});
  std::istringstream in("{\"id\":\"cmp-fail\",\"type\":\"compact\"}\n");
  std::ostringstream out;
  ava::app::RuntimeRunOptions runtime_options;
  runtime_options.access_token = "token";

  auto result = ava::app::run_rpc_loop(*session, open_options, provider, transport, runtime_options, in, out);
  const auto jsonl = out.str();
  auto entries = session->store.load();
  expect(result.has_value(), "RPC compact failure loop completes after error response");
  expect(jsonl.find("\"id\":\"cmp-fail\"") != std::string::npos &&
             jsonl.find("\"success\":false") != std::string::npos &&
             jsonl.find("compaction summary request failed with status 500") != std::string::npos &&
             jsonl.find("summary failed") != std::string::npos,
         "RPC compact provider failures are machine-readable error responses");
  expect(entries && count_compaction_entries(*entries) == 0,
         "RPC compact provider failure leaves session without a compaction entry");
}

void test_app_rpc_cancel_affects_subsequent_prompt() {
  const auto root = temp_root() / "app-rpc-cancel";
  std::error_code remove_error;
  std::filesystem::remove_all(root, remove_error);
  const auto workspace = root / "workspace";
  const auto paths = app_test_paths(root);
  std::filesystem::create_directories(workspace);

  ava::app::RuntimeOpenOptions open_options;
  open_options.workspace_dir = workspace;
  open_options.current_dir = workspace;
  open_options.paths = paths;
  auto session = ava::app::open_runtime_session(open_options);
  expect(session.has_value(), "RPC cancel test opens runtime session");
  if (!session) return;

  const ava::provider::OpenAIProvider provider("https://api.example.test");
  ava::tests::FakeTransport transport({});
  ava::app::RuntimeRunOptions runtime_options;
  runtime_options.access_token = "token";
  std::istringstream in(
      "{\"id\":\"cancel\",\"type\":\"cancel\"}\n"
      "{\"id\":\"state\",\"type\":\"get_state\"}\n"
      "{\"id\":\"prompt\",\"type\":\"prompt\",\"message\":\"should cancel\"}\n");
  std::ostringstream out;
  auto result = ava::app::run_rpc_loop(*session, open_options, provider, transport, runtime_options, in, out);
  const auto jsonl = out.str();
  expect(result.has_value(), "RPC cancel loop completes after canceled prompt response");
  expect(transport.requests().empty(), "RPC cancel flag prevents subsequent prompt provider request");
  expect(jsonl.find("\"id\":\"cancel\"") != std::string::npos &&
             jsonl.find("\"cancel_requested\":true") != std::string::npos &&
             jsonl.find("\"name\":\"cancel_requested\"") != std::string::npos &&
             jsonl.find("\"name\":\"canceled\"") != std::string::npos &&
             jsonl.find("\"id\":\"prompt\"") != std::string::npos &&
             jsonl.find("agent loop canceled") != std::string::npos &&
             jsonl.find("\"success\":false") != std::string::npos,
         "RPC cancel response updates state and canceled prompts return protocol errors");
}

void test_app_rpc_active_prompt_cancel_unblocks_pending_permission() {
  const auto root = temp_root() / "app-rpc-active-cancel";
  std::error_code remove_error;
  std::filesystem::remove_all(root, remove_error);
  const auto workspace = root / "workspace";
  const auto paths = app_test_paths(root);
  std::filesystem::create_directories(workspace);
  {
    std::ofstream file(workspace / "note.txt", std::ios::binary | std::ios::trunc);
    file << "rpc cancel note";
  }
  const auto outside_path = root / "outside.txt";
  {
    std::ofstream file(outside_path, std::ios::binary | std::ios::trunc);
    file << "outside cancel note";
  }

  ava::app::RuntimeOpenOptions open_options;
  open_options.workspace_dir = workspace;
  open_options.current_dir = workspace;
  open_options.paths = paths;
  auto session = ava::app::open_runtime_session(open_options);
  expect(session.has_value(), "RPC active cancel test opens runtime session");
  if (!session) return;

  const ava::provider::OpenAIProvider provider("https://api.example.test");
  ava::tests::FakeTransport transport({sse_response(read_file_call_sse(outside_path.generic_string()))});
  ava::app::RuntimeRunOptions runtime_options;
  runtime_options.access_token = "token";
  BlockingInputBuf input_buffer;
  std::istream in(&input_buffer);
  ThreadSafeStringBuf output_buffer;
  std::ostream out(&output_buffer);
  ava::core::VoidResult result;
  std::jthread rpc_thread(
      [&] { result = ava::app::run_rpc_loop(*session, open_options, provider, transport, runtime_options, in, out); });

  input_buffer.push("{\"id\":\"p1\",\"type\":\"prompt\",\"message\":\"read note\"}\n");
  const bool requested = output_buffer.wait_contains("\"name\":\"permission_requested\"", std::chrono::seconds(2));
  expect(requested, "RPC active cancel test observes pending permission request");
  input_buffer.push("{\"id\":\"cancel\",\"type\":\"cancel\"}\n");
  input_buffer.close();
  rpc_thread.join();

  const auto jsonl = output_buffer.str();
  expect(result.has_value(), "RPC active cancel loop exits successfully");
  expect(
      jsonl.find("\"id\":\"cancel\"") != std::string::npos && jsonl.find("\"active_run\":true") != std::string::npos &&
          jsonl.find("\"name\":\"cancel_requested\"") != std::string::npos &&
          jsonl.find("\"name\":\"canceled\"") != std::string::npos &&
          jsonl.find("\"id\":\"p1\"") != std::string::npos && jsonl.find("agent loop canceled") != std::string::npos &&
          jsonl.find("\"success\":false") != std::string::npos,
      "RPC cancel is processed while prompt waits and prompt receives one canceled response");
}

void test_app_rpc_steer_applies_before_next_provider_request() {
  const auto root = temp_root() / "app-rpc-steer";
  std::error_code remove_error;
  std::filesystem::remove_all(root, remove_error);
  const auto workspace = root / "workspace";
  const auto paths = app_test_paths(root);
  std::filesystem::create_directories(workspace);
  const auto outside_path = root / "outside.txt";
  {
    std::ofstream file(outside_path, std::ios::binary | std::ios::trunc);
    file << "outside steer note";
  }

  ava::app::RuntimeOpenOptions open_options;
  open_options.workspace_dir = workspace;
  open_options.current_dir = workspace;
  open_options.paths = paths;
  auto session = ava::app::open_runtime_session(open_options);
  expect(session.has_value(), "RPC steer test opens runtime session");
  if (!session) return;

  const ava::provider::OpenAIProvider provider("https://api.example.test");
  ava::tests::FakeTransport transport(
      {sse_response(read_file_call_sse(outside_path.generic_string())), sse_response(final_text_sse("after steer"))});
  ava::app::RuntimeRunOptions runtime_options;
  runtime_options.access_token = "token";
  BlockingInputBuf input_buffer;
  std::istream in(&input_buffer);
  ThreadSafeStringBuf output_buffer;
  std::ostream out(&output_buffer);
  ava::core::VoidResult result;
  std::jthread rpc_thread(
      [&] { result = ava::app::run_rpc_loop(*session, open_options, provider, transport, runtime_options, in, out); });

  input_buffer.push("{\"id\":\"p1\",\"type\":\"prompt\",\"message\":\"read before steer\"}\n");
  const bool requested = output_buffer.wait_contains("\"resolver_request_id\":\"permission_", std::chrono::seconds(2));
  const auto resolver_request_id = extract_json_string_field(output_buffer.str(), "resolver_request_id");
  expect(requested && !resolver_request_id.empty(), "RPC steer test observes permission wait safe point");
  input_buffer.push("{\"id\":\"s1\",\"type\":\"steer\",\"message\":\"steer this turn\"}\n");
  input_buffer.push("{\"id\":\"reply\",\"type\":\"permission_reply\",\"request_id\":\"" + resolver_request_id +
                    "\",\"correlation_id\":\"p1\",\"decision\":\"allow\"}\n");
  const bool completed = output_buffer.wait_contains("after steer", std::chrono::seconds(2));
  input_buffer.close();
  rpc_thread.join();

  const auto jsonl = output_buffer.str();
  expect(result.has_value() && completed, "RPC steer loop exits successfully");
  expect(transport.requests().size() == 2 && transport.requests()[1].body.find("steer this turn") != std::string::npos,
         "RPC steer is appended before the next provider request after tool completion");
  expect(jsonl.find("\"name\":\"steer_queued\"") != std::string::npos &&
             jsonl.find("\"name\":\"steer_applied\"") != std::string::npos &&
             jsonl.find("\"id\":\"s1\"") != std::string::npos && jsonl.find("\"success\":true") != std::string::npos &&
             jsonl.find("after steer") != std::string::npos,
         "RPC steer emits queued/applied events and active prompt completes");
}

void test_app_rpc_follow_up_runs_after_active_prompt() {
  const auto root = temp_root() / "app-rpc-follow-up";
  std::error_code remove_error;
  std::filesystem::remove_all(root, remove_error);
  const auto workspace = root / "workspace";
  const auto paths = app_test_paths(root);
  std::filesystem::create_directories(workspace);
  const auto outside_path = root / "outside.txt";
  {
    std::ofstream file(outside_path, std::ios::binary | std::ios::trunc);
    file << "outside follow note";
  }

  ava::app::RuntimeOpenOptions open_options;
  open_options.workspace_dir = workspace;
  open_options.current_dir = workspace;
  open_options.paths = paths;
  auto session = ava::app::open_runtime_session(open_options);
  expect(session.has_value(), "RPC follow_up test opens runtime session");
  if (!session) return;

  const ava::provider::OpenAIProvider provider("https://api.example.test");
  ava::tests::FakeTransport transport({sse_response(read_file_call_sse(outside_path.generic_string())),
                                       sse_response(final_text_sse("first done")),
                                       sse_response(final_text_sse("follow done"))});
  ava::app::RuntimeRunOptions runtime_options;
  runtime_options.access_token = "token";
  BlockingInputBuf input_buffer;
  std::istream in(&input_buffer);
  ThreadSafeStringBuf output_buffer;
  std::ostream out(&output_buffer);
  ava::core::VoidResult result;
  std::jthread rpc_thread(
      [&] { result = ava::app::run_rpc_loop(*session, open_options, provider, transport, runtime_options, in, out); });

  input_buffer.push("{\"id\":\"p1\",\"type\":\"prompt\",\"message\":\"first prompt\"}\n");
  const bool requested = output_buffer.wait_contains("\"resolver_request_id\":\"permission_", std::chrono::seconds(2));
  const auto resolver_request_id = extract_json_string_field(output_buffer.str(), "resolver_request_id");
  expect(requested && !resolver_request_id.empty(), "RPC follow_up test observes active prompt wait");
  input_buffer.push("{\"id\":\"fu1\",\"type\":\"follow_up\",\"message\":\"follow message\"}\n");
  input_buffer.push("{\"id\":\"reply\",\"type\":\"permission_reply\",\"request_id\":\"" + resolver_request_id +
                    "\",\"correlation_id\":\"p1\",\"decision\":\"allow\"}\n");
  const bool followed = output_buffer.wait_contains("follow done", std::chrono::seconds(2));
  input_buffer.close();
  rpc_thread.join();

  const auto jsonl = output_buffer.str();
  const auto first_response = jsonl.find("\"id\":\"p1\"");
  const auto started = jsonl.find("\"name\":\"follow_up_started\"");
  const auto follow_response = jsonl.find("\"id\":\"fu1\"");
  expect(result.has_value() && followed, "RPC follow_up loop runs queued follow-up successfully");
  expect(transport.requests().size() == 3 && transport.requests()[2].body.find("follow message") != std::string::npos,
         "RPC follow_up starts a new provider turn with the queued message");
  expect(jsonl.find("\"name\":\"follow_up_queued\"") != std::string::npos && first_response != std::string::npos &&
             started != std::string::npos && follow_response != std::string::npos && first_response < started &&
             started < follow_response,
         "RPC follow_up emits queued/started events and responds after the active prompt response");
}

void test_app_rpc_prompt_start_failure_cleans_queued_messages() {
  const auto root = temp_root() / "app-rpc-prompt-start-fail-queue";
  std::error_code remove_error;
  std::filesystem::remove_all(root, remove_error);
  const auto workspace = root / "workspace";
  const auto paths = app_test_paths(root);
  std::filesystem::create_directories(workspace);
  auto stored = ava::config::store_openai_credential(
      paths, ava::config::OpenAICredential{.type = ava::config::OpenAICredentialType::OAuth,
                                           .access_token = "expired-queued-access",
                                           .refresh_token = "queued-refresh",
                                           .expires_at = 100,
                                           .account_id = "acct_old",
                                           .source_path = {}});
  expect(stored.has_value(), "RPC prompt start failure test stores expired credential");

  ava::app::RuntimeOpenOptions open_options;
  open_options.workspace_dir = workspace;
  open_options.current_dir = workspace;
  open_options.paths = paths;
  auto session = ava::app::open_runtime_session(open_options);
  expect(session.has_value(), "RPC prompt start failure test opens runtime session");
  if (!session) return;

  const ava::provider::OpenAIProvider provider("https://api.example.test");
  BlockingResponseTransport transport(
      ava::provider::HttpResponse{.status_code = 400, .headers = {}, .body = "{\"error\":\"refresh failed\"}"});
  BlockingInputBuf input_buffer;
  std::istream in(&input_buffer);
  ThreadSafeStringBuf output_buffer;
  std::ostream out(&output_buffer);
  ava::core::VoidResult result;
  std::jthread rpc_thread(
      [&] { result = ava::app::run_rpc_loop(*session, open_options, provider, transport, {}, in, out); });

  input_buffer.push("{\"id\":\"p1\",\"type\":\"prompt\",\"message\":\"will fail before run\"}\n");
  const bool refresh_requested = transport.wait_for_request(std::chrono::seconds(2));
  expect(refresh_requested, "RPC prompt start failure test blocks during OAuth refresh");
  input_buffer.push("{\"id\":\"s1\",\"type\":\"steer\",\"message\":\"never apply\"}\n");
  input_buffer.push("{\"id\":\"fu1\",\"type\":\"follow_up\",\"message\":\"never run\"}\n");
  const bool queued = output_buffer.wait_contains("\"name\":\"follow_up_queued\"", std::chrono::seconds(2));
  expect(queued, "RPC prompt start failure test queues follow-up before startup failure");
  transport.release();
  const bool skipped = output_buffer.wait_contains("prompt_start_failed", std::chrono::seconds(2));
  input_buffer.close();
  rpc_thread.join();

  const auto jsonl = output_buffer.str();
  expect(result.has_value(), "RPC prompt start failure queue cleanup loop exits successfully");
  expect(skipped && jsonl.find("\"name\":\"steer_skipped\"") != std::string::npos &&
             jsonl.find("\"name\":\"follow_up_skipped\"") != std::string::npos &&
             jsonl.find("\"request_id\":\"fu1\"") != std::string::npos &&
             jsonl.find("\"id\":\"fu1\"") != std::string::npos && jsonl.find("\"success\":false") != std::string::npos,
         "RPC prompt startup failure emits skipped queue events and a failed follow-up response");
  expect(transport.requests().size() == 1, "RPC prompt startup failure does not run queued follow-up provider calls");
}

void test_app_rpc_steer_after_follow_up_started_targets_follow_up() {
  const auto root = temp_root() / "app-rpc-follow-up-steer";
  std::error_code remove_error;
  std::filesystem::remove_all(root, remove_error);
  const auto workspace = root / "workspace";
  const auto paths = app_test_paths(root);
  std::filesystem::create_directories(workspace);
  const auto outside_path = root / "outside.txt";
  {
    std::ofstream file(outside_path, std::ios::binary | std::ios::trunc);
    file << "outside follow steer note";
  }

  ava::app::RuntimeOpenOptions open_options;
  open_options.workspace_dir = workspace;
  open_options.current_dir = workspace;
  open_options.paths = paths;
  auto session = ava::app::open_runtime_session(open_options);
  expect(session.has_value(), "RPC follow-up steer test opens runtime session");
  if (!session) return;

  const ava::provider::OpenAIProvider provider("https://api.example.test");
  ava::tests::FakeTransport transport({sse_response(read_file_call_sse(outside_path.generic_string())),
                                       sse_response(final_text_sse("first done")),
                                       sse_response(read_file_call_sse(outside_path.generic_string())),
                                       sse_response(final_text_sse("follow steered done"))});
  ava::app::RuntimeRunOptions runtime_options;
  runtime_options.access_token = "token";
  BlockingInputBuf input_buffer;
  std::istream in(&input_buffer);
  ThreadSafeStringBuf output_buffer;
  std::ostream out(&output_buffer);
  ava::core::VoidResult result;
  std::jthread rpc_thread(
      [&] { result = ava::app::run_rpc_loop(*session, open_options, provider, transport, runtime_options, in, out); });

  input_buffer.push("{\"id\":\"p1\",\"type\":\"prompt\",\"message\":\"first prompt\"}\n");
  const bool parent_requested =
      output_buffer.wait_contains("\"resolver_request_id\":\"permission_", std::chrono::seconds(2));
  const auto parent_resolver_id = extract_json_string_field(output_buffer.str(), "resolver_request_id");
  expect(parent_requested && !parent_resolver_id.empty(), "RPC follow-up steer test observes parent permission wait");
  input_buffer.push("{\"id\":\"fu1\",\"type\":\"follow_up\",\"message\":\"follow message\"}\n");
  input_buffer.push("{\"id\":\"reply1\",\"type\":\"permission_reply\",\"request_id\":\"" + parent_resolver_id +
                    "\",\"correlation_id\":\"p1\",\"decision\":\"allow\"}\n");
  const bool started = output_buffer.wait_contains("\"name\":\"follow_up_started\"", std::chrono::seconds(2));
  expect(started, "RPC follow-up steer test observes follow-up start event");
  input_buffer.push("{\"id\":\"sfu\",\"type\":\"steer\",\"message\":\"steer follow turn\"}\n");
  const bool follow_requested = output_buffer.wait_contains(
      "\"correlation_id\":\"fu1\",\"name\":\"permission_requested\"", std::chrono::seconds(2));
  const auto follow_resolver_id = extract_last_json_string_field(output_buffer.str(), "resolver_request_id");
  expect(follow_requested && !follow_resolver_id.empty(),
         "RPC follow-up steer test observes follow-up permission wait");
  input_buffer.push("{\"id\":\"reply2\",\"type\":\"permission_reply\",\"request_id\":\"" + follow_resolver_id +
                    "\",\"correlation_id\":\"fu1\",\"decision\":\"allow\"}\n");
  const bool completed = output_buffer.wait_contains("follow steered done", std::chrono::seconds(2));
  input_buffer.close();
  rpc_thread.join();

  const auto jsonl = output_buffer.str();
  expect(result.has_value() && completed, "RPC follow-up steer loop exits successfully");
  expect(
      transport.requests().size() == 4 && transport.requests()[3].body.find("steer follow turn") != std::string::npos,
      "RPC steer after follow_up_started is appended to the follow-up continuation request");
  expect(jsonl.find("\"request_id\":\"fu1\",\"correlation_id\":\"fu1\",\"name\":\"follow_up_started\"") !=
                 std::string::npos &&
             jsonl.find("\"id\":\"sfu\"") != std::string::npos &&
             jsonl.find("\"correlation_id\":\"fu1\"") != std::string::npos,
         "RPC follow_up_started and subsequent steer use follow-up correlation");
}

void test_app_rpc_queue_limit_rejects_new_items() {
  const auto root = temp_root() / "app-rpc-queue-limit";
  std::error_code remove_error;
  std::filesystem::remove_all(root, remove_error);
  const auto workspace = root / "workspace";
  const auto paths = app_test_paths(root);
  std::filesystem::create_directories(workspace);
  const auto outside_path = root / "outside.txt";
  {
    std::ofstream file(outside_path, std::ios::binary | std::ios::trunc);
    file << "outside queue limit note";
  }

  ava::app::RuntimeOpenOptions open_options;
  open_options.workspace_dir = workspace;
  open_options.current_dir = workspace;
  open_options.paths = paths;
  auto session = ava::app::open_runtime_session(open_options);
  expect(session.has_value(), "RPC queue limit test opens runtime session");
  if (!session) return;

  const ava::provider::OpenAIProvider provider("https://api.example.test");
  ava::tests::FakeTransport transport({sse_response(read_file_call_sse(outside_path.generic_string()))});
  ava::app::RuntimeRunOptions runtime_options;
  runtime_options.access_token = "token";
  BlockingInputBuf input_buffer;
  std::istream in(&input_buffer);
  ThreadSafeStringBuf output_buffer;
  std::ostream out(&output_buffer);
  ava::core::VoidResult result;
  std::jthread rpc_thread(
      [&] { result = ava::app::run_rpc_loop(*session, open_options, provider, transport, runtime_options, in, out); });

  input_buffer.push("{\"id\":\"p1\",\"type\":\"prompt\",\"message\":\"read before limit\"}\n");
  const bool requested = output_buffer.wait_contains("\"name\":\"permission_requested\"", std::chrono::seconds(2));
  expect(requested, "RPC queue limit test observes active permission wait");
  for (int index = 0; index < 65; ++index) {
    input_buffer.push("{\"id\":\"s" + std::to_string(index) + "\",\"type\":\"steer\",\"message\":\"queued steer\"}\n");
  }
  const bool rejected = output_buffer.wait_contains("RPC queued message limit exceeded", std::chrono::seconds(2));
  expect(rejected, "RPC queue limit test observes capped steer rejection");
  input_buffer.push("{\"id\":\"cancel\",\"type\":\"cancel\"}\n");
  input_buffer.close();
  rpc_thread.join();

  const auto jsonl = output_buffer.str();
  expect(result.has_value(), "RPC queue limit loop exits successfully");
  expect(count_substrings(jsonl, "\"name\":\"steer_queued\"") == 64 &&
             jsonl.find("\"id\":\"s64\"") != std::string::npos &&
             jsonl.find("RPC queued message limit exceeded") != std::string::npos &&
             jsonl.find("\"cleared_steer\":64") != std::string::npos,
         "RPC queue limit accepts bounded entries, rejects the next steer, and cancel clears the bounded queue");
}

void test_app_rpc_eof_clears_queued_follow_up_without_running() {
  const auto root = temp_root() / "app-rpc-eof-clears-follow-up";
  std::error_code remove_error;
  std::filesystem::remove_all(root, remove_error);
  const auto workspace = root / "workspace";
  const auto paths = app_test_paths(root);
  std::filesystem::create_directories(workspace);
  const auto outside_path = root / "outside.txt";
  {
    std::ofstream file(outside_path, std::ios::binary | std::ios::trunc);
    file << "outside eof note";
  }

  ava::app::RuntimeOpenOptions open_options;
  open_options.workspace_dir = workspace;
  open_options.current_dir = workspace;
  open_options.paths = paths;
  auto session = ava::app::open_runtime_session(open_options);
  expect(session.has_value(), "RPC EOF queue cleanup test opens runtime session");
  if (!session) return;

  const ava::provider::OpenAIProvider provider("https://api.example.test");
  ava::tests::FakeTransport transport({sse_response(read_file_call_sse(outside_path.generic_string())),
                                       sse_response(final_text_sse("would only run if not canceled"))});
  ava::app::RuntimeRunOptions runtime_options;
  runtime_options.access_token = "token";
  BlockingInputBuf input_buffer;
  std::istream in(&input_buffer);
  ThreadSafeStringBuf output_buffer;
  std::ostream out(&output_buffer);
  ava::core::VoidResult result;
  std::jthread rpc_thread(
      [&] { result = ava::app::run_rpc_loop(*session, open_options, provider, transport, runtime_options, in, out); });

  input_buffer.push("{\"id\":\"p1\",\"type\":\"prompt\",\"message\":\"read before eof\"}\n");
  const bool requested = output_buffer.wait_contains("\"name\":\"permission_requested\"", std::chrono::seconds(2));
  expect(requested, "RPC EOF queue cleanup test observes active permission wait");
  input_buffer.push("{\"id\":\"fu1\",\"type\":\"follow_up\",\"message\":\"must not run\"}\n");
  const bool queued = output_buffer.wait_contains("\"name\":\"follow_up_queued\"", std::chrono::seconds(2));
  expect(queued, "RPC EOF queue cleanup test observes queued follow-up");
  input_buffer.close();
  rpc_thread.join();

  const auto jsonl = output_buffer.str();
  expect(result.has_value(), "RPC EOF queue cleanup loop exits successfully");
  expect(transport.requests().size() == 1,
         "RPC EOF cancels active prompt and prevents queued follow-up provider calls");
  expect(jsonl.find("\"name\":\"follow_up_skipped\"") != std::string::npos &&
             jsonl.find("\"reason\":\"canceled\"") != std::string::npos &&
             jsonl.find("\"id\":\"fu1\"") != std::string::npos &&
             jsonl.find("agent loop canceled") != std::string::npos,
         "RPC EOF clears queued follow-up and sends its canceled response");
}

void test_app_rpc_cancel_clears_queued_steer_and_follow_up() {
  const auto root = temp_root() / "app-rpc-queue-cancel";
  std::error_code remove_error;
  std::filesystem::remove_all(root, remove_error);
  const auto workspace = root / "workspace";
  const auto paths = app_test_paths(root);
  std::filesystem::create_directories(workspace);
  const auto outside_path = root / "outside.txt";
  {
    std::ofstream file(outside_path, std::ios::binary | std::ios::trunc);
    file << "outside cancel queue note";
  }

  ava::app::RuntimeOpenOptions open_options;
  open_options.workspace_dir = workspace;
  open_options.current_dir = workspace;
  open_options.paths = paths;
  auto session = ava::app::open_runtime_session(open_options);
  expect(session.has_value(), "RPC queued cancel test opens runtime session");
  if (!session) return;

  const ava::provider::OpenAIProvider provider("https://api.example.test");
  ava::tests::FakeTransport transport({sse_response(read_file_call_sse(outside_path.generic_string()))});
  ava::app::RuntimeRunOptions runtime_options;
  runtime_options.access_token = "token";
  BlockingInputBuf input_buffer;
  std::istream in(&input_buffer);
  ThreadSafeStringBuf output_buffer;
  std::ostream out(&output_buffer);
  ava::core::VoidResult result;
  std::jthread rpc_thread(
      [&] { result = ava::app::run_rpc_loop(*session, open_options, provider, transport, runtime_options, in, out); });

  input_buffer.push("{\"id\":\"p1\",\"type\":\"prompt\",\"message\":\"read before cancel\"}\n");
  const bool requested = output_buffer.wait_contains("\"name\":\"permission_requested\"", std::chrono::seconds(2));
  expect(requested, "RPC queued cancel test observes active permission wait");
  input_buffer.push("{\"id\":\"s1\",\"type\":\"steer\",\"message\":\"never apply\"}\n");
  input_buffer.push("{\"id\":\"fu1\",\"type\":\"follow_up\",\"message\":\"never run\"}\n");
  const bool queued = output_buffer.wait_contains("\"name\":\"follow_up_queued\"", std::chrono::seconds(2));
  expect(queued, "RPC queued cancel test observes queued follow-up");
  input_buffer.push("{\"id\":\"cancel\",\"type\":\"cancel\"}\n");
  input_buffer.close();
  rpc_thread.join();

  const auto jsonl = output_buffer.str();
  expect(result.has_value(), "RPC queued cancel loop exits successfully");
  expect(transport.requests().size() == 1, "RPC cancel prevents queued steer/follow-up provider requests");
  expect(jsonl.find("\"name\":\"steer_skipped\"") != std::string::npos &&
             jsonl.find("\"name\":\"follow_up_skipped\"") != std::string::npos &&
             jsonl.find("\"cleared_steer\":1") != std::string::npos &&
             jsonl.find("\"cleared_follow_up\":1") != std::string::npos &&
             jsonl.find("\"id\":\"fu1\"") != std::string::npos &&
             jsonl.find("agent loop canceled") != std::string::npos,
         "RPC cancel clears queued steer/follow-up items and reports skipped outcomes");
}

void test_app_rpc_active_prompt_rejects_second_prompt_and_session_switch() {
  const auto root = temp_root() / "app-rpc-active-rejects";
  std::error_code remove_error;
  std::filesystem::remove_all(root, remove_error);
  const auto workspace = root / "workspace";
  const auto paths = app_test_paths(root);
  std::filesystem::create_directories(workspace);
  {
    std::ofstream file(workspace / "note.txt", std::ios::binary | std::ios::trunc);
    file << "rpc active reject note";
  }
  const auto outside_path = root / "outside.txt";
  {
    std::ofstream file(outside_path, std::ios::binary | std::ios::trunc);
    file << "outside reject note";
  }

  ava::app::RuntimeOpenOptions open_options;
  open_options.workspace_dir = workspace;
  open_options.current_dir = workspace;
  open_options.paths = paths;
  auto session = ava::app::open_runtime_session(open_options);
  auto other = ava::app::open_runtime_session(open_options);
  expect(session.has_value() && other.has_value(), "RPC active reject test opens runtime sessions");
  if (!session || !other) return;

  const ava::provider::OpenAIProvider provider("https://api.example.test");
  ava::tests::FakeTransport transport({sse_response(read_file_call_sse(outside_path.generic_string()))});
  ava::app::RuntimeRunOptions runtime_options;
  runtime_options.access_token = "token";
  BlockingInputBuf input_buffer;
  std::istream in(&input_buffer);
  ThreadSafeStringBuf output_buffer;
  std::ostream out(&output_buffer);
  ava::core::VoidResult result;
  std::jthread rpc_thread(
      [&] { result = ava::app::run_rpc_loop(*session, open_options, provider, transport, runtime_options, in, out); });

  input_buffer.push("{\"id\":\"p1\",\"type\":\"prompt\",\"message\":\"read note\"}\n");
  const bool requested = output_buffer.wait_contains("\"name\":\"permission_requested\"", std::chrono::seconds(2));
  expect(requested, "RPC active reject test observes pending permission request");
  input_buffer.push("{\"id\":\"p2\",\"type\":\"prompt\",\"message\":\"second\"}\n");
  input_buffer.push("{\"id\":\"new\",\"type\":\"new_session\"}\n");
  input_buffer.push("{\"id\":\"switch\",\"type\":\"switch_session\",\"session_id\":\"" + other->store.session_id() +
                    "\"}\n");
  input_buffer.push("{\"id\":\"messages\",\"type\":\"get_messages\"}\n");
  input_buffer.push("{\"id\":\"cancel\",\"type\":\"cancel\"}\n");
  input_buffer.close();
  rpc_thread.join();

  const auto jsonl = output_buffer.str();
  expect(result.has_value(), "RPC active reject loop exits successfully");
  expect(jsonl.find("\"id\":\"p2\"") != std::string::npos && jsonl.find("\"id\":\"new\"") != std::string::npos &&
             jsonl.find("\"id\":\"switch\"") != std::string::npos &&
             jsonl.find("\"id\":\"messages\"") != std::string::npos &&
             jsonl.find("RPC command is unavailable while a prompt is active") != std::string::npos,
         "RPC rejects active-run mutations and session materialization queries");
}

void test_app_rpc_permission_policy_auto_allows_before_resolver_event() {
  const auto root = temp_root() / "app-rpc-policy-auto-allow";
  std::error_code remove_error;
  std::filesystem::remove_all(root, remove_error);
  const auto workspace = root / "workspace";
  const auto paths = app_test_paths(root);
  std::filesystem::create_directories(workspace);
  const auto outside_path = root / "outside.txt";
  {
    std::ofstream file(outside_path, std::ios::binary | std::ios::trunc);
    file << "outside permission note";
  }

  ava::app::RuntimeOpenOptions open_options;
  open_options.workspace_dir = workspace;
  open_options.current_dir = workspace;
  open_options.paths = paths;
  auto session = ava::app::open_runtime_session(open_options);
  expect(session.has_value(), "RPC permission policy auto-allow test opens runtime session");
  if (!session) return;

  const ava::provider::OpenAIProvider provider("https://api.example.test");
  ava::tests::FakeTransport transport({sse_response(read_file_call_sse(outside_path.generic_string())),
                                       sse_response(final_text_sse("after policy allow"))});
  ava::app::HeadlessPermissionPolicyOptions policy_options;
  auto read_only_added = ava::app::add_headless_allow_policy(policy_options, "read-only");
  expect(read_only_added.has_value(), "RPC permission policy test configures read-only allow");
  ava::app::RuntimeRunOptions runtime_options;
  runtime_options.access_token = "token";
  runtime_options.permission_resolver = ava::app::build_headless_permission_resolver(policy_options);
  BlockingInputBuf input_buffer;
  std::istream in(&input_buffer);
  ThreadSafeStringBuf output_buffer;
  std::ostream out(&output_buffer);
  ava::core::VoidResult result;
  std::jthread rpc_thread(
      [&] { result = ava::app::run_rpc_loop(*session, open_options, provider, transport, runtime_options, in, out); });

  input_buffer.push("{\"id\":\"p1\",\"type\":\"prompt\",\"message\":\"read outside\"}\n");
  const bool completed = output_buffer.wait_contains("after policy allow", std::chrono::seconds(2));
  input_buffer.close();
  rpc_thread.join();

  const auto jsonl = output_buffer.str();
  expect(result.has_value() && completed, "RPC permission policy auto-allow prompt completes");
  expect(jsonl.find("permission_requested") == std::string::npos,
         "RPC permission policy auto-allows matching read prompt before resolver event");
}

void test_app_rpc_permission_reply_allow_and_deny_flows() {
  for (std::string_view decision : {"allow", "deny"}) {
    const auto decision_text = std::string(decision);
    const auto root = temp_root() / ("app-rpc-permission-" + decision_text);
    std::error_code remove_error;
    std::filesystem::remove_all(root, remove_error);
    const auto workspace = root / "workspace";
    const auto paths = app_test_paths(root);
    std::filesystem::create_directories(workspace);
    {
      std::ofstream file(workspace / "note.txt", std::ios::binary | std::ios::trunc);
      file << "rpc permission note";
    }
    const auto outside_path = root / "outside.txt";
    {
      std::ofstream file(outside_path, std::ios::binary | std::ios::trunc);
      file << "outside permission note";
    }

    ava::app::RuntimeOpenOptions open_options;
    open_options.workspace_dir = workspace;
    open_options.current_dir = workspace;
    open_options.paths = paths;
    auto session = ava::app::open_runtime_session(open_options);
    expect(session.has_value(), "RPC permission reply test opens runtime session");
    if (!session) return;

    const ava::provider::OpenAIProvider provider("https://api.example.test");
    ava::tests::FakeTransport transport({sse_response(read_file_call_sse(outside_path.generic_string())),
                                         sse_response(final_text_sse("after " + decision_text))});
    ava::app::RuntimeRunOptions runtime_options;
    runtime_options.access_token = "token";
    BlockingInputBuf input_buffer;
    std::istream in(&input_buffer);
    ThreadSafeStringBuf output_buffer;
    std::ostream out(&output_buffer);
    ava::core::VoidResult result;
    std::jthread rpc_thread([&] {
      result = ava::app::run_rpc_loop(*session, open_options, provider, transport, runtime_options, in, out);
    });

    input_buffer.push("{\"id\":\"p1\",\"type\":\"prompt\",\"message\":\"read note\"}\n");
    const bool requested =
        output_buffer.wait_contains("\"resolver_request_id\":\"permission_", std::chrono::seconds(2));
    const auto resolver_request_id = extract_json_string_field(output_buffer.str(), "resolver_request_id");
    expect(requested && !resolver_request_id.empty(), "RPC permission reply test observes resolver request id");
    input_buffer.push("{\"id\":\"reply\",\"type\":\"permission_reply\",\"request_id\":\"" + resolver_request_id +
                      "\",\"correlation_id\":\"p1\",\"decision\":\"" + decision_text + "\"}\n");
    const bool completed = output_buffer.wait_contains("after " + decision_text, std::chrono::seconds(2));
    input_buffer.close();
    rpc_thread.join();

    const auto jsonl = output_buffer.str();
    expect(result.has_value() && completed, "RPC permission reply loop exits successfully");
    expect(jsonl.find("\"id\":\"reply\"") != std::string::npos && jsonl.find("\"success\":true") != std::string::npos &&
               jsonl.find("after " + decision_text) != std::string::npos &&
               jsonl.find("\"name\":\"permission_replied\"") != std::string::npos &&
               jsonl.find("\"decision\":\"" + decision_text + "\"") != std::string::npos,
           "RPC permission " + decision_text + " reply emits a reply event and unblocks the run");
  }
}

void test_app_rpc_permission_request_includes_mutation_diff_preview() {
  const auto root = temp_root() / "app-rpc-permission-diff";
  std::error_code remove_error;
  std::filesystem::remove_all(root, remove_error);
  const auto workspace = root / "workspace";
  const auto paths = app_test_paths(root);
  std::filesystem::create_directories(workspace);
  const auto outside_path = root / "outside-created.txt";

  ava::app::RuntimeOpenOptions open_options;
  open_options.workspace_dir = workspace;
  open_options.current_dir = workspace;
  open_options.paths = paths;
  auto session = ava::app::open_runtime_session(open_options);
  expect(session.has_value(), "RPC permission diff test opens runtime session");
  if (!session) return;

  const ava::provider::OpenAIProvider provider("https://api.example.test");
  ava::tests::FakeTransport transport({sse_response(write_file_call_sse(outside_path.generic_string(), "rpc new\n")),
                                       sse_response(final_text_sse("after diff deny"))});
  ava::app::RuntimeRunOptions runtime_options;
  runtime_options.access_token = "token";
  BlockingInputBuf input_buffer;
  std::istream in(&input_buffer);
  ThreadSafeStringBuf output_buffer;
  std::ostream out(&output_buffer);
  ava::core::VoidResult result;
  std::jthread rpc_thread(
      [&] { result = ava::app::run_rpc_loop(*session, open_options, provider, transport, runtime_options, in, out); });

  input_buffer.push("{\"id\":\"p1\",\"type\":\"prompt\",\"message\":\"write outside\"}\n");
  const bool requested = output_buffer.wait_contains("\"diff_preview\"", std::chrono::seconds(2));
  const auto resolver_request_id = extract_json_string_field(output_buffer.str(), "resolver_request_id");
  expect(requested && !resolver_request_id.empty(), "RPC permission diff test observes mutation diff preview");
  input_buffer.push("{\"id\":\"reply\",\"type\":\"permission_reply\",\"request_id\":\"" + resolver_request_id +
                    "\",\"correlation_id\":\"p1\",\"decision\":\"deny\"}\n");
  const bool completed = output_buffer.wait_contains("after diff deny", std::chrono::seconds(2));
  input_buffer.close();
  rpc_thread.join();

  const auto jsonl = output_buffer.str();
  expect(result.has_value() && completed && !std::filesystem::exists(outside_path),
         "RPC permission diff test completes after denied mutation without writing");
  expect(jsonl.find("\"name\":\"permission_requested\"") != std::string::npos &&
             jsonl.find("\"diff_preview\"") != std::string::npos && jsonl.find("+rpc new") != std::string::npos &&
             jsonl.find("\"diff_truncated\":false") != std::string::npos &&
             jsonl.find("\"name\":\"permission_replied\"") != std::string::npos &&
             jsonl.find("\"decision\":\"deny\"") != std::string::npos,
         "RPC permission request payload includes backend-provided unified diff preview and reply event");
}

void test_app_rpc_question_reply_flow() {
  const auto root = temp_root() / "app-rpc-question";
  std::error_code remove_error;
  std::filesystem::remove_all(root, remove_error);
  const auto workspace = root / "workspace";
  const auto paths = app_test_paths(root);
  std::filesystem::create_directories(workspace);

  ava::app::RuntimeOpenOptions open_options;
  open_options.workspace_dir = workspace;
  open_options.current_dir = workspace;
  open_options.paths = paths;
  auto session = ava::app::open_runtime_session(open_options);
  expect(session.has_value(), "RPC question reply test opens runtime session");
  if (!session) return;

  const ava::provider::OpenAIProvider provider("https://api.example.test");
  ava::tests::FakeTransport transport(
      {sse_response(question_call_sse()), sse_response(final_text_sse("question done"))});
  ava::app::RuntimeRunOptions runtime_options;
  runtime_options.access_token = "token";
  BlockingInputBuf input_buffer;
  std::istream in(&input_buffer);
  ThreadSafeStringBuf output_buffer;
  std::ostream out(&output_buffer);
  ava::core::VoidResult result;
  std::jthread rpc_thread(
      [&] { result = ava::app::run_rpc_loop(*session, open_options, provider, transport, runtime_options, in, out); });

  input_buffer.push("{\"id\":\"p1\",\"type\":\"prompt\",\"message\":\"ask question\"}\n");
  const bool requested = output_buffer.wait_contains("\"name\":\"question_requested\"", std::chrono::seconds(2));
  const auto resolver_request_id = extract_json_string_field(output_buffer.str(), "resolver_request_id");
  expect(requested && !resolver_request_id.empty(), "RPC question reply test observes question request event");
  input_buffer.push("{\"id\":\"reply\",\"type\":\"question_reply\",\"request_id\":\"" + resolver_request_id +
                    "\",\"correlation_id\":\"p1\",\"answer\":\"custom ok\"}\n");
  const bool completed = output_buffer.wait_contains("question done", std::chrono::seconds(2));
  input_buffer.close();
  rpc_thread.join();

  const auto jsonl = output_buffer.str();
  expect(result.has_value() && completed, "RPC question reply loop exits successfully");
  expect(jsonl.find("\"id\":\"reply\"") != std::string::npos && jsonl.find("\"success\":true") != std::string::npos &&
             jsonl.find("question done") != std::string::npos &&
             jsonl.find("\"name\":\"question_replied\"") != std::string::npos &&
             jsonl.find("\"answer\":\"custom ok\"") != std::string::npos,
         "RPC question reply emits a reply event and unblocks question tool");
}

void test_app_rpc_question_reply_selected_option_flow() {
  const auto root = temp_root() / "app-rpc-question-selected";
  std::error_code remove_error;
  std::filesystem::remove_all(root, remove_error);
  const auto workspace = root / "workspace";
  const auto paths = app_test_paths(root);
  std::filesystem::create_directories(workspace);

  ava::app::RuntimeOpenOptions open_options;
  open_options.workspace_dir = workspace;
  open_options.current_dir = workspace;
  open_options.paths = paths;
  auto session = ava::app::open_runtime_session(open_options);
  expect(session.has_value(), "RPC selected question reply test opens runtime session");
  if (!session) return;

  const ava::provider::OpenAIProvider provider("https://api.example.test");
  ava::tests::FakeTransport transport(
      {sse_response(question_call_sse()), sse_response(final_text_sse("selected question done"))});
  ava::app::RuntimeRunOptions runtime_options;
  runtime_options.access_token = "token";
  BlockingInputBuf input_buffer;
  std::istream in(&input_buffer);
  ThreadSafeStringBuf output_buffer;
  std::ostream out(&output_buffer);
  ava::core::VoidResult result;
  std::jthread rpc_thread(
      [&] { result = ava::app::run_rpc_loop(*session, open_options, provider, transport, runtime_options, in, out); });

  input_buffer.push("{\"id\":\"p1\",\"type\":\"prompt\",\"message\":\"ask question\"}\n");
  const bool requested = output_buffer.wait_contains("\"name\":\"question_requested\"", std::chrono::seconds(2));
  const auto resolver_request_id = extract_json_string_field(output_buffer.str(), "resolver_request_id");
  expect(requested && !resolver_request_id.empty(), "RPC selected question reply observes request event");
  input_buffer.push("{\"id\":\"bad\",\"type\":\"question_reply\",\"request_id\":\"" + resolver_request_id +
                    "\",\"correlation_id\":\"p1\",\"selected\":\"no\"}\n");
  const bool rejected =
      output_buffer.wait_contains("question_reply selected option is not valid", std::chrono::seconds(2));
  input_buffer.push("{\"id\":\"reply\",\"type\":\"question_reply\",\"request_id\":\"" + resolver_request_id +
                    "\",\"correlation_id\":\"p1\",\"selected\":\"yes\"}\n");
  const bool completed = output_buffer.wait_contains("selected question done", std::chrono::seconds(2));
  input_buffer.close();
  rpc_thread.join();

  const auto jsonl = output_buffer.str();
  expect(result.has_value() && rejected && completed, "RPC selected question reply loop exits successfully");
  expect(jsonl.find("\"id\":\"bad\"") != std::string::npos &&
             jsonl.find("question_reply selected option is not valid") != std::string::npos,
         "RPC selected question reply rejects invalid selected option without resolving request");
  expect(jsonl.find("\"id\":\"reply\"") != std::string::npos && jsonl.find("\"success\":true") != std::string::npos &&
             jsonl.find("selected question done") != std::string::npos &&
             jsonl.find("\"name\":\"question_replied\"") != std::string::npos &&
             jsonl.find("\"selected\":\"yes\"") != std::string::npos,
         "RPC selected question reply emits a reply event and unblocks question tool");
}

}  // namespace

void run_app_command_classification_tests() { test_command_classification(); }

void run_app_event_serialization_tests() {
  test_app_event_serialization();
  test_app_rpc_prompt_payload_serialization();
}

void run_app_runtime_tests() {
  test_app_runtime_open_session_and_context_prompt();
  test_app_run_prompt_emits_events();
  test_app_run_prompt_emits_provider_retry_events_when_enabled();
  test_app_run_prompt_emits_tool_progress_and_session_spill();
  test_app_run_prompt_event_sink_failure_cancels_before_next_provider_call();
  test_app_print_prompt_merging();
  test_headless_permission_policy();
  test_app_print_text_mode_outputs_final_text_only();
  test_app_print_text_mode_with_streaming_keeps_stdout_final_only();
  test_app_print_text_mode_reports_stdout_write_failure();
  test_app_print_mode_uses_headless_permission_policy();
  test_app_print_mode_refreshes_expired_oauth_before_provider_request();
  test_app_connect_provider_credentials_headlessly();
  test_app_print_json_mode_outputs_runtime_events();
  test_app_print_json_mode_streams_provider_deltas_before_final_message();
  test_app_command_dispatcher();
  test_app_compact_provider_summary_success();
  test_app_compact_openai_oauth_streaming_summary_success();
  test_app_compact_provider_failure_leaves_session_untouched();
  test_app_compact_oversized_summary_leaves_session_untouched();
  test_app_compaction_prompt_builder_sections();
  test_app_auto_compaction_appends_summary_and_rebuilds_context();
  test_app_auto_compaction_recent_context_respects_token_budget();
  test_app_auto_compaction_recent_context_truncates_utf8_safely();
  test_app_auto_compaction_explicit_zero_disables();
  test_app_auto_compaction_uses_default_threshold_without_context_window_metadata();
  test_app_auto_compaction_retries_stale_snapshot_before_append();
  test_app_auto_compaction_repeated_stale_snapshot_fails_without_append();
  test_app_context_overflow_compacts_and_retries_once_successfully();
  test_app_context_overflow_compaction_failure_leaves_no_partial_entry();
  test_app_non_overflow_provider_error_does_not_compact_or_retry();
  test_app_context_overflow_retry_is_bounded();
  test_app_rpc_parsing_and_response_serialization();
  test_app_rpc_identifier_validation();
  test_app_rpc_prompt_with_fake_transport_streams_events();
  test_app_rpc_prompt_streams_provider_deltas_before_final_response();
  test_app_rpc_prompt_refreshes_expired_oauth_before_provider_request();
  test_app_rpc_malformed_line_recovery_and_unknown_command();
  test_app_rpc_state_list_sessions_and_open_session();
  test_app_runtime_model_switch_persists_and_reopens();
  test_app_runtime_model_switch_rejects_incompatible_history();
  test_app_runtime_reasoning_selection_persists_and_requests();
  test_app_rpc_model_commands();
  test_app_rpc_reasoning_commands();
  test_app_rpc_protocol_version_and_session_commands();
  test_app_rpc_protocol_version_and_resolver_reply_errors();
  test_app_rpc_mcp_command_responses();
  test_app_rpc_command_responses_for_context_compact_export();
  test_app_rpc_compact_provider_failure_is_error_response();
  test_app_rpc_cancel_affects_subsequent_prompt();
  test_app_rpc_active_prompt_cancel_unblocks_pending_permission();
  test_app_rpc_steer_applies_before_next_provider_request();
  test_app_rpc_follow_up_runs_after_active_prompt();
  test_app_rpc_prompt_start_failure_cleans_queued_messages();
  test_app_rpc_steer_after_follow_up_started_targets_follow_up();
  test_app_rpc_queue_limit_rejects_new_items();
  test_app_rpc_eof_clears_queued_follow_up_without_running();
  test_app_rpc_cancel_clears_queued_steer_and_follow_up();
  test_app_rpc_active_prompt_rejects_second_prompt_and_session_switch();
  test_app_rpc_permission_policy_auto_allows_before_resolver_event();
  test_app_rpc_permission_reply_allow_and_deny_flows();
  test_app_rpc_permission_request_includes_mutation_diff_preview();
  test_app_rpc_question_reply_flow();
  test_app_rpc_question_reply_selected_option_flow();
}
