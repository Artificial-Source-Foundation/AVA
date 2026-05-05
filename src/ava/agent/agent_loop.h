#pragma once

#include <cstddef>
#include <filesystem>
#include <functional>
#include <mutex>
#include <optional>
#include <string>

#include "ava/agent/message_builder.h"
#include "ava/agent/mode.h"
#include "ava/agent/question.h"
#include "ava/agent/tool_timeline.h"
#include "ava/config/model_config.h"
#include "ava/core/result.h"
#include "ava/permissions/permission.h"
#include "ava/provider/provider.h"
#include "ava/session/session_store.h"

namespace ava::agent {

struct AgentLoopOptions {
  std::filesystem::path workspace_dir;
  Mode mode = Mode::Build;
  std::string provider_id = "openai";
  std::string model_id = "gpt-5.5";
  std::string system_prompt;
  std::string access_token;
  std::string credential_type = "bearer";
  bool openai_oauth = false;
  std::string openai_account_id = "";
  std::size_t max_tool_iterations = 10;
  std::size_t max_provider_events = 4096;
  std::size_t max_assistant_text_bytes = 256 * 1024;
  std::size_t max_tool_argument_bytes = 256 * 1024;
  std::size_t max_tool_result_context_bytes = 8 * 1024;
  bool stream = true;
  bool model_supports_tools = true;
  bool model_supports_streaming = true;
  std::optional<long long> model_max_output_tokens = std::nullopt;
  std::optional<ava::provider::ProviderReasoningOptions> reasoning = std::nullopt;
  std::function<void(ToolTimelineEntry const&)> on_tool_event = nullptr;
  std::function<ava::core::VoidResult(ToolProgressEntry const&)> on_tool_progress = nullptr;
  std::function<ava::core::VoidResult(ava::provider::StreamEvent const&)> on_stream_event = nullptr;
  ava::permissions::PermissionResolver permission_resolver = nullptr;
  QuestionResolver question_resolver = nullptr;
  std::function<bool()> cancel_requested = nullptr;
  std::function<ava::core::Result<std::vector<std::string>>()> take_steering_messages = nullptr;
  std::function<ava::core::Result<bool>(ava::session::SessionStore&, std::string_view,
                                        std::vector<std::string> const& replayed_user_messages)>
      compact_context = nullptr;
  std::mutex* session_mutex = nullptr;
  std::optional<ava::config::ModelPricing> model_pricing = std::nullopt;
};

struct AgentLoopResult {
  std::string final_text;
  std::optional<ava::provider::TokenUsage> usage = std::nullopt;
  std::optional<long double> cost_usd = std::nullopt;
  std::size_t provider_iterations = 0;
  std::size_t tool_calls = 0;
  std::size_t initial_context_messages = 0;
  bool used_compacted_context = false;
  std::size_t tool_iterations = 0;
  std::string stop_reason = "unknown";
  std::vector<ToolTimelineEntry> tool_timeline;
};

class AgentLoop {
 public:
  explicit AgentLoop(AgentLoopOptions options);

  [[nodiscard]] ava::core::Result<AgentLoopResult> run_turn(std::string const& user_message,
                                                            ava::session::SessionStore& store,
                                                            ava::provider::Provider const& provider,
                                                            ava::provider::Transport& transport);

 private:
  AgentLoopOptions options_;
};

}  // namespace ava::agent
