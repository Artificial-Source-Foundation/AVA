#include "ava/app/command_permissions.h"

#include "ava/app/command_format.h"

#include "ava/core/json.h"
#include "ava/permissions/permission_rules.h"
#include "ava/session/session_store.h"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

namespace ava::app {
namespace {

struct ParsedAddRule
{
  ava::permissions::PermissionRuleDraft draft;
};

struct PermissionAuditRow
{
  std::string timestamp;
  std::string entry_id;
  std::string request_id;
  std::string operation;
  std::string mode;
  std::string tool_name;
  std::string action;
  std::string reason;
  std::string risk;
  std::string target_path;
  std::string command;
  std::string resolution;
  std::string resolution_source;
  std::string resolution_reason;
  std::string actor;
  std::string rule_id;
};

ava::permissions::PermissionRuleStore permission_rule_store_for_session(RuntimeSession const& session)
{
  return ava::permissions::PermissionRuleStore{
      .global_rules_file = session.paths.ava_config_dir / "permission-rules.json",
      .workspace_rules_file = session.workspace_dir / ".ava" / "permission-rules.json",
      .workspace_dir = session.workspace_dir,
  };
}

std::string permissions_usage()
{
  return "usage: /permissions <list|audit|diagnose|explain|add|remove> ...\n"
         "  /permissions list [query]\n"
         "  /permissions audit [query]\n"
         "  /permissions audit summary [query]\n"
         "  /permissions audit show <entry_id|request_id>\n"
         "  /permissions audit export [query]\n"
         "  /permissions diagnose [query]\n"
         "  /permissions explain <rule_id>\n"
         "  /permissions add action=<allow|deny> operation=<operation> reason=\"<why>\" "
         "[scope=workspace|global] [mode=any|build|plan] [path=<path>] [command=\"<cmd>\"] [tool=<tool>]\n"
         "  /permissions remove <rule_id>\n"
         "operations: read, search, edit, bash, network.fetch, network.search, lsp.server.launch, lsp.query, "
         "skill, plugin.execute, plugin.tool.call, plugin.command.run, plugin.event.observe, mcp.server.launch, "
         "mcp.server.connect, mcp.tool.call, mcp.resource.read";
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
  std::ranges::transform(lowered, lowered.begin(),
                         [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
  return lowered;
}

bool contains_ascii_case_insensitive(std::string_view text, std::string_view query)
{
  if (query.empty()) return true;
  return lower_ascii(text).find(lower_ascii(query)) != std::string::npos;
}

std::string display_permission_path(std::filesystem::path const& path, std::filesystem::path const& workspace)
{
  if (path.empty()) return "";
  auto text = display_path(path, workspace);
  if (text == ".." || text.starts_with("../"))
    return path.generic_string();
  return text;
}

std::string permission_rule_storage_path(ava::permissions::PermissionRuleStore const& store,
                                         ava::permissions::PersistentPermissionRule const& rule)
{
  return ava::permissions::enforceable_permission_rules_file(store, rule.scope).generic_string();
}

std::string rule_target_text(ava::permissions::PersistentPermissionRule const& rule, RuntimeSession const& session)
{
  std::string text;
  if (!rule.target_path.empty())
    text += " path=" + sanitize_inline_text(display_permission_path(rule.target_path, session.workspace_dir));
  if (!rule.command.empty())
    text += " command=\"" + sanitize_inline_text(rule.command) + "\"";
  if (!rule.tool_name.empty())
    text += " tool=" + sanitize_inline_text(rule.tool_name);
  return text;
}

std::string format_rule_line(ava::permissions::PersistentPermissionRule const& rule, RuntimeSession const& session)
{
  std::string line = "- " + sanitize_inline_text(rule.rule_id);
  line += "  " + ava::permissions::to_string(rule.action);
  line += " " + ava::permissions::to_string(rule.operation);
  line += " scope=" + ava::permissions::to_string(rule.scope);
  line += " mode=" + ava::permissions::to_string(rule.mode);
  line += rule_target_text(rule, session);
  line += " reason=\"" + sanitize_inline_text(rule.reason) + "\"";
  return line;
}

bool rule_matches_query(ava::permissions::PersistentPermissionRule const& rule, RuntimeSession const& session,
                        std::string_view query)
{
  if (query.empty()) return true;
  return contains_ascii_case_insensitive(rule.rule_id, query) ||
         contains_ascii_case_insensitive(ava::permissions::to_string(rule.scope), query) ||
         contains_ascii_case_insensitive(ava::permissions::to_string(rule.action), query) ||
         contains_ascii_case_insensitive(ava::permissions::to_string(rule.operation), query) ||
         contains_ascii_case_insensitive(ava::permissions::to_string(rule.mode), query) ||
         contains_ascii_case_insensitive(rule.tool_name, query) ||
         contains_ascii_case_insensitive(display_permission_path(rule.target_path, session.workspace_dir), query) ||
         contains_ascii_case_insensitive(rule.command, query) ||
         contains_ascii_case_insensitive(rule.reason, query) ||
         contains_ascii_case_insensitive(rule.actor, query) ||
         contains_ascii_case_insensitive(rule.created_at, query);
}

std::string audit_field(std::string_view data_json, std::string_view key)
{
  return ava::core::json::string_field(data_json, key).value_or("");
}

PermissionAuditRow permission_audit_row(ava::session::SessionEntry const& entry, RuntimeSession const& session)
{
  auto target_path = audit_field(entry.data_json, "target_path");
  if (!target_path.empty())
    target_path = display_permission_path(target_path, session.workspace_dir);

  return PermissionAuditRow{.timestamp = entry.timestamp,
                            .entry_id = entry.id,
                            .request_id = audit_field(entry.data_json, "permission_request_id"),
                            .operation = audit_field(entry.data_json, "operation"),
                            .mode = audit_field(entry.data_json, "mode"),
                            .tool_name = audit_field(entry.data_json, "tool_name"),
                            .action = audit_field(entry.data_json, "action"),
                            .reason = audit_field(entry.data_json, "reason"),
                            .risk = audit_field(entry.data_json, "risk"),
                            .target_path = std::move(target_path),
                            .command = audit_field(entry.data_json, "command"),
                            .resolution = audit_field(entry.data_json, "resolution"),
                            .resolution_source = audit_field(entry.data_json, "resolution_source"),
                            .resolution_reason = audit_field(entry.data_json, "resolution_reason"),
                            .actor = audit_field(entry.data_json, "actor"),
                            .rule_id = audit_field(entry.data_json, "rule_id")};
}

void append_audit_field(std::string& line, std::string_view label, std::string_view value)
{
  if (value.empty())
    return;
  line += " ";
  line += label;
  line += "=";
  line += sanitize_inline_text(std::string(value));
}

void append_audit_quoted_field(std::string& line, std::string_view label, std::string_view value)
{
  if (value.empty())
    return;
  line += " ";
  line += label;
  line += "=\"";
  line += sanitize_inline_text(std::string(value));
  line += "\"";
}

std::string format_permission_audit_line(PermissionAuditRow const& row)
{
  std::string line = "- " + sanitize_inline_text(row.timestamp);
  append_audit_field(line, "entry", row.entry_id);
  append_audit_field(line, "request", row.request_id);
  append_audit_field(line, "action", row.action);
  append_audit_field(line, "resolution", row.resolution);
  append_audit_field(line, "source", row.resolution_source);
  append_audit_field(line, "operation", row.operation);
  append_audit_field(line, "mode", row.mode);
  append_audit_field(line, "risk", row.risk);
  append_audit_field(line, "tool", row.tool_name);
  append_audit_field(line, "path", row.target_path);
  append_audit_quoted_field(line, "command", row.command);
  append_audit_quoted_field(line, "reason", row.reason);
  append_audit_quoted_field(line, "resolution_reason", row.resolution_reason);
  append_audit_field(line, "rule", row.rule_id);
  append_audit_field(line, "actor", row.actor);
  return line;
}

std::string markdown_table_cell(std::string_view text)
{
  auto sanitized = sanitize_inline_text(std::string(text));
  std::string escaped;
  escaped.reserve(sanitized.size());
  for (char const ch : sanitized)
  {
    if (ch == '|')
      escaped.push_back('\\');
    escaped.push_back(ch);
  }
  return escaped;
}

bool audit_line_matches_query(std::string const& line, std::string_view data_json, std::string_view query)
{
  if (query.empty()) return true;
  return contains_ascii_case_insensitive(line, query) || contains_ascii_case_insensitive(data_json, query);
}

std::string format_permission_audit(std::vector<ava::session::SessionEntry> const& entries, RuntimeSession const& session,
                                    std::string_view query)
{
  static constexpr std::size_t kMaxDisplayedAuditRows = 50;

  std::ostringstream output;
  output << "Permission audit:\n";
  output << "  session: " << sanitize_inline_text(session.store.session_id()) << "\n";
  output << "  behavior: append-only session permission decisions; most recent first\n";
  if (!query.empty())
    output << "  filter: " << sanitize_inline_text(std::string(query)) << "\n";

  std::size_t audit_count = 0;
  std::size_t matched_count = 0;
  std::vector<std::string> rows;

  for (auto entry = entries.rbegin(); entry != entries.rend(); ++entry)
  {
    if (entry->type != ava::session::EntryType::PermissionDecision)
      continue;
    ++audit_count;

    auto const row = permission_audit_row(*entry, session);
    auto const line = format_permission_audit_line(row);
    if (!audit_line_matches_query(line, entry->data_json, query))
      continue;
    ++matched_count;
    if (rows.size() < kMaxDisplayedAuditRows)
      rows.push_back(line);
  }

  if (audit_count == 0)
  {
    output << "\nNo permission audit entries for this session.";
    return output.str();
  }
  if (matched_count == 0)
  {
    output << "\nNo permission audit entries match \"" << sanitize_inline_text(std::string(query)) << "\".";
    return output.str();
  }

  output << "  showing: " << rows.size() << " of " << matched_count << " matching entries";
  if (matched_count != audit_count)
    output << " (" << audit_count << " total audit entries)";
  if (matched_count > rows.size())
    output << "; newest " << kMaxDisplayedAuditRows << " displayed";
  output << "\n\n";

  for (auto const& row : rows)
  {
    output << row << "\n";
  }
  return output.str();
}

std::string format_permission_audit_export(std::vector<ava::session::SessionEntry> const& entries,
                                           RuntimeSession const& session, std::string_view query)
{
  static constexpr std::size_t kMaxDisplayedAuditRows = 50;

  std::ostringstream output;
  output << "Permission audit export:\n";
  output << "  session: " << sanitize_inline_text(session.store.session_id()) << "\n";
  output << "  format: markdown table\n";
  output << "  behavior: append-only session permission decisions; most recent first\n";
  if (!query.empty())
    output << "  filter: " << sanitize_inline_text(std::string(query)) << "\n";

  std::size_t audit_count = 0;
  std::size_t matched_count = 0;
  std::vector<PermissionAuditRow> rows;

  for (auto entry = entries.rbegin(); entry != entries.rend(); ++entry)
  {
    if (entry->type != ava::session::EntryType::PermissionDecision)
      continue;
    ++audit_count;

    auto row = permission_audit_row(*entry, session);
    auto const line = format_permission_audit_line(row);
    if (!audit_line_matches_query(line, entry->data_json, query))
      continue;
    ++matched_count;
    if (rows.size() < kMaxDisplayedAuditRows)
      rows.push_back(std::move(row));
  }

  if (audit_count == 0)
  {
    output << "\nNo permission audit entries for this session.";
    return output.str();
  }
  if (matched_count == 0)
  {
    output << "\nNo permission audit entries match \"" << sanitize_inline_text(std::string(query)) << "\".";
    return output.str();
  }

  output << "  rows: " << rows.size() << " of " << matched_count << " matching entries";
  if (matched_count != audit_count)
    output << " (" << audit_count << " total audit entries)";
  if (matched_count > rows.size())
    output << "; newest " << kMaxDisplayedAuditRows << " exported";
  output << "\n\n";

  output << "```markdown\n";
  output << "| timestamp | entry | request | action | resolution | source | operation | mode | risk | tool | target | command | "
            "reason | resolution reason | rule | actor |\n";
  output << "| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |\n";
  for (auto const& row : rows)
  {
    output << "| " << markdown_table_cell(row.timestamp) << " | " << markdown_table_cell(row.entry_id) << " | "
           << markdown_table_cell(row.request_id) << " | " << markdown_table_cell(row.action) << " | "
           << markdown_table_cell(row.resolution) << " | " << markdown_table_cell(row.resolution_source) << " | "
           << markdown_table_cell(row.operation) << " | " << markdown_table_cell(row.mode) << " | "
           << markdown_table_cell(row.risk) << " | " << markdown_table_cell(row.tool_name) << " | "
           << markdown_table_cell(row.target_path) << " | " << markdown_table_cell(row.command) << " | "
           << markdown_table_cell(row.reason) << " | " << markdown_table_cell(row.resolution_reason) << " | "
           << markdown_table_cell(row.rule_id) << " | " << markdown_table_cell(row.actor) << " |\n";
  }
  output << "```";
  return output.str();
}

void increment_summary_count(std::unordered_map<std::string, std::size_t>& counts, std::string_view value)
{
  if (value.empty())
    return;
  ++counts[std::string(value)];
}

std::string format_summary_counts(std::string_view label, std::unordered_map<std::string, std::size_t> const& counts)
{
  std::vector<std::pair<std::string, std::size_t>> sorted;
  sorted.reserve(counts.size());
  for (auto const& [key, count] : counts)
  {
    sorted.emplace_back(key, count);
  }
  std::ranges::sort(sorted, [](auto const& lhs, auto const& rhs) {
    if (lhs.second != rhs.second)
      return lhs.second > rhs.second;
    return lhs.first < rhs.first;
  });

  std::ostringstream output;
  output << "  by " << label << ": ";
  if (sorted.empty())
  {
    output << "none";
    return output.str();
  }

  for (auto index = std::size_t{0}; index < sorted.size(); ++index)
  {
    if (index > 0)
      output << ", ";
    output << sanitize_inline_text(sorted[index].first) << "=" << sorted[index].second;
  }
  return output.str();
}

std::string format_permission_audit_summary(std::vector<ava::session::SessionEntry> const& entries,
                                            RuntimeSession const& session, std::string_view query)
{
  std::ostringstream output;
  output << "Permission audit summary:\n";
  output << "  session: " << sanitize_inline_text(session.store.session_id()) << "\n";
  output << "  behavior: read-only grouped summary over append-only session permission decisions\n";
  if (!query.empty())
    output << "  filter: " << sanitize_inline_text(std::string(query)) << "\n";

  std::size_t audit_count = 0;
  std::size_t matched_count = 0;
  std::size_t denial_count = 0;
  std::string newest_matching_request;
  std::string newest_matching_entry;
  std::unordered_map<std::string, std::size_t> by_action;
  std::unordered_map<std::string, std::size_t> by_resolution;
  std::unordered_map<std::string, std::size_t> by_source;
  std::unordered_map<std::string, std::size_t> by_risk;
  std::unordered_map<std::string, std::size_t> by_operation;
  std::unordered_map<std::string, std::size_t> by_tool;

  for (auto entry = entries.rbegin(); entry != entries.rend(); ++entry)
  {
    if (entry->type != ava::session::EntryType::PermissionDecision)
      continue;
    ++audit_count;

    auto const row = permission_audit_row(*entry, session);
    auto const line = format_permission_audit_line(row);
    if (!audit_line_matches_query(line, entry->data_json, query))
      continue;

    ++matched_count;
    if (lower_ascii(row.action) == "deny" || lower_ascii(row.resolution) == "deny")
      ++denial_count;
    if (newest_matching_entry.empty())
      newest_matching_entry = row.entry_id;
    if (newest_matching_request.empty())
      newest_matching_request = row.request_id;
    increment_summary_count(by_action, row.action);
    increment_summary_count(by_resolution, row.resolution);
    increment_summary_count(by_source, row.resolution_source);
    increment_summary_count(by_risk, row.risk);
    increment_summary_count(by_operation, row.operation);
    increment_summary_count(by_tool, row.tool_name);
  }

  if (audit_count == 0)
  {
    output << "\nNo permission audit entries for this session.";
    return output.str();
  }
  if (matched_count == 0)
  {
    output << "\nNo permission audit entries match \"" << sanitize_inline_text(std::string(query)) << "\".";
    return output.str();
  }

  output << "  entries: " << matched_count << " matching";
  if (matched_count != audit_count)
    output << " of " << audit_count << " total";
  output << "\n";
  output << "  denials: " << denial_count << "\n";
  output << format_summary_counts("action", by_action) << "\n";
  output << format_summary_counts("resolution", by_resolution) << "\n";
  output << format_summary_counts("source", by_source) << "\n";
  output << format_summary_counts("risk", by_risk) << "\n";
  output << format_summary_counts("operation", by_operation) << "\n";
  output << format_summary_counts("tool", by_tool) << "\n";

  output << "\nRelated commands:\n";
  output << "  /permissions audit";
  if (!query.empty())
    output << " " << sanitize_inline_text(std::string(query));
  output << "\n";
  output << "  /permissions audit export";
  if (!query.empty())
    output << " " << sanitize_inline_text(std::string(query));
  output << "\n";
  output << "  /permissions diagnose";
  if (!query.empty())
    output << " " << sanitize_inline_text(std::string(query));
  output << "\n";
  if (!newest_matching_request.empty())
    output << "  /permissions audit show " << sanitize_inline_text(newest_matching_request) << "\n";
  else if (!newest_matching_entry.empty())
    output << "  /permissions audit show " << sanitize_inline_text(newest_matching_entry) << "\n";
  return output.str();
}

bool audit_row_matches_selector(PermissionAuditRow const& row, std::string_view selector)
{
  if (selector.empty())
    return false;
  return row.entry_id == selector || row.request_id == selector || row.entry_id.starts_with(selector) ||
         row.request_id.starts_with(selector);
}

void append_audit_detail_field(std::ostringstream& output, std::string_view label, std::string_view value)
{
  if (value.empty())
    return;
  output << "  " << label << ": " << sanitize_inline_text(std::string(value)) << "\n";
}

std::string format_permission_audit_detail(std::vector<ava::session::SessionEntry> const& entries,
                                           RuntimeSession const& session, std::string_view selector)
{
  std::ostringstream output;
  output << "Permission audit detail:\n";
  output << "  session: " << sanitize_inline_text(session.store.session_id()) << "\n";
  output << "  selector: " << sanitize_inline_text(std::string(selector)) << "\n";
  output << "  behavior: matching entry ids and permission request ids; oldest matching decision first\n";

  std::vector<PermissionAuditRow> rows;
  std::size_t audit_count = 0;
  for (auto const& entry : entries)
  {
    if (entry.type != ava::session::EntryType::PermissionDecision)
      continue;
    ++audit_count;
    auto row = permission_audit_row(entry, session);
    if (audit_row_matches_selector(row, selector))
      rows.push_back(std::move(row));
  }

  if (rows.empty())
  {
    output << "\nNo permission audit entries match \"" << sanitize_inline_text(std::string(selector)) << "\".";
    if (audit_count > 0)
      output << "\nUse /permissions audit [query] to find request or entry ids.";
    return output.str();
  }

  output << "  matched entries: " << rows.size() << " of " << audit_count << " audit entries\n\n";
  for (auto const& row : rows)
  {
    output << "- " << sanitize_inline_text(row.timestamp) << "\n";
    append_audit_detail_field(output, "entry", row.entry_id);
    append_audit_detail_field(output, "request", row.request_id);
    append_audit_detail_field(output, "operation", row.operation);
    append_audit_detail_field(output, "mode", row.mode);
    append_audit_detail_field(output, "tool", row.tool_name);
    append_audit_detail_field(output, "action", row.action);
    append_audit_detail_field(output, "resolution", row.resolution);
    append_audit_detail_field(output, "source", row.resolution_source);
    append_audit_detail_field(output, "risk", row.risk);
    append_audit_detail_field(output, "path", row.target_path);
    append_audit_detail_field(output, "command", row.command);
    append_audit_detail_field(output, "reason", row.reason);
    append_audit_detail_field(output, "resolution reason", row.resolution_reason);
    append_audit_detail_field(output, "rule", row.rule_id);
    append_audit_detail_field(output, "actor", row.actor);
  }

  output << "\nRelated commands:\n";
  output << "  /permissions audit " << sanitize_inline_text(std::string(selector)) << "\n";
  output << "  /permissions diagnose " << sanitize_inline_text(std::string(selector)) << "\n";
  output << "  /permissions audit export " << sanitize_inline_text(std::string(selector)) << "\n";
  for (auto const& row : rows)
  {
    if (!row.rule_id.empty())
      output << "  /permissions explain " << sanitize_inline_text(row.rule_id) << "\n";
  }
  return output.str();
}

bool permission_audit_row_is_denial(PermissionAuditRow const& row)
{
  return lower_ascii(row.action) == "deny" || lower_ascii(row.resolution) == "deny";
}

std::string permission_audit_row_target(PermissionAuditRow const& row)
{
  if (!row.command.empty())
    return "command=\"" + sanitize_inline_text(row.command) + "\"";
  if (!row.target_path.empty())
    return "path=" + sanitize_inline_text(row.target_path);
  if (!row.tool_name.empty())
    return "tool=" + sanitize_inline_text(row.tool_name);
  return "";
}

std::string permission_audit_row_decision(PermissionAuditRow const& row)
{
  if (!row.resolution.empty())
    return row.resolution;
  return row.action;
}

std::string format_permission_denial_diagnostics(
    std::vector<ava::session::SessionEntry> const& entries, RuntimeSession const& session,
    std::vector<ava::permissions::PersistentPermissionRule> const& rules, std::string_view query)
{
  static constexpr std::size_t kMaxDisplayedDenials = 10;

  std::ostringstream output;
  output << "Recent permission denials:\n";
  if (!query.empty())
    output << "  filter: " << sanitize_inline_text(std::string(query)) << "\n";

  std::size_t denial_count = 0;
  std::size_t matched_count = 0;
  std::vector<PermissionAuditRow> rows;
  for (auto entry = entries.rbegin(); entry != entries.rend(); ++entry)
  {
    if (entry->type != ava::session::EntryType::PermissionDecision)
      continue;

    auto row = permission_audit_row(*entry, session);
    if (!permission_audit_row_is_denial(row))
      continue;
    ++denial_count;

    auto const line = format_permission_audit_line(row);
    if (!audit_line_matches_query(line, entry->data_json, query))
      continue;
    ++matched_count;
    if (rows.size() < kMaxDisplayedDenials)
      rows.push_back(std::move(row));
  }

  if (denial_count == 0)
  {
    output << "\nNo denied permission decisions for this session.";
    return output.str();
  }
  if (matched_count == 0)
  {
    output << "\nNo denied permission decisions match \"" << sanitize_inline_text(std::string(query)) << "\".";
    return output.str();
  }

  output << "  showing: " << rows.size() << " of " << matched_count << " matching denials";
  if (matched_count != denial_count)
    output << " (" << denial_count << " total denials)";
  if (matched_count > rows.size())
    output << "; newest " << kMaxDisplayedDenials << " displayed";
  output << "\n\n";

  for (auto const& row : rows)
  {
    auto const decision = permission_audit_row_decision(row);
    auto const target = permission_audit_row_target(row);
    auto const* rule = [&rules, &row]() -> ava::permissions::PersistentPermissionRule const* {
      if (row.rule_id.empty())
        return nullptr;
      auto const found = std::ranges::find_if(rules, [&](auto const& candidate) { return candidate.rule_id == row.rule_id; });
      if (found == rules.end())
        return nullptr;
      return &*found;
    }();

    output << "- " << sanitize_inline_text(row.timestamp);
    if (!row.request_id.empty())
      output << " request=" << sanitize_inline_text(row.request_id);
    if (!decision.empty())
      output << " decision=" << sanitize_inline_text(decision);
    if (!row.resolution_source.empty())
      output << " source=" << sanitize_inline_text(row.resolution_source);
    if (!row.operation.empty())
      output << " operation=" << sanitize_inline_text(row.operation);
    if (!row.mode.empty())
      output << " mode=" << sanitize_inline_text(row.mode);
    if (!row.risk.empty())
      output << " risk=" << sanitize_inline_text(row.risk);
    if (!row.tool_name.empty())
      output << " tool=" << sanitize_inline_text(row.tool_name);
    if (!target.empty())
      output << " " << target;
    output << "\n";
    if (!row.reason.empty())
      output << "  reason: " << sanitize_inline_text(row.reason) << "\n";
    if (!row.resolution_reason.empty())
      output << "  resolution reason: " << sanitize_inline_text(row.resolution_reason) << "\n";
    if (!row.rule_id.empty())
    {
      output << "  rule: " << sanitize_inline_text(row.rule_id);
      if (rule)
      {
        output << " (" << ava::permissions::to_string(rule->action) << " "
               << ava::permissions::to_string(rule->operation) << " scope="
               << ava::permissions::to_string(rule->scope) << " mode=" << ava::permissions::to_string(rule->mode)
               << ")";
      }
      output << "\n";
      output << "  next: /permissions explain " << sanitize_inline_text(row.rule_id) << "\n";
    }
    else if (!row.request_id.empty())
    {
      output << "  next: /permissions audit " << sanitize_inline_text(row.request_id) << "\n";
    }
  }

  output << "\nUse /permissions audit";
  if (!query.empty())
    output << " " << sanitize_inline_text(std::string(query));
  output << " for raw audit rows.";
  return output.str();
}

std::string format_permission_rules_list(ava::permissions::PermissionRuleStore const& store,
                                         std::vector<ava::permissions::PersistentPermissionRule> const& rules,
                                         RuntimeSession const& session, std::string_view query)
{
  std::ostringstream output;
  output << "Permission rules:\n";
  output << "  global file: " << sanitize_inline_text(store.global_rules_file.generic_string()) << "\n";
  output << "  workspace file: "
         << sanitize_inline_text(
                ava::permissions::enforceable_permission_rules_file(store, ava::permissions::PermissionRuleScope::Workspace)
                    .generic_string())
         << "\n";
  output << "  behavior: built-in hard denies run first; matching deny rules override allow rules\n";

  bool wrote_any = false;
  for (auto const& rule : rules)
  {
    if (!rule_matches_query(rule, session, query))
      continue;
    if (!wrote_any)
    {
      if (!query.empty())
        output << "  filter: " << sanitize_inline_text(std::string(query)) << "\n";
      output << "\n";
      wrote_any = true;
    }
    output << format_rule_line(rule, session) << "\n";
  }

  if (!wrote_any)
  {
    output << "\n";
    if (query.empty())
      output << "No persistent permission rules.\n";
    else
      output << "No persistent permission rules match \"" << sanitize_inline_text(std::string(query)) << "\".\n";
  }

  output << "\nUse /permissions add ... to add a rule and /permissions remove <rule_id> to remove one.";
  return output.str();
}

std::string format_permission_rule_explain(ava::permissions::PermissionRuleStore const& store,
                                           ava::permissions::PersistentPermissionRule const& rule,
                                           RuntimeSession const& session)
{
  std::ostringstream output;
  output << "Permission rule " << sanitize_inline_text(rule.rule_id) << "\n";
  output << "  action: " << ava::permissions::to_string(rule.action) << "\n";
  output << "  operation: " << ava::permissions::to_string(rule.operation) << "\n";
  output << "  scope: " << ava::permissions::to_string(rule.scope) << "\n";
  output << "  mode: " << ava::permissions::to_string(rule.mode) << "\n";
  if (!rule.workspace_dir.empty())
    output << "  workspace: " << sanitize_inline_text(rule.workspace_dir.generic_string()) << "\n";
  if (!rule.target_path.empty())
    output << "  path: " << sanitize_inline_text(display_permission_path(rule.target_path, session.workspace_dir)) << "\n";
  if (!rule.command.empty())
    output << "  command: " << sanitize_inline_text(rule.command) << "\n";
  if (!rule.tool_name.empty())
    output << "  tool: " << sanitize_inline_text(rule.tool_name) << "\n";
  output << "  reason: " << sanitize_inline_text(rule.reason) << "\n";
  output << "  actor: " << sanitize_inline_text(rule.actor) << "\n";
  output << "  created: " << sanitize_inline_text(rule.created_at) << "\n";
  output << "  storage: " << sanitize_inline_text(permission_rule_storage_path(store, rule)) << "\n";
  output << "  matching: exact operation plus any non-empty path, command, tool, scope, and mode fields\n";
  output << "  precedence: built-in hard denies run first; matching deny rules override allow rules";
  return output.str();
}

ava::core::Result<std::vector<std::string>> parse_permission_command_tokens(std::string_view text)
{
  std::vector<std::string> tokens;
  std::size_t index = 0;
  while (index < text.size())
  {
    while (index < text.size() && std::isspace(static_cast<unsigned char>(text[index])) != 0) ++index;
    if (index >= text.size())
      break;

    std::string token;
    char quote = '\0';
    while (index < text.size())
    {
      char const ch = text[index++];
      if (quote != '\0')
      {
        if (ch == quote)
        {
          quote = '\0';
          continue;
        }
        if (ch == '\\' && index < text.size())
        {
          token.push_back(text[index++]);
          continue;
        }
        token.push_back(ch);
        continue;
      }

      if (ch == '"' || ch == '\'')
      {
        quote = ch;
        continue;
      }
      if (std::isspace(static_cast<unsigned char>(ch)) != 0)
        break;
      token.push_back(ch);
    }

    if (quote != '\0')
    {
      auto error = ava::core::Error(ava::core::ErrorCategory::InvalidArgument, "permission command has unterminated quoted value");
      return std::unexpected(std::move(error));
    }
    if (!token.empty())
      tokens.push_back(std::move(token));
  }
  return tokens;
}

std::string normalize_field_key(std::string_view key)
{
  auto normalized = lower_ascii(key);
  if (normalized == "target_path")
    return "path";
  if (normalized == "tool_name")
    return "tool";
  return normalized;
}

ava::core::Result<ParsedAddRule> parse_permission_add_rule(std::vector<std::string> const& tokens)
{
  std::unordered_map<std::string, std::string> fields;
  for (std::size_t index = 1; index < tokens.size(); ++index)
  {
    auto const separator = tokens[index].find('=');
    if (separator == std::string::npos || separator == 0)
    {
      auto error = ava::core::Error(ava::core::ErrorCategory::InvalidArgument, "permission rule add arguments must use key=value form");
      error.with_context("argument", tokens[index]);
      return std::unexpected(std::move(error));
    }

    auto key = normalize_field_key(std::string_view(tokens[index]).substr(0, separator));
    auto value = tokens[index].substr(separator + 1);
    if (key != "action" && key != "operation" && key != "scope" && key != "mode" && key != "path" &&
        key != "command" && key != "tool" && key != "reason")
    {
      auto error = ava::core::Error(ava::core::ErrorCategory::InvalidArgument, "permission rule add argument is unsupported");
      error.with_context("argument", key);
      return std::unexpected(std::move(error));
    }
    if (!fields.emplace(key, std::move(value)).second)
    {
      auto error = ava::core::Error(ava::core::ErrorCategory::InvalidArgument, "permission rule add argument is duplicated");
      error.with_context("argument", key);
      return std::unexpected(std::move(error));
    }
  }

  auto require_field = [&](std::string_view key) -> ava::core::Result<std::string> {
    auto const found = fields.find(std::string(key));
    if (found == fields.end() || found->second.empty())
    {
      auto error = ava::core::Error(ava::core::ErrorCategory::InvalidArgument, "permission rule add argument is required");
      error.with_context("argument", std::string(key));
      return std::unexpected(std::move(error));
    }
    return found->second;
  };

  auto action_text = require_field("action");
  if (!action_text)
    return std::unexpected(std::move(action_text.error()));
  auto action = ava::permissions::parse_permission_action(*action_text);
  if (!action || *action == ava::permissions::PermissionAction::Ask)
  {
    auto error = ava::core::Error(ava::core::ErrorCategory::InvalidArgument, "permission rule action must be allow or deny");
    error.with_context("action", *action_text);
    return std::unexpected(std::move(error));
  }

  auto operation_text = require_field("operation");
  if (!operation_text)
    return std::unexpected(std::move(operation_text.error()));
  auto operation = ava::permissions::parse_operation(*operation_text);
  if (!operation)
  {
    auto error = ava::core::Error(ava::core::ErrorCategory::InvalidArgument, "permission rule operation is unsupported");
    error.with_context("operation", *operation_text);
    return std::unexpected(std::move(error));
  }

  auto reason = require_field("reason");
  if (!reason)
    return std::unexpected(std::move(reason.error()));

  auto scope = ava::permissions::PermissionRuleScope::Workspace;
  if (auto const found = fields.find("scope"); found != fields.end() && !found->second.empty())
  {
    auto parsed = ava::permissions::parse_permission_rule_scope(found->second);
    if (!parsed)
    {
      auto error = ava::core::Error(ava::core::ErrorCategory::InvalidArgument, "permission rule scope must be workspace or global");
      error.with_context("scope", found->second);
      return std::unexpected(std::move(error));
    }
    scope = *parsed;
  }

  auto mode = ava::permissions::PermissionRuleMode::Any;
  if (auto const found = fields.find("mode"); found != fields.end() && !found->second.empty())
  {
    auto parsed = ava::permissions::parse_permission_rule_mode(found->second);
    if (!parsed)
    {
      auto error = ava::core::Error(ava::core::ErrorCategory::InvalidArgument, "permission rule mode must be any, build, or plan");
      error.with_context("mode", found->second);
      return std::unexpected(std::move(error));
    }
    mode = *parsed;
  }

  auto field_value = [&](std::string_view key) -> std::string {
    auto const found = fields.find(std::string(key));
    if (found == fields.end())
      return "";
    return found->second;
  };

  return ParsedAddRule{.draft = ava::permissions::PermissionRuleDraft{.scope = scope,
                                                                      .action = *action,
                                                                      .operation = *operation,
                                                                      .mode = mode,
                                                                      .tool_name = field_value("tool"),
                                                                      .target_path = field_value("path"),
                                                                      .command = field_value("command"),
                                                                      .reason = *reason,
                                                                      .actor = "tui"}};
}

ava::permissions::PersistentPermissionRule const* find_rule(
    std::vector<ava::permissions::PersistentPermissionRule> const& rules, std::string_view rule_id)
{
  auto const found = std::ranges::find_if(rules, [&](auto const& rule) { return rule.rule_id == rule_id; });
  if (found == rules.end())
    return nullptr;
  return &*found;
}

}  // namespace

ava::core::Result<CommandResult> run_permissions_command(RuntimeSession& session, CommandRequest const& request)
{
  CommandResult result;
  result.handled = true;

  auto const argument = command_argument(request.command, "/permissions");
  auto tokens = parse_permission_command_tokens(argument);
  if (!tokens)
  {
    add_output(result, tokens.error().format());
    return result;
  }
  if (tokens->empty())
    tokens->push_back("list");

  auto const subcommand = lower_ascii((*tokens)[0]);
  auto const store = permission_rule_store_for_session(session);

  if (subcommand == "list")
  {
    auto rules = ava::permissions::load_persistent_permission_rules(store);
    if (!rules)
    {
      add_output(result, rules.error().format());
      return result;
    }
    auto query = std::string{};
    if (argument.size() > (*tokens)[0].size())
      query = trim_ascii(argument.substr((*tokens)[0].size()));
    add_output(result, format_permission_rules_list(store, *rules, session, query));
    return result;
  }

  if (subcommand == "audit" || subcommand == "history")
  {
    auto entries = session.store.load();
    if (!entries)
    {
      add_output(result, "Permission audit failed closed:\n" + entries.error().format());
      return result;
    }
    auto audit_action = tokens->size() > 1 ? lower_ascii((*tokens)[1]) : std::string{};
    if (audit_action == "show" || audit_action == "inspect" || audit_action == "detail")
    {
      if (tokens->size() < 3)
      {
        add_output(result, missing_argument("/permissions audit show <entry_id|request_id>"));
        return result;
      }
      auto selector_tokens = std::vector<std::string>{};
      for (auto index = std::size_t{2}; index < tokens->size(); ++index)
      {
        selector_tokens.push_back((*tokens)[index]);
      }
      add_output(result, format_permission_audit_detail(*entries, session, joined_strings(selector_tokens, " ")));
      return result;
    }
    auto export_markdown = audit_action == "export";
    auto summarize = audit_action == "summary" || audit_action == "summarize" || audit_action == "stats";
    auto query_tokens = std::vector<std::string>{};
    auto const first_query_token = (export_markdown || summarize) ? std::size_t{2} : std::size_t{1};
    for (auto index = first_query_token; index < tokens->size(); ++index)
    {
      query_tokens.push_back((*tokens)[index]);
    }
    auto const query = joined_strings(query_tokens, " ");
    if (summarize)
      add_output(result, format_permission_audit_summary(*entries, session, query));
    else
      add_output(result, export_markdown ? format_permission_audit_export(*entries, session, query)
                                         : format_permission_audit(*entries, session, query));
    return result;
  }

  if (subcommand == "diagnose" || subcommand == "diagnostics")
  {
    auto rules = ava::permissions::load_persistent_permission_rules(store);
    if (!rules)
    {
      add_output(result, "Permission rule diagnostics failed closed:\n" + rules.error().format());
      return result;
    }
    auto entries = session.store.load();
    if (!entries)
    {
      add_output(result, "Permission audit diagnostics failed closed:\n" + entries.error().format());
      return result;
    }
    auto query_tokens = std::vector<std::string>{};
    for (auto index = std::size_t{1}; index < tokens->size(); ++index)
    {
      query_tokens.push_back((*tokens)[index]);
    }
    auto const query = joined_strings(query_tokens, " ");
    std::ostringstream output;
    output << "Permission rule diagnostics:\n";
    output << "  global file: " << sanitize_inline_text(store.global_rules_file.generic_string()) << "\n";
    output << "  workspace file: "
           << sanitize_inline_text(
                  ava::permissions::enforceable_permission_rules_file(store, ava::permissions::PermissionRuleScope::Workspace)
                      .generic_string())
           << "\n";
    output << "  loaded rules: " << rules->size() << "\n";
    output << "  hard-deny policy: evaluated before persistent rules\n";
    output << "  precedence: matching deny rules override allow rules\n";
    output << "  workspace storage: outside the model-writable workspace";
    output << "\n\n" << format_permission_denial_diagnostics(*entries, session, *rules, query);
    add_output(result, output.str());
    return result;
  }

  if (subcommand == "explain" || subcommand == "show")
  {
    if (tokens->size() != 2)
    {
      add_output(result, missing_argument("/permissions explain <rule_id>"));
      return result;
    }
    auto rules = ava::permissions::load_persistent_permission_rules(store);
    if (!rules)
    {
      add_output(result, rules.error().format());
      return result;
    }
    auto const* rule = find_rule(*rules, (*tokens)[1]);
    if (!rule)
    {
      add_output(result, "permission rule not found: " + sanitize_inline_text((*tokens)[1]));
      return result;
    }
    add_output(result, format_permission_rule_explain(store, *rule, session));
    return result;
  }

  if (subcommand == "add")
  {
    if (tokens->size() == 1)
    {
      add_output(result, permissions_usage());
      return result;
    }
    auto parsed = parse_permission_add_rule(*tokens);
    if (!parsed)
    {
      add_output(result, parsed.error().format() + "\n" + permissions_usage());
      return result;
    }
    auto added = ava::permissions::add_persistent_permission_rule(store, std::move(parsed->draft));
    if (!added)
    {
      add_output(result, added.error().format());
      return result;
    }
    add_output(result, "added permission rule " + sanitize_inline_text(added->rule_id) + "\n" +
                           format_rule_line(*added, session));
    return result;
  }

  if (subcommand == "remove" || subcommand == "delete")
  {
    if (tokens->size() != 2)
    {
      add_output(result, missing_argument("/permissions remove <rule_id>"));
      return result;
    }
    auto removed = ava::permissions::remove_persistent_permission_rule(store, (*tokens)[1]);
    if (!removed)
    {
      add_output(result, removed.error().format());
      return result;
    }
    add_output(result, "removed permission rule " + sanitize_inline_text(removed->rule_id) + "\n" +
                           format_rule_line(*removed, session));
    return result;
  }

  add_output(result, permissions_usage());
  return result;
}

}  // namespace ava::app
