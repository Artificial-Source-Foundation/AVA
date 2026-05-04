#pragma once

#include <cstddef>
#include <optional>
#include <string>
#include <vector>

namespace ava::agent {

struct ProviderToolCall {
  std::string id;
  std::string name;
  std::string arguments_json;
};

enum class ToolResultStatus {
  Success,
  Error,
  Canceled,
};

struct ToolResultPayload {
  ToolResultStatus status = ToolResultStatus::Success;
  std::string summary;
  std::string content;
  std::string content_type;
  std::string error_category;
  std::string error_code;
  std::string error_message;
  std::string error_details;
  std::string diff;
  std::vector<std::string> changed_paths;
  bool diff_truncated = false;
  bool truncated = false;
  std::optional<std::size_t> output_bytes = std::nullopt;
  std::optional<std::size_t> total_bytes = std::nullopt;
  std::optional<std::size_t> omitted_bytes = std::nullopt;
  std::optional<std::size_t> omitted_lines = std::nullopt;
  std::optional<std::size_t> visible_matches = std::nullopt;
  std::optional<std::size_t> total_matches = std::nullopt;
  std::string spill_path;
  bool spill_truncated = false;
};

struct ToolDispatchResult {
  std::string call_id;
  std::string name;
  bool success = false;
  std::string result_text;
  ToolResultPayload payload = {};
};

}  // namespace ava::agent
