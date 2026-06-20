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
         "  /permissions diagnose\n"
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

std::string format_permission_audit_line(ava::session::SessionEntry const& entry, RuntimeSession const& session)
{
  auto const request_id = audit_field(entry.data_json, "permission_request_id");
  auto const operation = audit_field(entry.data_json, "operation");
  auto const mode = audit_field(entry.data_json, "mode");
  auto const tool_name = audit_field(entry.data_json, "tool_name");
  auto const action = audit_field(entry.data_json, "action");
  auto const reason = audit_field(entry.data_json, "reason");
  auto const risk = audit_field(entry.data_json, "risk");
  auto const target_path = audit_field(entry.data_json, "target_path");
  auto const command = audit_field(entry.data_json, "command");
  auto const resolution = audit_field(entry.data_json, "resolution");
  auto const resolution_source = audit_field(entry.data_json, "resolution_source");
  auto const resolution_reason = audit_field(entry.data_json, "resolution_reason");
  auto const actor = audit_field(entry.data_json, "actor");
  auto const rule_id = audit_field(entry.data_json, "rule_id");

  std::string line = "- " + sanitize_inline_text(entry.timestamp);
  append_audit_field(line, "entry", entry.id);
  append_audit_field(line, "request", request_id);
  append_audit_field(line, "action", action);
  append_audit_field(line, "resolution", resolution);
  append_audit_field(line, "source", resolution_source);
  append_audit_field(line, "operation", operation);
  append_audit_field(line, "mode", mode);
  append_audit_field(line, "risk", risk);
  append_audit_field(line, "tool", tool_name);
  if (!target_path.empty())
    append_audit_field(line, "path", display_permission_path(target_path, session.workspace_dir));
  append_audit_quoted_field(line, "command", command);
  append_audit_quoted_field(line, "reason", reason);
  append_audit_quoted_field(line, "resolution_reason", resolution_reason);
  append_audit_field(line, "rule", rule_id);
  append_audit_field(line, "actor", actor);
  return line;
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

    auto const line = format_permission_audit_line(*entry, session);
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
    auto query = std::string{};
    if (argument.size() > (*tokens)[0].size())
      query = trim_ascii(argument.substr((*tokens)[0].size()));
    add_output(result, format_permission_audit(*entries, session, query));
    return result;
  }

  if (subcommand == "diagnose" || subcommand == "diagnostics")
  {
    if (tokens->size() != 1)
    {
      add_output(result, permissions_usage());
      return result;
    }
    auto rules = ava::permissions::load_persistent_permission_rules(store);
    if (!rules)
    {
      add_output(result, "Permission rule diagnostics failed closed:\n" + rules.error().format());
      return result;
    }
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
