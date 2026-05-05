#pragma once

#include "ava/app/command_catalog.h"
#include "ava/app/events.h"
#include "ava/app/runtime.h"

#include "ava/agent/agent_loop.h"

#include "ava/permissions/permission.h"

#include "ava/core/result.h"

#include <mutex>
#include <string>
#include <string_view>
#include <vector>

namespace ava::app {

struct CommandRequest {
  std::string command;
  RuntimeEventSink event_sink = nullptr;
  ava::permissions::PermissionResolver permission_resolver = nullptr;
  ava::agent::QuestionResolver question_resolver = nullptr;
  CompactionSummaryGenerator compaction_summary_generator = nullptr;
  std::mutex* session_mutex = nullptr;
  bool propagate_compaction_errors = false;
  std::vector<CommandHotkey> hotkeys = {};
};

struct CommandResult {
  bool handled = false;
  bool quit = false;
  std::vector<std::string> output;
  std::vector<ava::agent::ToolTimelineEntry> tool_timeline;
};

[[nodiscard]] bool is_backend_command(std::string_view line) noexcept;
[[nodiscard]] std::string command_help_text(std::vector<CommandHotkey> const& hotkeys = {});
[[nodiscard]] std::string command_hotkeys_text(std::vector<CommandHotkey> const& hotkeys = {});
[[nodiscard]] ava::core::Result<CommandResult> run_command(RuntimeSession& session, CommandRequest request);

}  // namespace ava::app
