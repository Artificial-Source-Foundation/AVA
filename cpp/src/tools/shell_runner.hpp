#pragma once

#include <cstdint>
#include <filesystem>
#include <string>

namespace ava::tools {

struct CommandOutcome {
  std::string output;
  int exit_code{1};
};

[[nodiscard]] CommandOutcome run_shell_command(
    const std::string& command,
    const std::filesystem::path& cwd,
    std::uint64_t timeout_ms
);

[[nodiscard]] std::string render_shell_result(const CommandOutcome& outcome);

}  // namespace ava::tools
