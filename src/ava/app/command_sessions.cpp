#include "sys.h"
#include "ava/app/command_format.h"
#include "ava/app/command_sessions.h"
#include "ava/app/command_tools.h"
#include "ava/app/runtime/Session.h"
#include "ava/session/compaction.h"
#include "ava/session/export.h"
#include "ava/session/record.h"
#include "ava/session/session_branch.h"
#include "ava/session/session_metadata.h"
#include "ava/session/session_tree.h"
#include "ava/session/stats.h"
#include "ava/session/validation.h"
#include "ava/context/context_loader.h"
#include "ava/lsp/configured_provider.h"
#include "ava/core/fingerprint.h"
#include "ava/core/ids.h"
#include "ava/core/json.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <memory>
#include <mutex>
#include <optional>
#include <sstream>
#include <unordered_map>
#include <utility>
#include <vector>

namespace ava::app {
namespace {

std::string format_cost_usd(long double value)
{
  std::ostringstream output;
  output << '$' << std::fixed << std::setprecision(6) << value;
  return output.str();
}

std::string trim_ascii(std::string_view text)
{
  while (!text.empty() && std::isspace(static_cast<unsigned char>(text.front())) != 0) text.remove_prefix(1);
  while (!text.empty() && std::isspace(static_cast<unsigned char>(text.back())) != 0) text.remove_suffix(1);
  return std::string(text);
}

std::string lower_ascii(std::string_view text)
{
  std::string lowered(text);
  std::ranges::transform(lowered, lowered.begin(), [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
  return lowered;
}

bool contains_ascii_case_insensitive(std::string_view text, std::string_view query)
{
  if (query.empty())
    return true;
  return lower_ascii(text).find(lower_ascii(query)) != std::string::npos;
}

enum class ExportFormat
{
  Markdown,
  Html,
  Jsonl,
};

struct ExportCommandArguments
{
  ExportFormat format = ExportFormat::Markdown;
  std::string path;
};

struct JsonlAttachmentSummary
{
  std::size_t count = 0;
  std::string first_entry_id;
  std::string first_storage_path;
};

ava::core::Result<std::vector<ava::session::SessionEntry>> load_runtime_entries(runtime::Session const& session)
{
  auto read_authority = session.read_authority();
  if (!read_authority)
    return std::unexpected(std::move(read_authority.error()));
  return read_authority->load();
}

ava::core::Result<ava::session::SessionMetadataView> load_runtime_metadata(runtime::Session const& session)
{
  auto entries = load_runtime_entries(session);
  if (!entries)
    return std::unexpected(std::move(entries.error()));
  return ava::session::session_metadata_from_entries(*entries);
}

std::string export_format_text(ExportFormat format)
{
  switch (format)
  {
    case ExportFormat::Markdown:
      return "markdown";
    case ExportFormat::Html:
      return "html";
    case ExportFormat::Jsonl:
      return "jsonl";
  }
  return "markdown";
}

std::string_view trim_ascii_view(std::string_view text)
{
  while (!text.empty() && std::isspace(static_cast<unsigned char>(text.front())) != 0) text.remove_prefix(1);
  while (!text.empty() && std::isspace(static_cast<unsigned char>(text.back())) != 0) text.remove_suffix(1);
  return text;
}

std::string_view first_token(std::string_view text)
{
  text = trim_ascii_view(text);
  auto const end = text.find_first_of(" \t\r\n");
  return text.substr(0, end == std::string_view::npos ? text.size() : end);
}

std::string_view after_first_token(std::string_view text)
{
  text = trim_ascii_view(text);
  auto const end = text.find_first_of(" \t\r\n");
  if (end == std::string_view::npos)
    return {};
  return trim_ascii_view(text.substr(end + 1));
}

bool path_looks_html(std::string_view path)
{
  auto const lowered = lower_ascii(path);
  return lowered.ends_with(".html") || lowered.ends_with(".htm");
}

bool path_looks_jsonl(std::string_view path)
{
  auto const lowered = lower_ascii(path);
  return lowered.ends_with(".jsonl");
}

ava::core::Result<std::string> format_session_jsonl(std::vector<ava::session::SessionEntry> const& entries)
{
  std::string out;
  for (auto const& entry : entries)
  {
    auto line = ava::session::serialize_session_entry_line(entry);
    if (!line)
      return std::unexpected(std::move(line.error()));
    out += *line;
    out += '\n';
  }
  return out;
}

bool json_bool_field_is_true(std::string_view object, std::string_view key)
{
  auto const start = ava::core::json::field_value_start(object, key);
  return start && object.substr(*start, 4) == "true";
}

JsonlAttachmentSummary summarize_non_redacted_jsonl_attachments(std::vector<ava::session::SessionEntry> const& entries)
{
  JsonlAttachmentSummary summary;
  for (auto const& entry : entries)
  {
    if (entry.type != ava::session::EntryType::UserMessage)
      continue;
    auto const sanitized = ava::session::sanitized_message_data_json(entry.data_json);
    for (auto const& attachment : ava::core::json::objects_in_array_field(sanitized, "attachments"))
    {
      if (ava::core::json::string_field(attachment, "type").value_or("") != "image")
        continue;
      if (json_bool_field_is_true(attachment, "redacted"))
        continue;
      auto const storage_path = ava::core::json::string_field(attachment, "storage_path").value_or("");
      if (storage_path.empty())
        continue;
      ++summary.count;
      if (summary.first_entry_id.empty())
      {
        summary.first_entry_id = entry.id;
        summary.first_storage_path = storage_path;
      }
    }
  }
  return summary;
}

std::string raw_jsonl_attachment_note(JsonlAttachmentSummary const& summary)
{
  if (summary.count == 0)
    return {};
  return "raw JSONL exports include " + std::to_string(summary.count) +
         " image attachment metadata record(s), but not the attachment files; raw JSONL /import rejects non-redacted attachments until an attachment-aware "
         "archive format exists";
}

ava::core::VoidResult validate_raw_jsonl_import_attachments(std::vector<ava::session::SessionEntry> const& entries, std::filesystem::path const& path)
{
  auto const summary = summarize_non_redacted_jsonl_attachments(entries);
  if (summary.count == 0)
    return {};

  auto error =
      ava::core::Error(ava::core::ErrorCategory::InvalidArgument, "session import has image attachments but raw JSONL does not include attachment files");
  error.with_context("path", path.string());
  error.with_context("attachments", std::to_string(summary.count));
  error.with_context("first_entry_id", summary.first_entry_id);
  error.with_context("first_storage_path", summary.first_storage_path);
  error.with_context("remediation", "remove or redact attachment metadata, or use a future attachment-aware archive format");
  return std::unexpected(std::move(error));
}

bool looks_like_pi_session_header(std::string_view line)
{
  if (!ava::core::json::is_valid_object(line))
    return false;
  auto const type = ava::core::json::string_field(line, "type");
  return type && *type == "session" && ava::core::json::string_field(line, "id") && ava::core::json::string_field(line, "timestamp") &&
         ava::core::json::string_field(line, "cwd");
}

ava::core::Error unsupported_pi_session_import_error(std::filesystem::path const& path)
{
  auto error = ava::core::Error(ava::core::ErrorCategory::Session, "Pi session import is not supported yet");
  error.with_context("path", path.string());
  return error;
}

ava::core::Result<ExportCommandArguments> parse_export_command_arguments(std::string_view argument)
{
  argument = trim_ascii_view(argument);
  ExportCommandArguments parsed;
  if (argument.empty())
    return parsed;

  auto const token = lower_ascii(first_token(argument));
  if (token == "markdown" || token == "md")
  {
    parsed.format = ExportFormat::Markdown;
    parsed.path = std::string(after_first_token(argument));
    return parsed;
  }
  if (token == "html")
  {
    parsed.format = ExportFormat::Html;
    parsed.path = std::string(after_first_token(argument));
    return parsed;
  }
  if (token == "jsonl" || token == "raw")
  {
    parsed.format = ExportFormat::Jsonl;
    parsed.path = std::string(after_first_token(argument));
    return parsed;
  }
  if (token == "--format")
  {
    auto const format_token = lower_ascii(first_token(after_first_token(argument)));
    if (format_token == "markdown" || format_token == "md")
    {
      parsed.format = ExportFormat::Markdown;
    }
    else if (format_token == "html")
    {
      parsed.format = ExportFormat::Html;
    }
    else if (format_token == "jsonl" || format_token == "raw")
    {
      parsed.format = ExportFormat::Jsonl;
    }
    else
    {
      auto error = ava::core::Error(ava::core::ErrorCategory::InvalidArgument, "unsupported export format");
      error.with_context("format", std::string(format_token));
      error.with_context("supported", "markdown, html, jsonl");
      return std::unexpected(std::move(error));
    }
    parsed.path = std::string(after_first_token(after_first_token(argument)));
    return parsed;
  }

  parsed.path = std::string(argument);
  parsed.format = path_looks_html(argument) ? ExportFormat::Html : (path_looks_jsonl(argument) ? ExportFormat::Jsonl : ExportFormat::Markdown);
  return parsed;
}

std::filesystem::path resolve_export_path(runtime::Session const& session, std::string_view path)
{
  auto resolved = std::filesystem::path(std::string(path));
  if (resolved.is_relative())
    resolved = session.current_dir / resolved;
  return resolved.lexically_normal();
}

std::string labels_text(std::vector<std::string> const& labels)
{
  std::string text;
  for (std::size_t index = 0; index < labels.size(); ++index)
  {
    if (index > 0)
      text += ",";
    text += labels[index];
  }
  return text;
}

bool metadata_labels_match(std::vector<std::string> const& labels, std::string_view query)
{
  return std::ranges::any_of(labels, [&](std::string const& label) { return contains_ascii_case_insensitive(label, query); });
}

bool session_matches_query(ava::session::SessionTreeNode const& node, std::string_view query)
{
  return contains_ascii_case_insensitive(node.summary.session_id, query) || contains_ascii_case_insensitive(node.summary.last_updated, query) ||
         contains_ascii_case_insensitive(node.summary.path.generic_string(), query) || contains_ascii_case_insensitive(node.metadata.name, query) ||
         contains_ascii_case_insensitive(node.metadata.parent_session_id, query) || contains_ascii_case_insensitive(node.metadata.source_session_id, query) ||
         contains_ascii_case_insensitive(node.metadata.branch_from_entry_id, query) || contains_ascii_case_insensitive(node.metadata.branch_origin, query) ||
         (node.metadata.archived && contains_ascii_case_insensitive("archived", query)) || contains_ascii_case_insensitive(node.metadata.actor, query) ||
         metadata_labels_match(node.metadata.labels, query);
}

bool context_source_matches_query(runtime::ContextSourceMetadata const& source, std::string_view query)
{
  return contains_ascii_case_insensitive(source.path.generic_string(), query) ||
         contains_ascii_case_insensitive(ava::context::to_string(source.source_type), query);
}

std::string context_file_status(std::filesystem::path const& path, std::size_t loaded_bytes, std::uint64_t loaded_fingerprint)
{
  std::error_code status_error;
  auto const status = std::filesystem::symlink_status(path, status_error);
  if (status_error == std::errc::no_such_file_or_directory || status_error == std::errc::not_a_directory)
    return "status=missing";
  if (status_error)
    return "status=unreadable cause=" + sanitize_inline_text(status_error.message());
  if (!std::filesystem::exists(status))
    return "status=missing";
  if (std::filesystem::is_symlink(status))
    return "status=changed cause=symlink";
  if (!std::filesystem::is_regular_file(status))
    return "status=changed cause=not_regular";

  std::error_code size_error;
  auto const current_bytes = std::filesystem::file_size(path, size_error);
  if (size_error)
    return "status=unreadable cause=" + sanitize_inline_text(size_error.message());
  if (current_bytes != loaded_bytes)
    return "status=changed current_bytes=" + std::to_string(current_bytes);

  std::ifstream file(path, std::ios::binary);
  if (!file)
    return "status=unreadable cause=open_failed";

  std::string content;
  content.reserve(loaded_bytes);
  std::array<char, 4096> buffer{};
  while (file)
  {
    file.read(buffer.data(), static_cast<std::streamsize>(buffer.size()));
    if (file.gcount() > 0)
      content.append(buffer.data(), static_cast<std::size_t>(file.gcount()));
  }
  if (!file.eof() && file.fail())
    return "status=unreadable cause=read_failed";
  if (ava::core::content_fingerprint(content) == loaded_fingerprint)
    return "status=current current_bytes=" + std::to_string(current_bytes);
  return "status=changed current_bytes=" + std::to_string(current_bytes);
}

bool prompt_matches_query(runtime::Session const& session, std::string_view query)
{
  if (query.empty())
    return true;
  if (contains_ascii_case_insensitive("prompt", query) || contains_ascii_case_insensitive("base_prompt", query) ||
      contains_ascii_case_insensitive("builtin", query) || contains_ascii_case_insensitive("override", query))
    return true;
  return session.base_prompt.source_path && contains_ascii_case_insensitive(session.base_prompt.source_path->generic_string(), query);
}

std::string_view freshness_source_kind_text(runtime::FreshnessSourceKind kind)
{
  switch (kind)
  {
    case runtime::FreshnessSourceKind::SystemPrompt:
      return "system_prompt";
    case runtime::FreshnessSourceKind::AppendSystemPrompt:
      return "append_system_prompt";
    case runtime::FreshnessSourceKind::PromptCommand:
      return "prompt_command";
    case runtime::FreshnessSourceKind::Skill:
      return "skill";
    case runtime::FreshnessSourceKind::PluginManifest:
      return "plugin_manifest";
    case runtime::FreshnessSourceKind::PluginPrompt:
      return "plugin_prompt";
    case runtime::FreshnessSourceKind::PluginSkill:
      return "plugin_skill";
  }
  return "unknown";
}

bool freshness_source_matches_query(runtime::FreshnessSourceMetadata const& source, std::string_view query)
{
  if (query.empty())
    return true;
  return contains_ascii_case_insensitive(freshness_source_kind_text(source.kind), query) || contains_ascii_case_insensitive(source.scope, query) ||
         contains_ascii_case_insensitive(source.source_id, query) || contains_ascii_case_insensitive(source.name, query) ||
         contains_ascii_case_insensitive(source.path.generic_string(), query);
}

std::size_t freshness_source_count(std::vector<runtime::FreshnessSourceMetadata> const& sources, runtime::FreshnessSourceKind kind)
{
  return static_cast<std::size_t>(std::ranges::count_if(sources, [&](auto const& source) { return source.kind == kind; }));
}

std::string freshness_source_status_text(runtime::FreshnessSourceMetadata const& source)
{
  if (source.path.empty())
    return "<inline>  loaded_bytes=" + std::to_string(source.byte_count) + "  status=inline";
  return source.path.string() + "  loaded_bytes=" + std::to_string(source.byte_count) + "  " +
         context_file_status(source.path, source.byte_count, source.content_fingerprint);
}

std::string lsp_error_text(ava::core::Error const& error)
{
  std::string text = ava::core::to_string(error.category()) + ":" + error.message();
  for (auto const& item : error.context())
  {
    text += " ";
    text += item.key;
    text += "=";
    text += item.value;
  }
  return sanitize_inline_text(std::move(text));
}

bool lsp_config_matches_query(ava::lsp::ConfiguredLspConfigDiagnostic const& diagnostic, std::string_view query)
{
  if (query.empty())
    return true;
  if (contains_ascii_case_insensitive("lsp", query) || contains_ascii_case_insensitive("lsp_config", query))
    return true;
  if (contains_ascii_case_insensitive(diagnostic.scope, query) || contains_ascii_case_insensitive(diagnostic.path.generic_string(), query))
    return true;
  if (diagnostic.error && contains_ascii_case_insensitive(diagnostic.error->message(), query))
    return true;
  if (diagnostic.error)
  {
    return std::ranges::any_of(diagnostic.error->context(), [&](auto const& item) {
      return contains_ascii_case_insensitive(item.key, query) || contains_ascii_case_insensitive(item.value, query);
    });
  }
  return false;
}

std::string lsp_config_status_text(ava::lsp::ConfiguredLspConfigDiagnostic const& diagnostic)
{
  std::string output = diagnostic.path.string();
  if (diagnostic.error)
  {
    output += "  status=error  error=" + lsp_error_text(*diagnostic.error);
    return output;
  }
  if (!diagnostic.exists)
  {
    output += "  status=missing";
    return output;
  }
  output += "  loaded_bytes=" + std::to_string(diagnostic.byte_count) + "  status=";
  output += diagnostic.loaded ? "loaded" : "skipped";
  output += " servers=" + std::to_string(diagnostic.server_count);
  return output;
}

std::size_t configured_lsp_config_count(std::vector<ava::lsp::ConfiguredLspConfigDiagnostic> const& diagnostics)
{
  return static_cast<std::size_t>(std::ranges::count_if(diagnostics, [](auto const& diagnostic) { return diagnostic.exists || diagnostic.error.has_value(); }));
}

std::string lsp_provider_status_text(ava::lsp::ConfiguredLspProviderInspection const& inspection)
{
  if (inspection.error_count > 0)
    return "error";
  if (inspection.server_count > 0)
    return "configured";
  return "unavailable";
}

bool path_exists_for_status(std::filesystem::path const& path)
{
  std::error_code status_error;
  auto const status = std::filesystem::symlink_status(path, status_error);
  return !status_error && std::filesystem::exists(status);
}

runtime::Event base_command_event(runtime::Session const& session, runtime::EventType type)
{
  runtime::Event event;
  event.type = type;
  event.timestamp = ava::session::now_timestamp();
  event.session_id = session.store.session_id();
  event.mode = session.mode;
  event.provider_id = session.model.provider_id;
  event.model_id = session.model.model_id;
  return event;
}

ava::core::VoidResult emit_command_event(CommandRequest const& request, runtime::Event event)
{
  if (!request.event_sink)
    return {};
  return emit_event(request.event_sink, event);
}

bool command_canceled(CommandRequest const& request)
{
  return request.cancel_requested && request.cancel_requested();
}

ava::core::Error command_canceled_error()
{
  return ava::core::Error(ava::core::ErrorCategory::Unknown, "agent loop canceled");
}

template <typename Value>
void append_known_value(std::ostringstream& output, bool& wrote_any, std::string_view label, std::optional<Value> const& value)
{
  if (!value)
    return;
  if (wrote_any)
    output << ' ';
  output << label << '=' << *value;
  wrote_any = true;
}

std::string shorten_middle(std::string text, std::size_t max_columns)
{
  if (text.size() <= max_columns || max_columns < 8)
    return text;
  auto const front = (max_columns - 3) / 2;
  auto const back = max_columns - 3 - front;
  return text.substr(0, front) + "..." + text.substr(text.size() - back);
}

std::string compact_workspace_label(std::filesystem::path const& workspace)
{
  auto const filename = workspace.filename().generic_string();
  if (!filename.empty())
    return shorten_middle(filename, 32);
  return shorten_middle(workspace.generic_string(), 48);
}

std::string compact_cwd_label(std::filesystem::path const& cwd, std::filesystem::path const& workspace)
{
  auto text = display_path(cwd, workspace);
  if (text.empty())
    text = ".";
  return shorten_middle(std::move(text), 48);
}

std::unordered_map<std::string, std::size_t> tree_index_by_id(std::vector<ava::session::SessionTreeNode> const& nodes)
{
  std::unordered_map<std::string, std::size_t> index;
  for (std::size_t node_index = 0; node_index < nodes.size(); ++node_index)
  {
    index.emplace(nodes[node_index].summary.session_id, node_index);
  }
  return index;
}

std::string format_session_tree_line(ava::session::SessionTreeNode const& node, std::size_t depth, bool current_path)
{
  std::string output(depth * 2, ' ');
  output += node.current ? "* " : "- ";
  if (!node.metadata.name.empty())
  {
    output += sanitize_inline_text(node.metadata.name);
    output += "  id=" + sanitize_inline_text(node.summary.session_id);
  }
  else
  {
    output += sanitize_inline_text(node.summary.session_id);
  }
  output += "  entries=" + std::to_string(node.summary.entry_count);
  if (!node.summary.last_updated.empty())
    output += "  updated=" + sanitize_inline_text(node.summary.last_updated);
  if (!node.metadata.branch_origin.empty())
    output += "  origin=" + sanitize_inline_text(node.metadata.branch_origin);
  if (node.metadata.archived)
    output += "  archived";
  if (!node.metadata.parent_session_id.empty())
    output += "  parent=" + sanitize_inline_text(shorten_middle(node.metadata.parent_session_id, 24));
  if (!node.metadata.branch_from_entry_id.empty())
    output += "  from=" + sanitize_inline_text(shorten_middle(node.metadata.branch_from_entry_id, 24));
  if (!node.metadata.labels.empty())
    output += "  labels=" + sanitize_inline_text(labels_text(node.metadata.labels));
  if (current_path && !node.current)
    output += "  current_path";
  return output;
}

ava::core::Result<runtime::Session> reopen_session(runtime::Session const& current, std::string_view session_id)
{
  runtime::OpenOptions options;
  options.workspace_dir = current.workspace_dir;
  options.current_dir = current.current_dir;
  options.requested_session_id = std::string(session_id);
  options.continue_last_session = false;
  options.sessionless = current.sessionless;
  options.mode = current.mode;
  options.tool_visibility = current.tool_visibility;
  options.paths = current.paths;
  return open_runtime_session(options);
}

ava::core::Result<runtime::Session> create_fresh_session(runtime::Session const& current)
{
  runtime::OpenOptions options;
  options.workspace_dir = current.workspace_dir;
  options.current_dir = current.current_dir;
  options.continue_last_session = false;
  options.sessionless = current.sessionless;
  options.mode = current.mode;
  options.tool_visibility = current.tool_visibility;
  options.paths = current.paths;
  return open_runtime_session(options);
}

runtime::OpenOptions owned_replacement_options(runtime::Session const& current)
{
  runtime::OpenOptions options;
  options.workspace_dir = current.workspace_dir;
  options.current_dir = current.current_dir;
  options.mode = current.mode;
  options.tool_visibility = current.tool_visibility;
  options.paths = current.paths;
  options.prompt_overrides = current.prompt_overrides;
  options.offline = current.offline;
  return options;
}

void attach_created_session_cleanup_context(ava::session::SessionStore const& store, ava::session::SessionLease const& lease, ava::core::Error& error)
{
  auto removed = store.remove_created_file(lease);
  if (!removed)
  {
    error.with_context("rollback_path", store.session_path().string());
    error.with_context("rollback_cause", removed.error().format());
  }
}

ava::core::Result<CommandResult> run_branch_command(runtime::Session& session, std::string_view name, ava::session::SessionBranchMode mode)
{
  CommandResult result;
  result.handled = true;

  auto const source_session_id = session.store.session_id();
  if (session.sessionless)
  {
    add_output(result, "Cannot branch a sessionless session.");
    return result;
  }
  auto const trimmed_name = trim_ascii(name);
  auto recovered = session.store.recover_torn_tail(session.lease, ava::session::legacy_unbounded_session_read_limits());
  if (!recovered)
    return std::unexpected(std::move(recovered.error()));
  auto branched = ava::session::create_session_branch(
      ava::session::SessionBranchOptions{.workspace_dir = session.workspace_dir,
                                         .root_dir = session.paths.sessions_dir,
                                         .source_session_id = source_session_id,
                                         .branch_from_entry_id = {},
                                         .name = trimmed_name.empty() ? std::nullopt : std::optional<std::string>(trimmed_name),
                                         .labels = std::nullopt,
                                         .source_lease = &session.lease,
                                         .mode = mode,
                                         .actor = "tui"});
  if (!branched)
    return std::unexpected(std::move(branched.error()));

  auto const created_session_id = branched->store.session_id();
  auto const branch_from_entry_id = branched->branch_from_entry_id;
  auto owned_options = owned_replacement_options(session);
  auto opened = open_owned_runtime_session(owned_options, branched->store, branched->lease, true);
  if (!opened)
  {
    auto error = std::move(opened.error());
    ava::session::rollback_created_session_with_context(branched->store, branched->lease, error);
    return std::unexpected(std::move(error));
  }
  opened->created = true;
  if (auto replaced = replace_runtime_session(session, std::move(*opened)); !replaced)
    return std::unexpected(std::move(replaced.error()));

  auto const mode_text = mode == ava::session::SessionBranchMode::Clone ? std::string("cloned") : std::string("forked");
  std::string output = mode_text + " session " + created_session_id + " from " + source_session_id;
  if (!branch_from_entry_id.empty())
    output += " at " + branch_from_entry_id;
  if (!trimmed_name.empty())
    output += " name=\"" + sanitize_inline_text(trimmed_name) + "\"";
  output += "\nswitched to " + session.store.session_id();
  add_output(result, std::move(output));
  return result;
}

ava::core::Result<CommandResult> run_sessions_rename_command(runtime::Session& session, std::string_view arguments)
{
  CommandResult result;
  result.handled = true;

  auto rest = std::string_view(arguments);
  auto const subcommand = std::string_view("rename");
  if (rest.starts_with(subcommand))
    rest.remove_prefix(subcommand.size());
  while (!rest.empty() && std::isspace(static_cast<unsigned char>(rest.front())) != 0) rest.remove_prefix(1);
  auto const id_end = rest.find_first_of(" \t\r\n");
  auto const requested_session_id = id_end == std::string_view::npos ? rest : rest.substr(0, id_end);
  if (requested_session_id.empty())
  {
    add_output(result, missing_argument("/sessions rename <id> <name|--clear>"));
    return result;
  }
  rest = id_end == std::string_view::npos ? std::string_view{} : rest.substr(id_end);
  auto const trimmed_name = trim_ascii(rest);
  if (trimmed_name.empty())
  {
    add_output(result, missing_argument("/sessions rename <id> <name|--clear>"));
    return result;
  }

  auto const clear_name = trimmed_name == "--clear";
  ava::session::SessionMetadataUpdate update;
  update.name = clear_name ? std::optional<std::string>(std::string{}) : std::optional<std::string>(trimmed_name);
  update.actor = "tui";

  std::string target_id;
  ava::core::Result<ava::session::SessionMetadataView> metadata =
      std::unexpected(ava::core::Error(ava::core::ErrorCategory::Unknown, "session rename target was not resolved"));
  if (requested_session_id == session.store.session_id())
  {
    target_id = session.store.session_id();
    metadata = append_runtime_session_metadata(session, std::move(update));
  }
  else
  {
    auto target = reopen_session(session, requested_session_id);
    if (!target)
      return std::unexpected(std::move(target.error()));
    target_id = target->store.session_id();
    metadata = append_runtime_session_metadata(*target, std::move(update));
  }
  if (!metadata)
    return std::unexpected(std::move(metadata.error()));

  if (clear_name)
    add_output(result, "session " + target_id + " name cleared");
  else
    add_output(result, "session " + target_id + " name set: \"" + sanitize_inline_text(metadata->name) + "\"");
  return result;
}

ava::core::Result<CommandResult> run_sessions_labels_command(runtime::Session& session, std::string_view arguments)
{
  CommandResult result;
  result.handled = true;

  auto rest = std::string_view(arguments);
  if (rest.starts_with("labels"))
    rest.remove_prefix(std::string_view("labels").size());
  else if (rest.starts_with("label"))
    rest.remove_prefix(std::string_view("label").size());

  auto const parts = split_command_arguments(rest);
  if (parts.empty())
  {
    add_output(result, missing_argument("/sessions labels <id> <label...|--clear>"));
    return result;
  }

  auto const requested_session_id = parts.front();
  auto target = reopen_session(session, requested_session_id);
  if (!target)
    return std::unexpected(std::move(target.error()));

  if (parts.size() == 1)
  {
    auto metadata = load_runtime_metadata(*target);
    if (!metadata)
      return std::unexpected(std::move(metadata.error()));
    auto text = metadata->labels.empty() ? std::string("session " + target->store.session_id() + " labels: <none>")
                                         : std::string("session " + target->store.session_id() + " labels: " + labels_text(metadata->labels));
    text += "\nusage: /sessions labels <id> <label...|--clear>";
    add_output(result, std::move(text));
    return result;
  }

  std::vector<std::string> label_parts(parts.begin() + 1, parts.end());
  if (std::ranges::find(label_parts, "--clear") != label_parts.end() && label_parts.size() != 1)
  {
    add_output(result, missing_argument("/sessions labels <id> <label...|--clear>"));
    return result;
  }

  auto next_labels = label_parts.size() == 1 && label_parts[0] == "--clear" ? std::vector<std::string>{} : label_parts;
  ava::session::SessionMetadataUpdate update;
  update.labels = next_labels;
  update.actor = "tui";
  auto metadata = append_runtime_session_metadata(*target, std::move(update));
  if (!metadata)
    return std::unexpected(std::move(metadata.error()));

  if (metadata->labels.empty())
    add_output(result, "session " + target->store.session_id() + " labels cleared");
  else
    add_output(result, "session " + target->store.session_id() + " labels set: " + sanitize_inline_text(labels_text(metadata->labels)));
  return result;
}

ava::core::Result<CommandResult> run_sessions_archive_command(runtime::Session& session, std::string_view arguments, bool archived)
{
  CommandResult result;
  result.handled = true;

  auto rest = std::string_view(arguments);
  auto const subcommand = archived ? std::string_view("archive") : std::string_view("unarchive");
  if (rest.starts_with(subcommand))
    rest.remove_prefix(subcommand.size());
  auto const parts = split_command_arguments(rest);
  if (parts.empty())
  {
    add_output(result, missing_argument(archived ? "/sessions archive <id> --confirm" : "/sessions unarchive <id>"));
    return result;
  }
  auto const requested_session_id = parts.front();
  if (requested_session_id == session.store.session_id())
  {
    add_output(result, "Cannot archive the active session. Switch sessions first.");
    return result;
  }
  if (archived && std::ranges::find(parts, "--confirm") == parts.end())
  {
    add_output(result,
               "Archive hides a session from the default selector and /sessions view but keeps its JSONL file intact.\n"
               "Run /sessions archive " +
                   sanitize_inline_text(requested_session_id) + " --confirm to archive it.");
    return result;
  }

  auto target = reopen_session(session, requested_session_id);
  if (!target)
    return std::unexpected(std::move(target.error()));

  auto current_metadata = load_runtime_metadata(*target);
  if (!current_metadata)
    return std::unexpected(std::move(current_metadata.error()));
  if (current_metadata->archived == archived)
  {
    add_output(result, "session " + target->store.session_id() + (archived ? " already archived" : " is not archived"));
    return result;
  }

  ava::session::SessionMetadataUpdate update;
  update.archived = archived;
  update.actor = "tui";
  auto metadata = append_runtime_session_metadata(*target, std::move(update));
  if (!metadata)
    return std::unexpected(std::move(metadata.error()));

  add_output(result, "session " + target->store.session_id() + (metadata->archived ? " archived" : " unarchived"));
  return result;
}

void append_session_tree_lines(std::string& output, std::vector<ava::session::SessionTreeNode> const& nodes,
                               std::unordered_map<std::string, std::size_t> const& index_by_id, std::vector<std::string> const& ids,
                               std::vector<std::string> const& current_path, std::string_view query, std::size_t depth, bool include_archived, bool& wrote_any)
{
  for (auto const& id : ids)
  {
    auto const found = index_by_id.find(id);
    if (found == index_by_id.end())
      continue;
    auto const& node = nodes[found->second];
    auto const query_match = session_matches_query(node, query);
    auto const current_path_node = std::ranges::find(current_path, node.summary.session_id) != current_path.end();
    auto const visible = include_archived || !node.metadata.archived;
    if (visible && (query.empty() || query_match))
    {
      output += format_session_tree_line(node, depth, current_path_node);
      output += '\n';
      wrote_any = true;
    }
    append_session_tree_lines(output, nodes, index_by_id, node.children, current_path, query, depth + (visible ? 1 : 0), include_archived, wrote_any);
  }
}

std::string known_values_text(ava::session::SessionStats const& stats)
{
  std::ostringstream output;
  bool wrote_any = false;
  append_known_value(output, wrote_any, "input", stats.input_tokens);
  append_known_value(output, wrote_any, "output", stats.output_tokens);
  append_known_value(output, wrote_any, "reasoning", stats.reasoning_tokens);
  append_known_value(output, wrote_any, "cache_read", stats.cache_read_tokens);
  append_known_value(output, wrote_any, "cache_write", stats.cache_write_tokens);
  append_known_value(output, wrote_any, "total", stats.total_tokens);
  return wrote_any ? output.str() : std::string("unavailable");
}

std::string estimated_bytes_text(ava::session::SessionStats const& stats)
{
  std::ostringstream output;
  bool wrote_any = false;
  append_known_value(output, wrote_any, "input", stats.estimated_input_bytes);
  append_known_value(output, wrote_any, "output", stats.estimated_output_bytes);
  append_known_value(output, wrote_any, "total", stats.estimated_total_bytes);
  return wrote_any ? output.str() : std::string("unavailable");
}

std::string cost_text(ava::session::SessionStats const& stats)
{
  if (stats.cost_complete)
    return stats.total_cost_usd ? format_cost_usd(*stats.total_cost_usd) : "unavailable";
  if (stats.known_cost_usd)
  {
    return "at least " + format_cost_usd(*stats.known_cost_usd) + " (" + std::to_string(stats.unknown_cost_entries) + " unknown)";
  }
  return "incomplete (" + std::to_string(stats.unknown_cost_entries) + " unknown)";
}

std::string format_session_stats_text(runtime::Session const& session, ava::session::SessionStats const& stats)
{
  std::ostringstream output;
  output << "Session stats\n";
  output << "  session: " << shorten_middle(session.store.session_id(), 32) << "   entries: " << stats.entry_count << '\n';
  output << "  model: " << session.model.provider_id << '/' << session.model.model_id << "   mode: " << ava::agent::to_string(session.mode) << '\n';
  output << "  workspace: " << compact_workspace_label(session.workspace_dir) << "   cwd: " << compact_cwd_label(session.current_dir, session.workspace_dir)
         << '\n';
  if (!stats.first_timestamp.empty() || !stats.last_timestamp.empty())
  {
    output << "  time: " << (stats.first_timestamp.empty() ? "unknown" : stats.first_timestamp) << " -> "
           << (stats.last_timestamp.empty() ? "unknown" : stats.last_timestamp) << '\n';
  }

  output << "\nMessages:\n";
  output << "  user " << stats.counts.user_message << "   assistant " << stats.counts.assistant_message << "   tools " << stats.counts.tool_call << '/'
         << stats.counts.tool_result << "   permissions " << stats.counts.permission_decision << '\n';
  output << "  compactions " << stats.counts.compaction << "   mode/model " << stats.counts.mode_change << '/' << stats.counts.model_change
         << "   errors/cancels " << stats.counts.error << '/' << stats.counts.cancel << '\n';

  output << "\nUsage:\n";
  output << "  tokens: " << known_values_text(stats) << '\n';
  output << "  est bytes: " << estimated_bytes_text(stats) << '\n';
  output << "  cost: " << cost_text(stats) << "   usage entries exact/estimated " << stats.exact_usage_entries << '/' << stats.estimated_usage_entries << '\n';

  output << "\nHints:\n";
  output << "  export: /export   resume: ava --session " << session.store.session_id();
  return output.str();
}

}  // namespace

ava::core::Result<CommandResult> run_sessions_command(runtime::Session& session, std::string_view query)
{
  CommandResult result;
  result.handled = true;
  auto const trimmed_query = trim_ascii(query);
  auto const query_parts = split_command_arguments(trimmed_query);
  if (!query_parts.empty() && query_parts.front() == "rename")
  {
    return run_sessions_rename_command(session, trimmed_query);
  }
  if (!query_parts.empty() && (query_parts.front() == "labels" || query_parts.front() == "label"))
  {
    return run_sessions_labels_command(session, trimmed_query);
  }
  if (!query_parts.empty() && query_parts.front() == "archive")
  {
    return run_sessions_archive_command(session, trimmed_query, true);
  }
  if (!query_parts.empty() && query_parts.front() == "unarchive")
  {
    return run_sessions_archive_command(session, trimmed_query, false);
  }
  auto list_query = trimmed_query;
  bool include_archived = false;
  if (!query_parts.empty() && query_parts.front() == "--archived")
  {
    include_archived = true;
    list_query = trimmed_query.substr(std::string_view("--archived").size());
    list_query = trim_ascii(list_query);
  }
  auto tree = ava::session::build_session_tree(session.workspace_dir, session.paths.sessions_dir, session.store.session_id());
  if (!tree)
  {
    add_output(result, tree.error().format());
    return result;
  }
  if (tree->sessions.empty())
  {
    add_output(result, "No sessions for this workspace.");
    return result;
  }
  std::string output;
  output += include_archived ? "Sessions (including archived):\n" : "Sessions:\n";
  auto const index_by_id = tree_index_by_id(tree->sessions);
  bool wrote_any = false;
  append_session_tree_lines(output, tree->sessions, index_by_id, tree->roots, tree->current_path, list_query, 0, include_archived, wrote_any);
  if (output.empty() && !list_query.empty())
  {
    add_output(result, "No sessions matching: " + sanitize_inline_text(list_query));
    return result;
  }
  if (!wrote_any && !list_query.empty())
  {
    add_output(result, "No sessions matching: " + sanitize_inline_text(list_query));
    return result;
  }
  if (!wrote_any && !include_archived)
  {
    add_output(result, "No active sessions for this workspace. Use /sessions --archived to include archived sessions.");
    return result;
  }
  add_output(result, std::move(output));
  return result;
}

ava::core::Result<CommandResult> run_fork_command(runtime::Session& session, std::string_view name)
{
  return run_branch_command(session, name, ava::session::SessionBranchMode::Fork);
}

ava::core::Result<CommandResult> run_clone_command(runtime::Session& session, std::string_view name)
{
  return run_branch_command(session, name, ava::session::SessionBranchMode::Clone);
}

ava::core::Result<CommandResult> run_new_session_command(runtime::Session& session, std::string_view name)
{
  CommandResult result;
  result.handled = true;

  auto const previous_session_id = session.store.session_id();
  auto const trimmed_name = trim_ascii(name);
  auto opened = create_fresh_session(session);
  if (!opened)
    return std::unexpected(std::move(opened.error()));

  auto const created_session_id = opened->store.session_id();
  if (!trimmed_name.empty())
  {
    auto metadata =
        append_runtime_session_metadata(*opened, ava::session::SessionMetadataUpdate{.name = std::optional<std::string>(trimmed_name), .actor = "tui"});
    if (!metadata)
      return std::unexpected(std::move(metadata.error()));
  }

  if (auto replaced = replace_runtime_session(session, std::move(*opened)); !replaced)
    return std::unexpected(std::move(replaced.error()));

  std::string output = "started session " + created_session_id;
  if (!trimmed_name.empty())
    output += " name=\"" + sanitize_inline_text(trimmed_name) + "\"";
  output += "\nprevious session " + previous_session_id;
  output += "\nswitched to " + session.store.session_id();
  add_output(result, std::move(output));
  return result;
}

ava::core::Result<CommandResult> run_resume_command(runtime::Session& session, std::string_view session_id)
{
  CommandResult result;
  result.handled = true;

  auto const trimmed_session_id = trim_ascii(session_id);
  if (trimmed_session_id.empty())
  {
    add_output(result, missing_argument("/resume <id>"));
    return result;
  }

  auto opened = reopen_session(session, trimmed_session_id);
  if (!opened)
    return std::unexpected(std::move(opened.error()));

  if (auto replaced = replace_runtime_session(session, std::move(*opened)); !replaced)
    return std::unexpected(std::move(replaced.error()));
  add_output(result, "resumed session " + session.store.session_id());
  return result;
}

ava::core::Result<CommandResult> run_name_command(runtime::Session& session, std::string_view name)
{
  CommandResult result;
  result.handled = true;

  auto const trimmed_name = trim_ascii(name);
  if (trimmed_name.empty())
  {
    add_output(result, missing_argument("/name <name|--clear>"));
    return result;
  }

  auto const clear_name = trimmed_name == "--clear";
  ava::session::SessionMetadataUpdate update;
  update.name = clear_name ? std::optional<std::string>(std::string{}) : std::optional<std::string>(trimmed_name);
  update.actor = "tui";
  auto metadata = append_runtime_session_metadata(session, std::move(update));
  if (!metadata)
    return std::unexpected(std::move(metadata.error()));

  if (clear_name)
    add_output(result, "session name cleared");
  else
    add_output(result, "session name set: \"" + sanitize_inline_text(metadata->name) + "\"");
  return result;
}

ava::core::Result<CommandResult> run_labels_command(runtime::Session& session, std::string_view labels)
{
  CommandResult result;
  result.handled = true;

  auto const parts = split_command_arguments(labels);
  if (parts.empty())
  {
    auto metadata = load_runtime_metadata(session);
    if (!metadata)
      return std::unexpected(std::move(metadata.error()));
    auto text = metadata->labels.empty() ? std::string("session labels: <none>") : std::string("session labels: ") + labels_text(metadata->labels);
    text += "\nusage: /labels <label> [label...] | /labels --clear";
    add_output(result, std::move(text));
    return result;
  }

  if (std::ranges::find(parts, "--clear") != parts.end() && parts.size() != 1)
  {
    add_output(result, missing_argument("/labels <label> [label...] | /labels --clear"));
    return result;
  }

  auto next_labels = parts.size() == 1 && parts[0] == "--clear" ? std::vector<std::string>{} : parts;
  ava::session::SessionMetadataUpdate update;
  update.labels = next_labels;
  update.actor = "tui";
  auto metadata = append_runtime_session_metadata(session, std::move(update));
  if (!metadata)
    return std::unexpected(std::move(metadata.error()));

  if (metadata->labels.empty())
    add_output(result, "session labels cleared");
  else
    add_output(result, "session labels set: " + sanitize_inline_text(labels_text(metadata->labels)));
  return result;
}

ava::core::Result<CommandResult> run_mode_command(runtime::Session& session)
{
  CommandResult result;
  result.handled = true;
  auto const new_mode = ava::agent::toggle_mode(session.mode);
  auto prompt_state = select_runtime_prompt_state(session, new_mode);
  if (!prompt_state)
    return std::unexpected(std::move(prompt_state.error()));
  if (auto appended = append_runtime_mode_change(session, new_mode); !appended)
  {
    return std::unexpected(std::move(appended.error()));
  }
  apply_runtime_prompt_state(session, std::move(*prompt_state));
  add_output(result, "mode switched to " + ava::agent::to_string(session.mode));
  return result;
}

ava::core::Result<CommandResult> run_context_command(runtime::Session& session, std::string_view query)
{
  CommandResult result;
  result.handled = true;
  auto const trimmed_query = trim_ascii(query);
  auto const include_project_resources = project_resources_trusted(session.project_trust);
  auto const project_lsp_config = session.workspace_dir / ".ava" / "lsp.json";
  auto const lsp_inspection = ava::lsp::inspect_configured_lsp_provider(ava::lsp::ConfiguredLspProviderFiles{
      .global_config_file = session.paths.ava_config_dir / "lsp.json",
      .project_config_file = include_project_resources ? project_lsp_config : std::filesystem::path{},
      .workspace_root = session.workspace_dir,
      .mode = session.mode,
  });
  std::string output = "Context freshness:\n";
  output += "  mode=" + ava::agent::to_string(session.mode) + "\n";
  output += "  model=" + session.model.provider_id + "/" + session.model.model_id + "\n";
  output += "  project_trust=" + std::string(to_string(session.project_trust.decision)) +
            " project_resources=" + (include_project_resources ? std::string("enabled") : std::string("skipped")) + "\n";
  if (prompt_matches_query(session, trimmed_query))
  {
    output += "  base_prompt=";
    if (session.base_prompt.from_override)
    {
      output += "override";
      if (session.base_prompt.source_path)
        output += " path=" + session.base_prompt.source_path->string() + " " +
                  context_file_status(*session.base_prompt.source_path, session.base_prompt.byte_count, session.base_prompt.content_fingerprint);
    }
    else
    {
      output += "builtin";
    }
    output += " bytes=" + std::to_string(session.base_prompt.byte_count) + "\n";
  }
  output += "  context_sources=" + std::to_string(session.context_sources.size()) + "\n";
  auto const system_prompt_sources = freshness_source_count(session.freshness_sources, runtime::FreshnessSourceKind::SystemPrompt) +
                                     freshness_source_count(session.freshness_sources, runtime::FreshnessSourceKind::AppendSystemPrompt);
  output += "  system_prompt_sources=" + std::to_string(system_prompt_sources) + "\n";
  auto const prompt_command_sources = freshness_source_count(session.freshness_sources, runtime::FreshnessSourceKind::PromptCommand);
  output += "  prompt_commands=" + std::to_string(prompt_command_sources) + "\n";
  auto const skill_sources = freshness_source_count(session.freshness_sources, runtime::FreshnessSourceKind::Skill);
  output += "  skills=" + std::to_string(skill_sources) + "\n";
  auto const plugin_sources = freshness_source_count(session.freshness_sources, runtime::FreshnessSourceKind::PluginManifest) +
                              freshness_source_count(session.freshness_sources, runtime::FreshnessSourceKind::PluginPrompt) +
                              freshness_source_count(session.freshness_sources, runtime::FreshnessSourceKind::PluginSkill);
  output += "  plugin_sources=" + std::to_string(plugin_sources) + "\n";
  output += "  lsp_status=" + lsp_provider_status_text(lsp_inspection) + " lsp_configs=" + std::to_string(configured_lsp_config_count(lsp_inspection.configs)) +
            " lsp_servers=" + std::to_string(lsp_inspection.server_count) + " lsp_errors=" + std::to_string(lsp_inspection.error_count) + "\n";

  bool matched_source = false;
  for (auto const& source : session.context_sources)
  {
    if (!context_source_matches_query(source, trimmed_query))
      continue;
    matched_source = true;
    output += "  " + ava::context::to_string(source.source_type) + "  " + source.path.string() + "  loaded_bytes=" + std::to_string(source.byte_count) + "  " +
              context_file_status(source.path, source.byte_count, source.content_fingerprint) + '\n';
  }
  bool matched_freshness_source = false;
  for (auto const& source : session.freshness_sources)
  {
    if (!freshness_source_matches_query(source, trimmed_query))
      continue;
    matched_freshness_source = true;
    output += "  " + std::string(freshness_source_kind_text(source.kind)) + "  " + source.scope + "  ";
    if (!source.source_id.empty())
    {
      output += sanitize_inline_text(source.source_id);
      if (!source.name.empty() && source.name != source.source_id)
        output += "/" + sanitize_inline_text(source.name);
    }
    else
    {
      output += sanitize_inline_text(source.name);
    }
    output += "  " + freshness_source_status_text(source) + '\n';
  }
  bool matched_lsp_config = false;
  for (auto const& diagnostic : lsp_inspection.configs)
  {
    bool const visible = diagnostic.error.has_value() || (!trimmed_query.empty() && lsp_config_matches_query(diagnostic, trimmed_query));
    if (!visible || !lsp_config_matches_query(diagnostic, trimmed_query))
      continue;
    matched_lsp_config = true;
    output += "  lsp_config  " + sanitize_inline_text(diagnostic.scope) + "  " + lsp_config_status_text(diagnostic) + '\n';
  }
  if (!include_project_resources)
  {
    ava::lsp::ConfiguredLspConfigDiagnostic skipped_project{
        .scope = "project",
        .path = project_lsp_config,
        .exists = path_exists_for_status(project_lsp_config),
    };
    bool const visible = !trimmed_query.empty() && lsp_config_matches_query(skipped_project, trimmed_query);
    if (visible)
    {
      matched_lsp_config = true;
      output += "  lsp_config  project  " + project_lsp_config.string() + "  status=skipped reason=project_resources_skipped\n";
    }
  }
  if (!matched_source && !matched_freshness_source && !matched_lsp_config && !prompt_matches_query(session, trimmed_query) && !trimmed_query.empty())
  {
    add_output(result, "No context sources matching: " + sanitize_inline_text(trimmed_query));
    return result;
  }
  add_output(result, std::move(output));
  return result;
}

ava::core::Result<CommandResult> run_stats_command(runtime::Session& session)
{
  CommandResult result;
  result.handled = true;
  auto entries = load_runtime_entries(session);
  if (!entries)
  {
    add_output(result, entries.error().format());
    return result;
  }
  add_output(result, format_session_stats_text(session, ava::session::compute_session_stats(*entries)));
  return result;
}

ava::core::Result<CommandResult> run_compact_command(runtime::Session& session, CommandRequest const& request)
{
  CommandResult result;
  result.handled = true;
  auto fail_compaction = [&](ava::core::Error error) -> ava::core::Result<CommandResult> {
    if (request.propagate_compaction_errors)
      return std::unexpected(std::move(error));
    add_output(result, error.format());
    return result;
  };
  auto const instructions = command_argument(request.command, "/compact");
  if (!request.compaction_summary_generator)
  {
    return fail_compaction(ava::core::Error(ava::core::ErrorCategory::InvalidArgument, "/compact requires provider-backed summary generation"));
  }
  auto config = ava::session::load_compaction_config(session.paths);
  if (!config)
  {
    return fail_compaction(std::move(config.error()));
  }
  auto read_authority = session.read_authority();
  if (!read_authority)
    return fail_compaction(std::move(read_authority.error()));

  constexpr std::size_t max_compaction_attempts = 2;
  std::size_t last_snapshot_entries = 0;
  std::size_t last_current_entries = 0;
  for (std::size_t attempt = 0; attempt < max_compaction_attempts; ++attempt)
  {
    if (command_canceled(request))
      return fail_compaction(command_canceled_error());
    ava::core::Result<std::vector<ava::session::SessionEntry>> entries =
        std::unexpected(ava::core::Error(ava::core::ErrorCategory::Unknown, "session entries were not loaded"));
    if (request.session_mutex)
    {
      std::lock_guard lock(*request.session_mutex);
      entries = read_authority->load();
    }
    else
    {
      entries = read_authority->load();
    }
    if (!entries)
    {
      return fail_compaction(std::move(entries.error()));
    }
    auto const estimated_tokens = ava::session::estimate_session_tokens(*entries);
    auto start_event = base_command_event(session, runtime::EventType::CompactionStart);
    start_event.trigger = "manual";
    start_event.status = "started";
    start_event.attempt = attempt + 1;
    start_event.max_attempts = max_compaction_attempts;
    start_event.estimated_tokens = estimated_tokens;
    if (auto emitted = emit_command_event(request, std::move(start_event)); !emitted)
    {
      return fail_compaction(std::move(emitted.error()));
    }
    auto summary = request.compaction_summary_generator(*entries, *config, instructions, estimated_tokens);
    if (!summary)
    {
      return fail_compaction(std::move(summary.error()));
    }
    if (command_canceled(request))
      return fail_compaction(command_canceled_error());
    if (summary->empty())
    {
      return fail_compaction(ava::core::Error(ava::core::ErrorCategory::Provider, "compaction summary generation returned an empty summary"));
    }
    if (summary->size() > config->max_summary_bytes)
    {
      auto error = ava::core::Error(ava::core::ErrorCategory::InvalidArgument, "generated compaction summary is too large");
      error.with_context("max_summary_bytes", std::to_string(config->max_summary_bytes));
      error.with_context("summary_bytes", std::to_string(summary->size()));
      return fail_compaction(std::move(error));
    }

    bool snapshot_stale = false;
    auto validate_and_append = [&]() -> ava::core::VoidResult {
      auto current_entries = read_authority->load();
      if (!current_entries)
        return std::unexpected(std::move(current_entries.error()));
      if (command_canceled(request))
        return std::unexpected(command_canceled_error());
      if (!same_session_snapshot(*entries, *current_entries))
      {
        snapshot_stale = true;
        last_snapshot_entries = entries->size();
        last_current_entries = current_entries->size();
        return {};
      }
      auto entry = ava::session::make_manual_compaction_entry(ava::session::ManualCompactionRequest{.summary = *summary,
                                                                                                    .instructions = instructions,
                                                                                                    .config = *config,
                                                                                                    .estimated_tokens = estimated_tokens,
                                                                                                    .threshold_tokens = 0,
                                                                                                    .trigger = "manual",
                                                                                                    .recent_context = ""});
      if (!entry)
        return std::unexpected(std::move(entry.error()));
      return session.append_owned(std::move(*entry));
    };
    ava::core::VoidResult appended;
    if (request.session_mutex)
    {
      std::lock_guard lock(*request.session_mutex);
      appended = validate_and_append();
    }
    else
    {
      appended = validate_and_append();
    }
    if (!appended)
    {
      return fail_compaction(std::move(appended.error()));
    }
    if (!snapshot_stale)
    {
      auto end_event = base_command_event(session, runtime::EventType::CompactionEnd);
      end_event.trigger = "manual";
      end_event.status = "completed";
      end_event.attempt = attempt + 1;
      end_event.max_attempts = max_compaction_attempts;
      end_event.estimated_tokens = estimated_tokens;
      end_event.summary_bytes = summary->size();
      if (auto emitted = emit_command_event(request, std::move(end_event)); !emitted)
      {
        return fail_compaction(std::move(emitted.error()));
      }
      add_output(result, "compaction summary recorded");
      return result;
    }
    if (attempt + 1 < max_compaction_attempts)
    {
      auto retry_event = base_command_event(session, runtime::EventType::Retry);
      retry_event.trigger = "manual";
      retry_event.reason = "stale_compaction_snapshot";
      retry_event.status = "started";
      retry_event.attempt = attempt + 2;
      retry_event.max_attempts = max_compaction_attempts;
      retry_event.snapshot_entries = last_snapshot_entries;
      retry_event.current_entries = last_current_entries;
      if (auto emitted = emit_command_event(request, std::move(retry_event)); !emitted)
      {
        return fail_compaction(std::move(emitted.error()));
      }
    }
  }
  return fail_compaction(stale_compaction_snapshot_error("manual", last_snapshot_entries, last_current_entries));
}

ava::core::Result<std::vector<ava::session::SessionEntry>> load_import_session_entries(std::filesystem::path const& path)
{
  std::error_code status_error;
  auto const status = std::filesystem::symlink_status(path, status_error);
  if (status_error || !std::filesystem::exists(status))
  {
    auto error = ava::core::Error(ava::core::ErrorCategory::NotFound, "session import file not found");
    error.with_context("path", path.string());
    if (status_error)
      error.with_context("cause", status_error.message());
    return std::unexpected(std::move(error));
  }
  if (std::filesystem::is_symlink(status) || !std::filesystem::is_regular_file(status))
  {
    auto error = ava::core::Error(ava::core::ErrorCategory::PermissionDenied, "session import path must be a regular file and not a symlink");
    error.with_context("path", path.string());
    return std::unexpected(std::move(error));
  }

  std::ifstream file(path, std::ios::binary);
  if (!file)
  {
    auto error = ava::core::Error(ava::core::ErrorCategory::Io, "failed to open session import file");
    error.with_context("path", path.string());
    return std::unexpected(std::move(error));
  }

  std::vector<ava::session::SessionEntry> entries;
  std::string line;
  while (true)
  {
    auto line_read = ava::session::read_limited_session_line(file, line);
    if (!line_read)
      return std::unexpected(std::move(line_read.error()));
    if (!*line_read)
      break;
    if (entries.empty() && looks_like_pi_session_header(line))
      return std::unexpected(unsupported_pi_session_import_error(path));
    auto entry = ava::session::parse_session_entry_line(line, path);
    if (!entry)
      return std::unexpected(std::move(entry.error()));
    entries.push_back(std::move(*entry));
  }

  if (entries.empty())
  {
    auto error = ava::core::Error(ava::core::ErrorCategory::InvalidArgument, "session import file has no entries");
    error.with_context("path", path.string());
    return std::unexpected(std::move(error));
  }

  auto validation = ava::session::validate_session_replay(entries);
  if (!validation.ok())
  {
    auto error = ava::core::Error(ava::core::ErrorCategory::Session, "session import failed validation");
    error.with_context("path", path.string());
    error.with_context("errors", std::to_string(validation.error_count));
    if (!validation.issues.empty())
      error.with_context("first_issue", validation.issues.front().message);
    return std::unexpected(std::move(error));
  }
  if (auto attachments = validate_raw_jsonl_import_attachments(entries, path); !attachments)
    return std::unexpected(std::move(attachments.error()));
  return entries;
}

ava::core::Result<CommandResult> run_import_command(runtime::Session& session, std::string_view argument)
{
  CommandResult result;
  result.handled = true;
  auto const args = split_command_arguments(argument);
  if (args.empty())
  {
    add_output(result, "usage: /import <path.jsonl> --confirm");
    return result;
  }

  bool confirm = false;
  std::optional<std::string> path_arg;
  for (auto const& arg : args)
  {
    if (arg == "--confirm")
    {
      confirm = true;
      continue;
    }
    if (!path_arg)
    {
      path_arg = arg;
      continue;
    }
    add_output(result, "usage: /import <path.jsonl> --confirm");
    return result;
  }
  if (!path_arg)
  {
    add_output(result, "usage: /import <path.jsonl> --confirm");
    return result;
  }

  auto import_path = std::filesystem::path(*path_arg);
  if (import_path.is_relative())
    import_path = session.current_dir / import_path;
  import_path = import_path.lexically_normal();

  auto entries = load_import_session_entries(import_path);
  if (!entries)
  {
    add_output(result, entries.error().format());
    return result;
  }
  if (!confirm)
  {
    add_output(result, "session import is ready: " + import_path.string() + "\n  entries: " + std::to_string(entries->size()) +
                           "\n  re-run with --confirm to create a new AVA session and switch to it");
    return result;
  }

  auto imported_store = ava::session::SessionStore::create(session.workspace_dir, session.paths.sessions_dir);
  if (!imported_store)
  {
    add_output(result, imported_store.error().format());
    return result;
  }
  auto imported_lease = ava::session::SessionLease::create_and_acquire(imported_store->session_path());
  if (!imported_lease)
  {
    add_output(result, imported_lease.error().format());
    return result;
  }
  for (auto const& entry : *entries)
  {
    auto entry_to_append = entry;
    if (entry_to_append.version == 0)
      entry_to_append.version = ava::session::kCurrentSessionEntryVersion;
    if (auto appended = imported_store->append(*imported_lease, entry_to_append); !appended)
    {
      auto error = std::move(appended.error());
      attach_created_session_cleanup_context(*imported_store, *imported_lease, error);
      add_output(result, error.format());
      return result;
    }
  }

  auto owned_options = owned_replacement_options(session);
  auto opened = open_owned_runtime_session(owned_options, *imported_store, *imported_lease, true);
  if (!opened)
  {
    auto error = std::move(opened.error());
    attach_created_session_cleanup_context(*imported_store, *imported_lease, error);
    add_output(result, error.format());
    return result;
  }
  if (auto replaced = replace_runtime_session(session, std::move(*opened)); !replaced)
    return std::unexpected(std::move(replaced.error()));
  add_output(result, "imported session " + session.store.session_id() + " from " + import_path.string() + "\n  entries: " + std::to_string(entries->size()) +
                         "\n  switched to " + session.store.session_id());
  return result;
}

ava::core::Result<CommandResult> run_recover_persistence_command(runtime::Session& session)
{
  CommandResult result;
  result.handled = true;
  if (!session.run_controller)
  {
    add_output(result, "session append controller is unavailable");
    return result;
  }
  if (auto recovered = session.run_controller->reset_persistence_failure(); !recovered)
  {
    add_output(result, recovered.error().format());
    return result;
  }
  add_output(result, "session persistence recovered; append failure latch cleared");
  return result;
}

ava::core::Result<CommandResult> run_export_command(runtime::Session& session, CommandRequest const& request)
{
  CommandResult result;
  result.handled = true;
  auto parsed = parse_export_command_arguments(command_argument(request.command, "/export"));
  if (!parsed)
  {
    add_output(result, parsed.error().format());
    return result;
  }
  auto entries = load_runtime_entries(session);
  if (!entries)
  {
    add_output(result, entries.error().format());
    return result;
  }

  auto const format = parsed->format;
  auto const jsonl_attachment_summary = format == ExportFormat::Jsonl ? summarize_non_redacted_jsonl_attachments(*entries) : JsonlAttachmentSummary{};
  auto const jsonl_attachment_note = raw_jsonl_attachment_note(jsonl_attachment_summary);
  std::string content;
  if (format == ExportFormat::Html)
  {
    content = ava::session::format_session_html(*entries);
  }
  else if (format == ExportFormat::Jsonl)
  {
    auto jsonl = format_session_jsonl(*entries);
    if (!jsonl)
    {
      add_output(result, jsonl.error().format());
      return result;
    }
    content = std::move(*jsonl);
  }
  else
  {
    content = ava::session::format_session_markdown(*entries);
  }
  if (parsed->path.empty())
  {
    add_output(result, std::move(content));
    return result;
  }

  auto const target_path = resolve_export_path(session, parsed->path);
  auto context = make_tool_context(session, request.permission_resolver);
  context.permission_request_ids = std::make_shared<std::vector<std::string>>();
  auto linked_permission_ids = [&]() -> std::vector<std::string> {
    return context.permission_request_ids ? *context.permission_request_ids : std::vector<std::string>{};
  };

  auto const call_id = ava::core::make_id("cmd");
  auto const path_text = display_path(target_path, session.current_dir);
  if (auto recorded = record_tool_start(session, request.event_sink, result, call_id, "export", path_text); !recorded)
    return std::unexpected(std::move(recorded.error()));

  auto written = ava::tools::write_file(context, target_path, content);
  if (!written)
  {
    auto const text = written.error().format();
    if (auto recorded = record_tool_result(session, request.event_sink, result, call_id, "export", ava::agent::ToolTimelineStatus::Error, text, {},
                                           linked_permission_ids());
        !recorded)
      return std::unexpected(std::move(recorded.error()));
    add_output(result, text);
    return result;
  }

  auto result_json = std::string("{\"tool\":\"export\",\"ok\":true,\"format\":\"") + export_format_text(format) + "\",\"path\":\"" +
                     ava::core::json::escape(target_path.string()) + "\",\"bytes_written\":" + std::to_string(written->bytes_written);
  if (format == ExportFormat::Jsonl)
  {
    result_json += ",\"attachment_files_included\":false,\"non_redacted_image_attachment_metadata_count\":" + std::to_string(jsonl_attachment_summary.count);
  }
  result_json += "}";
  if (auto recorded = record_tool_result(session, request.event_sink, result, call_id, "export", ava::agent::ToolTimelineStatus::Success,
                                         "wrote " + std::to_string(written->bytes_written) + " bytes", result_json, linked_permission_ids());
      !recorded)
    return std::unexpected(std::move(recorded.error()));
  auto output = "exported session:\n  format: " + export_format_text(format) + "\n  path: " + target_path.string() +
                "\n  bytes: " + std::to_string(written->bytes_written);
  if (!jsonl_attachment_note.empty())
    output += "\n  note: " + jsonl_attachment_note;
  add_output(result, std::move(output));
  return result;
}

}  // namespace ava::app
