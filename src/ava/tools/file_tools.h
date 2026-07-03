#pragma once

#include "ava/agent/mode.h"
#include "ava/agent/question.h"
#include "ava/agent/tool_visibility.h"
#include "ava/permissions/permission.h"
#include "ava/core/result.h"

#include <cstddef>
#include <filesystem>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace ava::lsp {
class DiagnosticsProvider;
}  // namespace ava::lsp

namespace ava::tools {

class MutationQueue;

struct PermissionAuditEvent
{
  std::string permission_request_id = {};
  ava::permissions::Operation operation;
  ava::agent::Mode mode = ava::agent::Mode::Build;
  std::string tool_name;
  ava::permissions::PermissionAction action = ava::permissions::PermissionAction::Deny;
  std::string reason;
  ava::permissions::PermissionRisk risk = ava::permissions::PermissionRisk::Low;
  std::filesystem::path target_path;
  std::string command;
  std::string resolution;
  std::string resolution_source;
  std::string resolution_reason;
  std::string actor = "agent";
  std::string rule_id;
};

using PermissionAuditSink = std::function<ava::core::VoidResult(PermissionAuditEvent const&)>;

struct ToolProgressEvent
{
  std::string text;
  std::string call_id;
  std::string tool_name;
  std::string status = "running";
};

using ToolProgressSink = std::function<ava::core::VoidResult(ToolProgressEvent const&)>;

struct ToolContext
{
  std::filesystem::path workspace_dir;
  std::filesystem::path spill_dir = {};
  ava::agent::Mode mode = ava::agent::Mode::Build;
  ava::permissions::PermissionResolver permission_resolver = nullptr;
  PermissionAuditSink permission_audit_sink = nullptr;
  ToolProgressSink progress_sink = nullptr;
  std::function<bool()> cancel_requested = nullptr;
  ava::agent::QuestionResolver question_resolver = nullptr;
  std::string permission_tool_name = {};
  std::string permission_actor = {};
  std::string current_tool_name = {};
  std::string current_call_id = {};
  std::shared_ptr<std::vector<std::string>> permission_request_ids = nullptr;
  std::shared_ptr<MutationQueue> mutation_queue = nullptr;
  std::shared_ptr<ava::lsp::DiagnosticsProvider> lsp_diagnostics_provider = nullptr;
  std::filesystem::path plugin_global_plugins_dir = {};
  std::filesystem::path plugin_project_plugins_dir = {};
  std::filesystem::path plugin_enablement_file = {};
  bool include_project_plugins = true;
  std::filesystem::path mcp_global_config_file = {};
  std::filesystem::path mcp_project_config_file = {};
  bool include_project_mcp_config = true;
  std::vector<std::filesystem::path> skill_global_dirs = {};
  std::vector<std::filesystem::path> skill_project_dirs = {};
  bool include_project_skills = true;
  std::string session_id = {};
  std::string provider_id = {};
  std::string model_id = {};
  std::filesystem::path current_dir = {};
  ava::agent::ToolVisibilityOptions tool_visibility;
};

struct TextOutput
{
  std::string content;
  bool truncated = false;
  bool byte_limited = false;
  bool line_limited = false;
  std::size_t total_bytes = 0;
  std::size_t output_bytes = 0;
  std::size_t output_lines = 0;
  std::size_t start_line = 1;
  std::size_t end_line = 0;
  std::size_t total_lines = 0;
  std::size_t next_offset_line = 0;
};

struct FileMutationResult
{
  std::filesystem::path path;
  std::size_t bytes_written = 0;
  std::string diff;
  bool diff_truncated = false;
  std::string line_endings;
  bool had_utf8_bom = false;
};

struct ReadOptions
{
  std::size_t max_bytes = 50 * 1024;
  std::size_t offset_line = 1;
  std::size_t max_lines = 200;
  bool permission_already_checked = false;
};

struct WriteOptions
{
  bool permission_already_checked = false;
  bool mutation_already_locked = false;
};

[[nodiscard]] ava::core::Result<TextOutput> read_file(ToolContext const& context, std::filesystem::path const& path, ReadOptions options = {});
[[nodiscard]] ava::core::Result<FileMutationResult> write_file(ToolContext const& context, std::filesystem::path const& path, std::string_view content,
                                                               WriteOptions options = {});
[[nodiscard]] ava::core::Result<FileMutationResult> edit_file(ToolContext const& context, std::filesystem::path const& path, std::string_view old_text,
                                                              std::string_view new_text);
[[nodiscard]] ava::core::VoidResult ensure_permission(ToolContext const& context, ava::permissions::Operation operation,
                                                      std::filesystem::path const& target_path, std::string_view command, std::string_view tool_name,
                                                      std::string_view error_message, std::string_view diff_preview = {}, bool diff_truncated = false);
[[nodiscard]] std::string permission_audit_data_json(PermissionAuditEvent const& event);
[[nodiscard]] ava::core::VoidResult replace_file_with_staged_file(std::filesystem::path const& staged_path, std::filesystem::path const& target_path);
void remove_staged_file_best_effort(std::filesystem::path const& staged_path);

}  // namespace ava::tools
