---
title: "Plugins"
description: "Power plugins, host seams, and SDK entrypoints for advanced AVA capability."
order: 2
updated: "2026-04-08"
---

# Plugins

Power plugins are AVA's installable extension path for advanced capability.

## What Plugins Are For

Use a plugin when you need one or more of these:

1. Hooking into agent or tool lifecycle events
2. Registering namespaced commands, routes, or events
3. Surfacing plugin-owned UI mounts
4. Shipping capability that should not live in the default core product

## Current Host Seam

Plugins are standalone processes that communicate with AVA over JSON-RPC via stdio.

Current host seam entry points:

1. Desktop command dispatch via `plugin_host_invoke`
2. Web command dispatch via `/api/plugins/{plugin}/commands/{command}`
3. Web route proxying via `/api/plugins/{plugin}/routes/{*route}`
4. Mount discovery via `list_plugin_mounts` and `/api/plugins/mounts`
5. Read-only mount metadata surfaced in the Plugins settings detail panel

Plugins remain namespaced under their own IDs. They do not register arbitrary top-level app commands or routes.

## Capability Types

Supported v1 capability types:

1. commands
2. routes
3. events
4. mounts

## SDK And Implementation

1. TypeScript SDK: [`plugins/sdk/README.md`](../../plugins/sdk/README.md)
2. Python SDK: `plugins/sdk-python/`
3. Example plugins: `plugins/examples/`
4. Core runtime: `crates/ava-plugin/`
5. HQ migration example: `plugins/examples/ava-hq/` backed by `crates/ava-hq/src/bin/ava-hq-plugin.rs`

## Product Direction

The plugin seam is intentionally narrow and namespaced. It is the path for moving large optional systems, such as HQ, out of core.

For the active migration checklist, see [`../architecture/plugin-boundary.md`](../architecture/plugin-boundary.md).
