---
title: "How-to: Configure AVA"
description: "Add provider credentials and understand the local configuration files AVA uses."
order: 2
updated: "2026-04-21"
---

# How-to: Configure AVA

Use this page when you need to connect a model provider and understand the few local settings most users actually need.

See also: [Reference: Providers and auth](../reference/providers-and-auth.md), [Reference: Credential storage](../reference/credential-storage.md), [How-to: Use local models with Ollama](ollama-local-models.md)

For startup/delegation profile setup, use [How-to: Configure primary agents and subagents](agents.md). That is advanced setup, not required for normal AVA use.

## Choose What You Need

| I want to... | Start here |
|---|---|
| Add a provider login | [Add credentials](#add-credentials) |
| See what AVA currently recognizes | [Check the configured providers](#check-the-configured-providers) |
| Understand the main config files AVA uses | [Know where AVA reads configuration](#know-where-ava-reads-configuration) |
| Prefer environment variables | [Use environment variables when needed](#use-environment-variables-when-needed) |
| Understand project-local trust behavior | [Trust project-local configuration carefully](#trust-project-local-configuration-carefully) |

## Add credentials

Log in to a provider:

```bash
ava auth login openrouter
```

Other provider IDs are listed in [`../reference/providers-and-auth.md`](../reference/providers-and-auth.md).

## Check the configured providers

```bash
ava auth list
```

`ava auth list` shows the resolved provider status view and can reflect env overrides. It is still a configuration check, not a full runtime verification.

You can also test one provider directly:

```bash
ava auth test openrouter
```

`ava auth test <provider>` is also a local configuration check. For end-to-end verification, run a real one-turn prompt against that provider and model.

## Know where AVA reads configuration

Canonical user-global config root is `$XDG_CONFIG_HOME/ava` (typically `~/.config/ava`).

Legacy `~/.ava` is still read for compatibility on existing installs.

The main files most users care about under that user-global root are:

```text
<config-root>/
├── credentials.json
├── config.yaml
├── mcp.json
├── tools/
├── themes/
└── AGENTS.md
```

Use [Reference: Configuration](../reference/configuration.md) only if you need the exact precedence rules and full path details.

## Use environment variables when needed

AVA checks provider-specific environment variables before falling back to `<config-root>/credentials.json` (with `~/.ava/credentials.json` compatibility). The lookup order is documented in [Reference: Providers and auth](../reference/providers-and-auth.md).

## Trust project-local configuration carefully

The `--trust` flag enables project-local surfaces such as `.ava/mcp.json`, hooks, tools, commands, skills, rules, and project instruction files. Most users can ignore this until they want repo-local customization.

## Verify the full setup

Use the auth commands above as local configuration checks.

For end-to-end verification, run one real prompt against the provider and model you plan to use, for example:

```bash
ava --provider openrouter --model anthropic/claude-sonnet-4 --headless --max-turns 1 "Reply with OK"
```
