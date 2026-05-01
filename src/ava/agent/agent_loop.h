#pragma once

#include <cstddef>
#include <filesystem>
#include <functional>
#include <mutex>
#include <string>
#include <vector>

#include "ava/agent/mode.h"
#include "ava/agent/question.h"
#include "ava/core/result.h"
#include "ava/permissions/permission.h"
#include "ava/provider/provider.h"
#include "ava/session/session_store.h"

namespace ava::agent {

enum class ToolTimelineStatus {
  Running,
  Success,
  Error,
};

struct ToolTimelineEntry {
  ToolTimelineStatus status = ToolTimelineStatus::Running;
  std::string call_id = {};
  std::string name = {};
  std::string argument_summary = {};
  std::string result_summary = {};
};

[[nodiscard]] std::string to_string(ToolTimelineStatus status);

struct AgentLoopOptions {
  std::filesystem::path workspace_dir;
  Mode mode = Mode::Build;
  std::string provider_id = "openai";
  std::string model_id = "gpt-5.5";
  std::string system_prompt;
  std::string access_token;
  bool openai_oauth = false;
  std::string openai_account_id = "";
  std::size_t max_tool_iterations = 10;
  std::size_t max_provider_events = 4096;
  std::size_t max_assistant_text_bytes = 256 * 1024;
  std::size_t max_tool_argument_bytes = 256 * 1024;
  std::size_t max_tool_result_context_bytes = 8 * 1024;
  bool stream = true;
  std::function<void(const ToolTimelineEntry&)> on_tool_event = nullptr;
  std::function<ava::core::VoidResult(const ava::provider::StreamEvent&)> on_stream_event = nullptr;
  ava::permissions::PermissionResolver permission_resolver = nullptr;
  QuestionResolver question_resolver = nullptr;
  std::function<bool()> cancel_requested = nullptr;
  std::function<ava::core::Result<std::vector<std::string>>()> take_steering_messages = nullptr;
  std::mutex* session_mutex = nullptr;
};

struct AgentLoopResult {
  std::string final_text;
  std::size_t provider_iterations = 0;
  std::size_t tool_calls = 0;
  std::size_t initial_context_messages = 0;
  bool used_compacted_context = false;
  std::size_t tool_iterations = 0;
  std::string stop_reason = "unknown";
  std::vector<ToolTimelineEntry> tool_timeline;
};

struct MessageBuildOptions {
  std::size_t max_tool_result_context_bytes = 8 * 1024;
};

[[nodiscard]] ava::core::Result<std::vector<ava::provider::ChatMessage>> build_provider_messages_from_entries(
    const std::vector<ava::session::SessionEntry>& entries, MessageBuildOptions options = MessageBuildOptions{});

class AgentLoop {
 public:
  explicit AgentLoop(AgentLoopOptions options);

  [[nodiscard]] ava::core::Result<AgentLoopResult> run_turn(const std::string& user_message,
                                                            ava::session::SessionStore& store,
                                                            const ava::provider::Provider& provider,
                                                            ava::provider::Transport& transport);

 private:
  AgentLoopOptions options_;
};

}  // namespace ava::agent
