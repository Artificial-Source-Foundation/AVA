#pragma once

#include "ava/agent/message_builder.h"
#include "ava/agent/mode.h"
#include "ava/agent/question.h"
#include "ava/agent/tool_visibility.h"

#include "ava/config/model_config.h"

#include "ava/session/attachments.h"
#include "ava/session/session_store.h"

#include "ava/permissions/permission.h"

#include "ava/provider/provider.h"

#include "ava/lsp/lsp_client.h"

#include "ava/core/result.h"

#include <cstddef>
#include <filesystem>
#include <functional>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

namespace ava::agent {

enum class ToolTimelineStatus {
  Running,
  Success,
  Canceled,
  Error,
};

struct ToolTimelineEntry {
  ToolTimelineStatus status = ToolTimelineStatus::Running;
  std::string call_id = {};
  std::string name = {};
  std::string argument_summary = {};
  std::string result_summary = {};
  std::string arguments_json = {};
  std::string result_json = {};
  std::string structured_result_json = {};
  std::string content_type = {};
  std::string error_category = {};
  std::string error_code = {};
  std::string error_message = {};
  std::string error_details = {};
  std::string diff = {};
  bool diff_truncated = false;
  std::vector<std::string> changed_paths = {};
  std::vector<std::string> permission_request_ids = {};
  bool truncated = false;
  bool byte_limited = false;
  bool line_limited = false;
  std::optional<std::size_t> output_bytes = std::nullopt;
  std::optional<std::size_t> total_bytes = std::nullopt;
  std::optional<std::size_t> output_lines = std::nullopt;
  std::optional<std::size_t> total_lines = std::nullopt;
  std::optional<std::size_t> start_line = std::nullopt;
  std::optional<std::size_t> end_line = std::nullopt;
  std::optional<std::size_t> next_offset_line = std::nullopt;
  std::optional<std::size_t> omitted_bytes = std::nullopt;
  std::optional<std::size_t> omitted_lines = std::nullopt;
  std::optional<std::size_t> visible_matches = std::nullopt;
  std::optional<std::size_t> total_matches = std::nullopt;
  std::string spill_path = {};
  bool spill_truncated = false;
};

struct ToolProgressEntry {
  std::string call_id = {};
  std::string name = {};
  std::string text = {};
  std::string status = "running";
};

[[nodiscard]] std::string to_string(ToolTimelineStatus status);

struct AgentLoopOptions {
  std::filesystem::path workspace_dir;
  std::filesystem::path current_dir = {};
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
  bool include_project_resources = true;
  ToolVisibilityOptions tool_visibility = {};
  std::vector<std::string> model_input_modalities = {"text"};
  std::optional<long long> model_max_output_tokens = std::nullopt;
  std::optional<ava::provider::ProviderReasoningOptions> reasoning = std::nullopt;
  std::function<void(ToolTimelineEntry const&)> on_tool_event = nullptr;
  std::function<ava::core::VoidResult(ToolProgressEntry const&)> on_tool_progress = nullptr;
  std::function<ava::core::VoidResult(ava::provider::StreamEvent const&)> on_stream_event = nullptr;
  ava::permissions::PermissionResolver permission_resolver = nullptr;
  QuestionResolver question_resolver = nullptr;
  std::function<bool()> cancel_requested = nullptr;
  std::function<ava::core::Result<std::vector<std::string>>()> take_steering_messages = nullptr;
  std::shared_ptr<ava::lsp::DiagnosticsProvider> lsp_diagnostics_provider = nullptr;
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
  [[nodiscard]] ava::core::Result<AgentLoopResult> run_turn(std::string const& user_message,
                                                            std::vector<ava::session::ImageAttachmentRef> const& image_attachments,
                                                            ava::session::SessionStore& store,
                                                            ava::provider::Provider const& provider,
                                                            ava::provider::Transport& transport);

 private:
  AgentLoopOptions options_;
};

}  // namespace ava::agent
