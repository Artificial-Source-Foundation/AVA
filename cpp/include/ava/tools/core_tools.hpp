#pragma once

// Default-tool registration seam. Concrete tool declarations live in their
// dedicated headers (for example, bash_tool.hpp and read_tool.hpp).

#include <filesystem>
#include <memory>

namespace ava::tools {

class FileBackupSession;
class ToolRegistry;

struct DefaultToolRegistration {
  std::shared_ptr<FileBackupSession> backup_session;
};

[[nodiscard]] DefaultToolRegistration register_default_tools(
    ToolRegistry& registry,
    const std::filesystem::path& workspace_root
);

}  // namespace ava::tools
