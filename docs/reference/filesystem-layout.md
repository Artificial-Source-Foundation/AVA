---
title: "Filesystem Layout"
description: "Current AVA file and directory layout across user-global and project-local state."
order: 5
updated: "2026-04-21"
---

# Filesystem Layout

This page summarizes filesystem locations that AVA currently reads or writes.

## User-Global Paths (`$XDG_CONFIG_HOME/ava` canonical, `~/.ava` compatibility)

Canonical user-global root: `$XDG_CONFIG_HOME/ava` (typically `~/.config/ava`).
Use this for active configuration.

Legacy root: `~/.ava` remains compatibility input for existing installs while migrating.

Common paths referenced in current runtime code (shown here with legacy `~/.ava` examples for compatibility clarity):

1. `~/.ava/credentials.json`
   - Default credential-store path (`crates/ava-config/src/credentials.rs`)
2. `~/.ava/config.yaml`
   - One of the current user-global config paths (`crates/ava-tui/src/config/cli.rs`)
3. `~/.ava/mcp.json`
   - User-global MCP configuration path surfaced by the repo docs and runtime loading paths
4. `~/.ava/subagents.toml`
   - Preferred user-global delegated subagent configuration
5. `~/.ava/hooks/`
   - User-global hook loading path (`crates/ava-tui/src/hooks/config.rs`)
6. `~/.ava/tools/`
   - User-global custom tool directory referenced by runtime instruction and tool-loading code
7. `~/.ava/AGENTS.md`
   - User-global instruction file path referenced by instruction-loading logic
8. `~/.ava/trusted_projects.json`
   - Trusted project state (`crates/ava-config/src/trust.rs`)
9. `~/.ava/logs/`
   - Runtime/frontend/crash log directory (`crates/ava-tui/src/main.rs`, `crates/ava-web/src/api_config.rs`, `crates/ava-web/src/lib.rs`)
10. `~/.ava/log/`
   - Session JSONL log directory (`crates/ava-agent/src/session_logger.rs`)
11. `~/.ava/permissions.toml`
   - Persistent permission approvals (`crates/ava-tools/src/permission_middleware.rs`, related command help in `crates/ava-tui/src/app/commands.rs`)
12. `~/.ava/acp-sessions.json`
   - ACP session store default (`crates/ava-acp/src/session_store.rs`)

## Project-Local (`<repo>/.ava`) Paths

Project-scoped files and directories referenced in command/runtime code:

1. `.ava/state.json` (project model history; `crates/ava-config/src/lib.rs`)
2. `.ava/mcp.json` (`/init` command scaffold and trusted-project loading, `crates/ava-tui/src/app/commands.rs`, `crates/ava-tui/src/config/cli.rs`)
3. `.ava/tools/` (`/init` creates tool template)
4. `.ava/commands/` (trusted-project command loading path listed in CLI trust documentation)
5. `.ava/hooks/` (trusted-project hook loading path listed in CLI trust documentation)
6. `.ava/skills/` (trusted-project skill loading path listed in CLI trust documentation)
7. `.ava/rules/` (trusted-project rules loading path listed in CLI trust documentation)
8. `.ava/subagents.toml` (trusted-project delegated subagent config)
9. `.ava/permissions.toml` (project-level permission rules; surfaced in command help)
10. `.ava/plans/` (plan persistence/read path surfaced in tool and web route docs)
11. `.ava/AGENTS.md` (trusted project-local instruction file path)
12. `AGENTS.md` at the repository root (trusted project instruction file path loaded alongside `.ava/` instruction surfaces)

## Path Guidance

1. Prefer `$XDG_CONFIG_HOME/ava/*` for all user-global config files.
2. Keep `~/.ava/*` only as migration/compatibility input.
3. For delegated subagents, use `subagents.toml` (not `agents.toml`).

## Agent Config Notes

1. Primary-agent startup profiles live in `config.yaml` (`primary_agent`, `primary_agents`).
2. Delegated subagent profiles live in `subagents.toml` (global + trusted project scopes).
3. Subagent profile prompts can be inline (`prompt`) or loaded from external files (`prompt_file`).

For examples, see [How-to: Configure primary agents and subagents](../how-to/agents.md).

## Related

1. [Configuration](configuration.md)
2. [Credential storage](credential-storage.md)
3. [Web API surface](web-api.md)
