#pragma once

#include <cstddef>
#include <filesystem>
#include <string>

#include "ava/agent/mode.h"
#include "ava/core/result.h"
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
  std::size_t max_tool_iterations = 10;
  std::size_t max_provider_events = 4096;
  std::size_t max_assistant_text_bytes = 256 * 1024;
  std::size_t max_tool_argument_bytes = 256 * 1024;
  std::size_t max_tool_result_context_bytes = 8 * 1024;
  bool stream = true;
};

struct AgentLoopResult {
  std::string final_text;
  std::size_t provider_iterations = 0;
  std::size_t tool_calls = 0;
};

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
