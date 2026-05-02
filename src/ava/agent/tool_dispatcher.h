#pragma once

#include <string>
#include <span>
#include <vector>

#include "ava/agent/tool_metadata.h"
#include "ava/core/result.h"
#include "ava/tools/file_tools.h"

namespace ava::agent {

struct ProviderToolCall {
  std::string id;
  std::string name;
  std::string arguments_json;
};

struct ToolDispatchResult {
  std::string call_id;
  std::string name;
  bool success = false;
  std::string result_text;
};

class ToolDispatcher {
 public:
  explicit ToolDispatcher(ava::tools::ToolContext context);

  [[nodiscard]] ava::core::Result<ToolDispatchResult> dispatch(const ProviderToolCall& call) const;
  [[nodiscard]] static std::span<const ToolMetadata> tool_metadata();
  [[nodiscard]] static std::vector<std::string> tool_schemas_json();
  [[nodiscard]] static std::vector<std::string> tool_schemas_json(const ava::tools::ToolContext& context);

 private:
  ava::tools::ToolContext context_;
};

}  // namespace ava::agent
