#include "ava/tools/core_tools.hpp"

#include "ava/tools/bash_tool.hpp"
#include "ava/tools/edit_tool.hpp"
#include "ava/tools/git_read_tool.hpp"
#include "ava/tools/read_tool.hpp"
#include "ava/tools/search_tools.hpp"
#include "ava/tools/todo_tools.hpp"
#include "ava/tools/tool_search_tool.hpp"
#include "ava/tools/write_tool.hpp"

#include <filesystem>
#include <memory>

namespace ava::tools {

DefaultToolRegistration register_default_tools(ToolRegistry& registry, const std::filesystem::path& workspace_root) {
  auto backup_session = std::make_shared<FileBackupSession>(workspace_root);
  auto todo_state = std::make_shared<TodoListState>();

  registry.register_tool(std::make_unique<ReadTool>(workspace_root));
  registry.register_tool(std::make_unique<WriteTool>(workspace_root, backup_session));
  registry.register_tool(std::make_unique<EditTool>(workspace_root, backup_session));
  registry.register_tool(std::make_unique<BashTool>(workspace_root));
  registry.register_tool(std::make_unique<GlobTool>(workspace_root));
  registry.register_tool(std::make_unique<GrepTool>(workspace_root));
  registry.register_tool(std::make_unique<GitReadTool>(workspace_root));
  registry.register_tool(std::make_unique<GitReadAliasTool>(workspace_root));
  registry.register_tool(std::make_unique<TodoWriteTool>(todo_state));
  registry.register_tool(std::make_unique<TodoReadTool>(todo_state));
  registry.register_tool(std::make_unique<ToolSearchTool>(registry));

  return DefaultToolRegistration{.backup_session = std::move(backup_session)};
}

}  // namespace ava::tools
