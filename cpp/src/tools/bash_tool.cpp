#include "ava/tools/bash_tool.hpp"

#include <cstdint>
#include <filesystem>
#include <stdexcept>
#include <string>

#include <nlohmann/json.hpp>

#include "ava/tools/output_fallback.hpp"
#include "ava/tools/path_guard.hpp"
#include "shell_runner.hpp"

namespace ava::tools {
namespace {

constexpr std::size_t kBashOutputBytes = 48 * 1024;

}  // namespace

BashTool::BashTool(std::filesystem::path workspace_root)
    : workspace_root_(normalize_workspace_root(workspace_root)) {}

std::string BashTool::name() const {
  return "bash";
}

std::string BashTool::description() const {
  return "Execute shell command";
}

std::string BashTool::search_hint() const {
  return "run execute shell command terminal bash timeout cwd";
}

nlohmann::json BashTool::parameters() const {
  return nlohmann::json{{"type", "object"},
                        {"required", nlohmann::json::array({"command"})},
                        {"properties",
                         {{"command", {{"type", "string"}, {"description", "Non-interactive shell command to execute"}}},
                          {"timeout_ms", {{"type", "integer"}, {"minimum", 1}, {"description", "Command timeout in milliseconds"}}},
                          {"cwd", {{"type", "string"}, {"description", "Working directory relative to the workspace root"}}}}}};
}

ava::types::ToolResult BashTool::execute(const nlohmann::json& args) const {
  if(!args.contains("command")) {
    throw std::runtime_error("missing required field: command");
  }

  const auto command = args.at("command").get<std::string>();
  const std::uint64_t timeout_ms = args.value("timeout_ms", kDefaultShellCommandTimeoutMs);
  const auto cwd_raw = args.value("cwd", std::string("."));
  const auto cwd = enforce_workspace_path(workspace_root_, cwd_raw, name());

  const auto outcome = run_shell_command(command, cwd, timeout_ms);
  auto content = render_shell_result(outcome);
  content = apply_output_fallback(name(), content, kBashOutputBytes);

  return ava::types::ToolResult{.call_id = "", .content = content, .is_error = outcome.exit_code != 0};
}

}  // namespace ava::tools
