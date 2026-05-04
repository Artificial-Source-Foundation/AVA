#pragma once

#include <span>
#include <string>
#include <vector>

#include "ava/agent/tool_metadata.h"
#include "ava/agent/tool_registry.h"
#include "ava/agent/tool_types.h"
#include "ava/core/result.h"
#include "ava/tools/file_tools.h"

namespace ava::agent {

class ToolDispatcher {
 public:
  explicit ToolDispatcher(ava::tools::ToolContext context);

  [[nodiscard]] ava::core::Result<ToolDispatchResult> dispatch(const ProviderToolCall& call) const;
  [[nodiscard]] static std::span<const ToolMetadata> tool_metadata();
  [[nodiscard]] static std::vector<ToolMetadata> tool_metadata(const ava::tools::ToolContext& context);
  [[nodiscard]] static std::vector<std::string> tool_schemas_json();
  [[nodiscard]] static std::vector<std::string> tool_schemas_json(const ava::tools::ToolContext& context);

 private:
  ava::tools::ToolContext context_;
  ToolRegistry registry_;
};

}  // namespace ava::agent
