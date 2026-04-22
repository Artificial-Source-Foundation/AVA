---
title: "Extend"
description: "Overview of AVA's plugins, MCP, commands, hooks, tools, and instruction surfaces."
order: 1
updated: "2026-04-08"
---

# Extend AVA

AVA keeps its core surface intentionally small and pushes advanced customization into explicit extension paths.

## Extension Model

The default AVA 0.6 direction is:

1. `MCP` for external tool integration
2. `Commands`, `Rules`, and `Skills` for user-facing customization
3. Power plugins for advanced installable capability
4. Custom tools for lightweight project or user-local automation

## Pages

1. [Plugins](plugins.md) - power plugins, host seams, and SDK entrypoints
2. [MCP servers](mcp-servers.md) - external tool servers and configuration
3. [Custom tools](custom-tools.md) - TOML-defined shell and script tools
4. [Commands and hooks](commands-and-hooks.md) - repo-local slash commands and lifecycle automation
5. [Instructions and skills](instructions-and-skills.md) - repo instructions, modular rules, includes, and skill discovery

## How To Choose

1. Use `instructions` and `skills` to shape how AVA behaves in a repo.
2. Use `commands and hooks` when you need lightweight repo-local workflow automation.
3. Use `custom tools` when you need small local automations without building a plugin.
4. Use `MCP` when the capability already exists as an external tool server.
5. Use `plugins` when you need namespaced app features, routes, commands, or UI mounts.

These are advanced paths. Normal AVA use should not require touching them.

## Related Docs

1. [`plugins/sdk/README.md`](../../plugins/sdk/README.md) - TypeScript plugin SDK quick start
2. [`docs/architecture/plugin-boundary.md`](../architecture/plugin-boundary.md) - current plugin-boundary execution checklist
3. [`docs/project/roadmap.md`](../project/roadmap.md) - product direction for AVA's extension model
