---
title: "Commands"
description: "Reference for AVA's slash commands, CLI subcommands, and important runtime flags."
order: 3
updated: "2026-04-21"
---

# Commands

This page documents the user-facing command surfaces that exist today in AVA.

## Slash Commands

Primary slash commands exposed in the TUI and slash-aware headless paths:

1. `/model [provider/model]` - show or switch model
2. `/think [show|hide]` - toggle thinking block visibility
3. `/theme [name]` - cycle or switch theme
4. `/permissions [list]` - toggle permission level or list glob rules
5. `/connect [provider]` - add provider credentials
6. `/providers` - show provider status
7. `/disconnect <provider>` - remove provider credentials
8. `/mcp [list]` - show MCP servers
9. `/mcp reload` - reload MCP config
10. `/mcp enable <name>` - enable a disabled MCP server
11. `/mcp disable <name>` - disable an MCP server for the session
12. `/skills` or `/skills list` - list live filesystem-discovered runtime skills
13. `/new [title]` - start a new session
14. `/sessions` - open the session picker
15. `/bookmark [label]` - add a bookmark
16. `/bookmark list` - list bookmarks
17. `/bookmark clear` - clear bookmarks
18. `/bookmark remove <id-prefix>` - remove one bookmark
19. `/plan [view]` - show plan state or open it in the browser
20. `/review` - run code review on working tree changes
21. `/commit` - inspect commit readiness and suggest a message
22. `/export [filename]` - export conversation to `.md` or `.json`
23. `/copy [all]` - copy the last response
24. `/plugin` or `/plugins` - list installed plugins
25. `/hooks` or `/hooks list` - list loaded hooks
26. `/hooks reload` - reload hooks from disk
27. `/hooks dry-run <event> [tool_name]` - simulate matching hook execution
28. `/init` - create starter repo-local AVA files
29. `/btw [question]` - open a side branch conversation
30. `/btw end` - return from the side branch
31. `/tasks` - show background tasks
32. `/later <message>` - queue a post-complete message
33. `/queue` - show queued messages
34. `/shortcuts`, `/keys`, or `/keybinds` - show keyboard shortcuts
35. `/clear` - clear chat
36. `/compact [focus]` - compact the conversation
37. `/help` - show command help

## Headless Notes

1. `/help` has a lightweight headless implementation and does not need a full `AgentStack`.
2. `/skills` lists the current runtime-visible `SKILL.md` files using the same trust-gated filesystem discovery logic as prompt assembly.
3. Other slash commands fall through to the normal app command machinery in headless mode.
4. Some TUI-oriented commands still depend on async or modal context and are best used in the interactive app.

## CLI Subcommands

Top-level CLI subcommands:

1. `ava review` - review code changes using an LLM agent
2. `ava auth <login|logout|list|test>` - manage provider authentication
3. `ava plugin <list|add|remove|info|init>` - manage power plugins
4. `ava update` and `ava self-update` - check for and install updates
5. `ava serve --host <host> --port <port> [--token <token>] [--insecure-open-cors]` - run the web server

`ava serve` is only available in builds compiled with the `web` feature. Default `ava-tui` builds use `default = []` in `crates/ava-tui/Cargo.toml`.

Current serve defaults and hardening:

1. Default bind host is `127.0.0.1` (not `0.0.0.0`).
2. If `--token` is omitted, AVA generates a control token at startup; the raw value is shown only on the live terminal and redacted from normal logs.
3. Sensitive HTTP control/session routes require `Authorization: Bearer <token>` (or `x-ava-token: <token>`).
4. WebSocket clients must connect with `ws://.../ws?token=<token>`; `access_token` is also accepted as a query alias.
5. Browser origins are loopback-only by default (`localhost`, `127.0.0.1`, `[::1]`). `--insecure-open-cors` re-opens browser origin access and should only be used deliberately for trusted local development setups.

## Important CLI Flags

1. `--headless` - force non-TUI execution
2. `--trust` - trust the current project and allow project-local config loading
3. `--fast` - skip project instructions and eager codebase indexing
4. `--json` - emit JSON events for scripting
5. `--review` - force a post-run code review pass; in headless mode this can also trigger an automatic follow-up fix pass when the review finds issues after a successful run
6. `--acp-server` - run as an ACP server on stdio
7. `--provider` and `--model` - override routing for a run
8. `--agent <id>` - choose a startup primary-agent profile from `primary_agents.<id>` in `config.yaml`
9. `--continue` / `--session <id>` - resume the previous or specific session; restored session primary-agent metadata applies unless `--agent` is explicitly provided

For startup/delegation profile setup and trust/compatibility details, see [How-to: Configure primary agents and subagents](../how-to/agents.md).

## Related

1. [Scripts and build commands](../contributing/scripts-and-build.md)
2. [Web API surface](web-api.md)
3. [Configuration](configuration.md)
4. [How-to: Run AVA in CI/headless automation](../how-to/ci-headless-automation.md)
