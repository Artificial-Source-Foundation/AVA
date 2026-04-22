---
title: "Reference"
description: "Reference docs for AVA runtime surfaces, configuration, commands, providers, and credentials."
order: 1
updated: "2026-04-21"
---

# Reference Docs

This section holds factual reference material that supports the rest of the docs tree.

Use reference pages when you want stable facts, command surfaces, config rules, or file/layout details. For step-by-step tasks, use the how-to guides instead.

## Choose by question

| I need to know... | Start here |
|---|---|
| Which config files and precedence rules AVA uses | [Configuration](configuration.md) |
| Which environment variables matter | [Environment variables](environment-variables.md) |
| Where AVA stores files locally | [Filesystem layout](filesystem-layout.md) |
| Which install artifacts and release paths are official | [Install and release paths](install-and-release-paths.md) |
| Which providers and auth flows are supported | [Providers and auth](providers-and-auth.md) |
| Which commands and flags exist | [Commands](commands.md) |
| How primary-agent and subagent profiles are configured | [How-to: Configure primary agents and subagents](../how-to/agents.md) |
| How credentials are stored | [Credential storage](credential-storage.md) |
| How the feature-gated `ava serve` backend works (advanced) | [Web API surface](web-api.md) |

## Documents

1. [Reference overview](overview.md) - scope, stability notes, and source-of-truth links
2. [Configuration](configuration.md) - config loading paths, precedence, and core schema sections
3. [Environment variables](environment-variables.md) - runtime env vars that affect provider selection, auth, and runtime behavior
4. [Filesystem layout](filesystem-layout.md) - user-global and project-local AVA files and directories
5. [Install and release paths](install-and-release-paths.md) - current CLI binary/source install and release automation map
6. [Providers and auth](providers-and-auth.md) - provider IDs, aliases, auth flows, and env vars
7. [Commands](commands.md) - slash commands, CLI subcommands, and key flags
8. [How-to: Configure primary agents and subagents](../how-to/agents.md) - advanced setup for startup and delegated agent profiles
9. [Credential storage](credential-storage.md) - where credentials live and the recommended security posture
10. [Web API surface](web-api.md) - advanced implementation reference for `ava serve`

## Use This Section For

1. Capability reference material that explains how AVA works
2. Reusable setup and configuration details
3. Docs that support implementation without acting as roadmap or architecture guidance

## Public Vs Internal Reference

User-facing reference:

1. configuration
2. environment variables
3. filesystem layout
4. install and release paths
5. providers and auth
6. commands
7. credential storage

Implementation reference:

1. `web-api.md` - advanced, feature-gated `ava serve` backend surface for local web development and integration work

For task-driven CI/unattended execution steps, use [How-to: Run AVA in CI/headless automation](../how-to/ci-headless-automation.md).

For contributor build, test, and repo-workflow commands, use [Contributing Docs](../contributing/README.md) and [Testing](../testing/README.md).
