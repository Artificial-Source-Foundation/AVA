#include "ava/tools/read_tool.hpp"
#include "ava/tools/write_tool.hpp"

#include <algorithm>
#include <filesystem>
#include <iomanip>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

#include <nlohmann/json.hpp>

#include "ava/tools/file_backup.hpp"
#include "ava/tools/output_fallback.hpp"
#include "ava/tools/path_guard.hpp"
#include "file_io.hpp"

namespace ava::tools {
namespace {

constexpr std::size_t kReadDefaultLimit = 2000;
constexpr std::size_t kReadMaxLineLength = 2000;
constexpr std::size_t kReadOutputBytes = 48 * 1024;
constexpr std::uintmax_t kReadMaxFileBytes = 10 * 1024 * 1024;

[[nodiscard]] std::vector<std::string> split_lines(const std::string& content) {
  std::vector<std::string> lines;
  std::stringstream ss(content);
  std::string line;
  while(std::getline(ss, line)) {
    if(line.size() > kReadMaxLineLength) {
      line.resize(kReadMaxLineLength);
    }
    lines.push_back(line);
  }
  return lines;
}

}  // namespace

ReadTool::ReadTool(std::filesystem::path workspace_root)
    : workspace_root_(normalize_workspace_root(workspace_root)) {}

std::string ReadTool::name() const {
  return "read";
}

std::string ReadTool::description() const {
  return "Read file content with line numbers";
}

std::string ReadTool::search_hint() const {
  return "read file contents lines offset limit directory";
}

nlohmann::json ReadTool::parameters() const {
  return nlohmann::json{
      {"type", "object"},
      {"required", nlohmann::json::array({"path"})},
      {"properties",
       {
            {"path", {{"type", "string"}, {"description", "File or directory path to read, relative to the workspace root"}}},
            {"offset", {{"type", "integer"}, {"minimum", 1}, {"description", "1-based line offset for file reads"}}},
            {"limit", {{"type", "integer"}, {"minimum", 1}, {"description", "Maximum number of lines to return"}}},
       }},
  };
}

ava::types::ToolResult ReadTool::execute(const nlohmann::json& args) const {
  if(!args.contains("path")) {
    throw std::runtime_error("missing required field: path");
  }

  const auto path = args.at("path").get<std::string>();
  const auto offset = args.value("offset", static_cast<std::size_t>(1));
  const auto limit = args.value("limit", kReadDefaultLimit);
  const auto full_path = enforce_workspace_path(workspace_root_, path, name());
  reject_backup_history_access(workspace_root_, full_path, name());

  std::error_code ec;
  if(!std::filesystem::exists(full_path, ec) || ec) {
    throw std::runtime_error("Not found: " + path);
  }

  if(std::filesystem::is_directory(full_path, ec) && !ec) {
    std::vector<std::string> entries;
    entries.reserve(64);
    for(const auto& entry : std::filesystem::directory_iterator(full_path)) {
      if(is_backup_history_path(workspace_root_, entry.path())) {
        continue;
      }
      auto display = entry.path().filename().string();
      if(entry.is_directory()) {
        display += "/";
      }
      entries.push_back(display);
    }
    std::sort(entries.begin(), entries.end());
    std::ostringstream out;
    for(std::size_t idx = 0; idx < entries.size(); ++idx) {
      if(idx > 0) {
        out << "\n";
      }
      out << entries[idx];
    }
    return ava::types::ToolResult{.call_id = "", .content = apply_output_fallback(name(), out.str(), kReadOutputBytes), .is_error = false};
  }

  ensure_regular_file_size_within_limit(full_path, kReadMaxFileBytes, "read");
  const auto content = read_file_text(full_path);
  const auto lines = split_lines(content);
  const std::size_t start = offset > 0 ? offset - 1 : 0;
  std::ostringstream out;
  std::size_t emitted = 0;
  for(std::size_t index = start; index < lines.size() && emitted < limit; ++index, ++emitted) {
    if(emitted > 0) {
      out << "\n";
    }
    out << std::setw(6) << (index + 1) << '\t' << lines[index];
  }

  return ava::types::ToolResult{.call_id = "", .content = apply_output_fallback(name(), out.str(), kReadOutputBytes), .is_error = false};
}

WriteTool::WriteTool(std::filesystem::path workspace_root, std::shared_ptr<FileBackupSession> backup_session)
    : workspace_root_(normalize_workspace_root(workspace_root)), backup_session_(std::move(backup_session)) {}

std::string WriteTool::name() const {
  return "write";
}

std::string WriteTool::description() const {
  return "Write content to a file";
}

std::string WriteTool::search_hint() const {
  return "write create overwrite file content";
}

nlohmann::json WriteTool::parameters() const {
  return nlohmann::json{{"type", "object"},
                        {"required", nlohmann::json::array({"path", "content"})},
                        {"properties",
                         {{"path", {{"type", "string"}, {"description", "File path to write, relative to the workspace root"}}},
                          {"content", {{"type", "string"}, {"description", "Complete file content to write"}}}}}};
}

ava::types::ToolResult WriteTool::execute(const nlohmann::json& args) const {
  if(!args.contains("path") || !args.contains("content")) {
    throw std::runtime_error("missing required fields: path/content");
  }

  const auto path = args.at("path").get<std::string>();
  const auto content = args.at("content").get<std::string>();
  const auto full_path = enforce_workspace_path(workspace_root_, path, name());
  reject_backup_history_access(workspace_root_, full_path, name());
  if(backup_session_) {
    backup_session_->backup_file_before_edit(full_path);
  }
  write_file_text(full_path, content);
  return ava::types::ToolResult{
      .call_id = "",
      .content = "Wrote " + std::to_string(content.size()) + " bytes to " + path,
      .is_error = false,
  };
}

}  // namespace ava::tools
