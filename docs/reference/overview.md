---
title: "Reference Overview"
description: "What the AVA reference section covers and how to interpret stability boundaries."
order: 2
updated: "2026-04-21"
---

# Reference Overview

This section documents AVA's current operational surfaces.

It is intentionally factual.

## Stability Scope

Use these pages as **implementation reference**, not as a long-term API guarantee, unless a page explicitly says otherwise.

1. CLI flags and slash commands are user-facing and comparatively stable, but still evolve.
2. Web routes under `crates/ava-web/src/` exist and are used today, but are primarily the current frontend/backend integration seam.
3. Some routes are debug-only or stubbed; those are called out explicitly in [Web API surface](web-api.md).

## Related Reference Pages

1. [Configuration](configuration.md)
2. [Environment variables](environment-variables.md)
3. [Filesystem layout](filesystem-layout.md)
4. [Install and release paths](install-and-release-paths.md)
5. [Web API surface](web-api.md)
6. [Providers and auth](providers-and-auth.md)
7. [Commands](commands.md)
8. [Credential storage](credential-storage.md)
9. [How-to: Configure primary agents and subagents](../how-to/agents.md)
