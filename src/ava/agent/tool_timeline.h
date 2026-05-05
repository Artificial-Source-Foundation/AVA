#pragma once

#include <cstddef>
#include <optional>
#include <string>
#include <vector>

#include "ava/agent/tool_types.h"
#include "ava/core/error.h"

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
  bool truncated = false;
  std::optional<std::size_t> output_bytes = std::nullopt;
  std::optional<std::size_t> total_bytes = std::nullopt;
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
[[nodiscard]] ToolDispatchResult synthetic_failed_dispatch_result(ProviderToolCall const& call,
                                                                  ava::core::Error const& error);
void populate_tool_timeline_metadata(ToolTimelineEntry& entry, ToolDispatchResult const& result);

}  // namespace ava::agent
