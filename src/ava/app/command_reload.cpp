#include "sys.h"
#include "ava/app/command_format.h"
#include "ava/app/command_reload.h"
#include "ava/app/commands.h"
#include "ava/app/display_settings.h"
#include "ava/app/project_trust.h"
#include "ava/app/runtime.h"
#include "ava/app/runtime/Session.h"
#include "ava/app/runtime_prompt.h"
#include "ava/config/model_config.h"
#include "ava/session/compaction.h"

#include <filesystem>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace ava::app {
namespace {

struct ReloadReportRow
{
  std::string name;
  std::string status;
  std::vector<std::pair<std::string, std::string>> details;

  AVA_DEBUG_PRINT_MEMBERS_OPT_OUT
};

std::string reload_supported_targets()
{
  return "all, theme, models, prompts, trust, compaction, keybindings, auth, permissions, lsp, mcp, plugins";
}

void append_reload_detail(ReloadReportRow& row, std::string key, std::string value)
{
  row.details.push_back({std::move(key), sanitize_inline_text(std::move(value))});
}

ReloadReportRow reload_error_row(std::string name, ava::core::Error const& error)
{
  ReloadReportRow row{.name = std::move(name), .status = "error", .details = {}};
  append_reload_detail(row, "error", error.format());
  return row;
}

std::string format_reload_report(std::vector<ReloadReportRow> const& rows)
{
  std::string output = "Reload report:";
  for (auto const& row : rows)
  {
    output += "\n  " + row.name + ": " + row.status;
    for (auto const& detail : row.details) output += "\n    " + detail.first + ": " + detail.second;
  }
  return output;
}

std::string normalize_reload_target(std::string_view target)
{
  if (target.empty() || target == "all")
    return "all";
  if (target == "theme" || target == "themes" || target == "display")
    return "display";
  if (target == "model" || target == "models")
    return "models";
  if (target == "prompt" || target == "prompts" || target == "context" || target == "contexts")
    return "prompts";
  if (target == "trust" || target == "project" || target == "projects")
    return "trust";
  if (target == "compact" || target == "compaction")
    return "compaction";
  if (target == "keybindings" || target == "keybinds" || target == "keys")
    return "keybindings";
  if (target == "auth" || target == "credentials")
    return "auth";
  if (target == "permission" || target == "permissions")
    return "permissions";
  if (target == "lsp" || target == "language-server" || target == "language-servers")
    return "lsp";
  if (target == "mcp")
    return "mcp";
  if (target == "plugin" || target == "plugins")
    return "plugins";
  return {};
}

ReloadReportRow reload_display_settings(runtime::Session& session)
{
  auto settings = apply_tui_display_settings(session.paths());
  if (!settings)
    return reload_error_row("display", settings.error());
  ReloadReportRow row{.name = "display", .status = "loaded", .details = {}};
  append_reload_detail(row, "config", settings->path.string());
  append_reload_detail(row, "configured", settings->theme ? *settings->theme : std::string("built-in default"));
  append_reload_detail(row, "active", active_tui_theme_summary());
  return row;
}

ReloadReportRow reload_model_settings(runtime::Session& session)
{
  auto registry = ava::config::load_model_registry(session.paths());
  if (!registry)
    return reload_error_row("models", registry.error());
  session.model_selection().scoped_model_cycle = registry->scoped_model_cycle;
  if (auto refreshed = session.refresh_parent_configuration(); !refreshed)
    return reload_error_row("models", refreshed.error());
  ReloadReportRow row{.name = "models", .status = "loaded", .details = {}};
  append_reload_detail(row, "config", session.paths().models_file.string());
  append_reload_detail(row, "models", std::to_string(registry->models.size()));
  append_reload_detail(row, "scoped_cycle", session.scoped_model_cycle() ? "configured" : "not configured");
  append_reload_detail(row, "active_model", session.model().provider_id + "/" + session.model().model_id + " (unchanged)");
  return row;
}

ReloadReportRow reload_prompt_settings(runtime::Session& session)
{
  auto prompt_state = runtime::load_runtime_prompt_state(session.paths(), session.model(), session.mode(), session.workspace_dir(), session.current_dir(),
                                                         project_resources_trusted(session.project_trust()), session.prompt_overrides());
  if (!prompt_state)
    return reload_error_row("prompts", prompt_state.error());
  if (auto refreshed = session.apply_prompt_state(std::move(*prompt_state)); !refreshed)
    return reload_error_row("prompts", refreshed.error());
  ReloadReportRow row{.name = "prompts", .status = "loaded", .details = {}};
  append_reload_detail(row, "project_resources", project_resources_trusted(session.project_trust()) ? "enabled" : "skipped");
  append_reload_detail(row, "context_sources", std::to_string(session.context_sources().size()));
  append_reload_detail(row, "freshness_sources", std::to_string(session.freshness_sources().size()));
  append_reload_detail(row, "base_prompt",
                       session.base_prompt().from_override ? std::string("override")
                       : session.base_prompt().source_path ? session.base_prompt().source_path->string()
                                                           : std::string("built-in"));
  return row;
}

ReloadReportRow reload_trust_settings(runtime::Session& session)
{
  auto next_trust = load_project_trust_state(session.paths(), session.workspace_dir());
  auto prompt_state = runtime::load_runtime_prompt_state(session.paths(), session.model(), session.mode(), session.workspace_dir(), session.current_dir(),
                                                         project_resources_trusted(next_trust), session.prompt_overrides());
  if (!prompt_state)
  {
    auto row = reload_error_row("trust", prompt_state.error());
    append_reload_detail(row, "trust_file", next_trust.trust_file.string());
    return row;
  }
  session.trust_state().project_trust = std::move(next_trust);
  if (auto refreshed = session.apply_prompt_state(std::move(*prompt_state)); !refreshed)
    return reload_error_row("trust", refreshed.error());
  ReloadReportRow row{.name = "trust", .status = "loaded", .details = {}};
  append_reload_detail(row, "trust_file", session.project_trust().trust_file.string());
  append_reload_detail(row, "decision", std::string(to_string(session.project_trust().decision)));
  append_reload_detail(row, "project_resources", project_resources_trusted(session.project_trust()) ? "enabled" : "skipped");
  if (!session.project_trust().diagnostic.empty())
    append_reload_detail(row, "diagnostic", session.project_trust().diagnostic);
  return row;
}

ReloadReportRow reload_compaction_settings(runtime::Session& session)
{
  auto loaded_config = ava::session::load_compaction_config(session.paths());
  if (!loaded_config)
    return reload_error_row("compaction", loaded_config.error());
  auto config = resolve_compaction_config(session, std::move(*loaded_config));
  if (!config)
    return reload_error_row("compaction", config.error());
  ReloadReportRow row{.name = "compaction", .status = "validated", .details = {}};
  append_reload_detail(row, "config", session.paths().compaction_file.string());
  append_reload_detail(row, "provider", config->provider_id);
  append_reload_detail(row, "model", config->model_id);
  append_reload_detail(row, "auto_threshold_tokens", std::to_string(config->auto_threshold_tokens));
  append_reload_detail(row, "auto_threshold_percent", std::to_string(config->auto_threshold_percent));
  append_reload_detail(row, "effective_threshold_tokens",
                       std::to_string(ava::session::effective_auto_threshold_tokens(*config, session.model().context_window_tokens)));
  append_reload_detail(row, "keep_recent_tokens", std::to_string(config->keep_recent_tokens));
  append_reload_detail(row, "keep_recent_turns", std::to_string(config->keep_recent_turns));
  append_reload_detail(row, "keep_recent_messages", std::to_string(config->keep_recent_messages));
  append_reload_detail(row, "max_summary_bytes", std::to_string(config->max_summary_bytes));
  return row;
}

ReloadReportRow keybindings_reload_row(runtime::Session const& session)
{
  ReloadReportRow row{.name = "keybindings", .status = "tui-runtime", .details = {}};
  append_reload_detail(row, "config", (session.paths().ava_config_dir / "keybinds.json").string());
  append_reload_detail(row, "note", "interactive TUI reloads keybindings live; restart non-TTY sessions after edits");
  return row;
}

ReloadReportRow restart_required_reload_row(std::string name, std::string reason, std::vector<std::pair<std::string, std::filesystem::path>> paths)
{
  ReloadReportRow row{.name = std::move(name), .status = "restart-required", .details = {}};
  append_reload_detail(row, "reason", std::move(reason));
  for (auto const& path : paths) append_reload_detail(row, path.first, path.second.string());
  return row;
}

std::vector<ReloadReportRow> reload_report_rows_for_target(runtime::Session& session, std::string const& target)
{
  auto one = [&](std::string const& normalized) -> ReloadReportRow {
    if (normalized == "display")
      return reload_display_settings(session);
    if (normalized == "models")
      return reload_model_settings(session);
    if (normalized == "trust")
      return reload_trust_settings(session);
    if (normalized == "prompts")
      return reload_prompt_settings(session);
    if (normalized == "compaction")
      return reload_compaction_settings(session);
    if (normalized == "keybindings")
      return keybindings_reload_row(session);
    if (normalized == "auth")
    {
      return restart_required_reload_row("auth", "active provider credentials are resolved when a run starts", {{"config", session.paths().auth_file}});
    }
    if (normalized == "permissions")
    {
      return restart_required_reload_row(
          "permissions", "active permission policy and session grants are not hot-reloaded",
          {{"global", session.paths().ava_config_dir / "permission-rules.json"}, {"project", session.workspace_dir() / ".ava" / "permission-rules.json"}});
    }
    if (normalized == "lsp")
    {
      return restart_required_reload_row("lsp", "language-server clients are created for tool calls and should restart with config changes",
                                         {{"global", session.paths().ava_config_dir / "lsp.json"}, {"project", session.workspace_dir() / ".ava" / "lsp.json"}});
    }
    if (normalized == "mcp")
    {
      return restart_required_reload_row("mcp", "running MCP server processes are not restarted by /reload",
                                         {{"global", session.paths().ava_config_dir / "mcp.json"}, {"project", session.workspace_dir() / ".ava" / "mcp.json"}});
    }
    return restart_required_reload_row("plugins", "plugin discovery and process state are not hot-reloaded",
                                       {{"global", session.paths().ava_config_dir / "plugins"},
                                        {"project", session.workspace_dir() / ".ava" / "plugins"},
                                        {"state", session.paths().ava_state_dir / "plugin-enablement.json"}});
  };

  if (target != "all")
    return {one(target)};
  return {one("display"), one("models"),      one("trust"), one("prompts"), one("compaction"), one("keybindings"),
          one("auth"),    one("permissions"), one("lsp"),   one("mcp"),     one("plugins")};
}

}  // namespace

ava::core::Result<CommandResult> run_reload_command(runtime::session_ts& unlocked_session, std::string_view argument)
{
  auto const args = split_command_arguments(argument);
  if (args.size() > 1)
    return handled_text("unsupported reload target: " + std::string(argument) + "\nsupported: " + reload_supported_targets());

  auto const raw_target = args.empty() ? std::string{} : args.front();
  auto const target = normalize_reload_target(raw_target);
  if (target.empty())
    return handled_text("unsupported reload target: " + raw_target + "\nsupported: " + reload_supported_targets());
  return handled_text(format_reload_report(reload_report_rows_for_target(session, target)));
}

}  // namespace ava::app
