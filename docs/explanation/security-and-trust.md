---
title: "Explanation: Security And Trust"
description: "How AVA handles trust, local credentials, permissions, sandboxing, and logs."
order: 2
updated: "2026-04-18"
---

# Explanation: Security And Trust

AVA works in real repositories, so its security model is centered on local trust boundaries instead of silent background access.

See also: [Reference: Credential storage](../reference/credential-storage.md), [Reference: Filesystem layout](../reference/filesystem-layout.md), [Reference: Commands](../reference/commands.md)

## Project trust is explicit

Project-local AVA configuration is not something the app should load implicitly for every directory. The public docs expose `--trust`, and the filesystem/config references show that project-local `.ava/` content can include tools, hooks, skills, commands, agents, and permissions.

That means trusting a project is a meaningful decision: it changes what local automation and configuration AVA is allowed to see and use.

## Credentials stay local first

In practice:

1. AVA can read provider credentials from environment variables.
2. It can store credentials in `~/.ava/credentials.json`.
3. It also has stronger secure-storage paths through keychain or encrypted local storage support.

The public-facing rule of thumb is simple: use keychain-backed or encrypted storage when possible, use environment variables for temporary sessions, and treat `~/.ava/credentials.json` as sensitive local state if you use the file-backed path.

## Permissions are not only on or off

AVA uses a layered model rather than a single yes/no switch:

1. tool source and type matter
2. filesystem paths matter
3. shell commands are classified by risk
4. persistent rules can affect future decisions

That is why AVA exposes both permission commands and trust controls instead of assuming every tool call should behave the same way.

## Sandboxing reduces blast radius

AVA includes OS-level sandboxing support for specific execution paths rather than as a blanket guarantee for every command it can run.

This does not mean every action is risk-free or always sandboxed. It means the product has concrete sandboxing machinery that reduces risk for some command classes and supported execution paths.

## Logs and audit data are local operational state

AVA keeps several kinds of local operational state:

1. runtime logs under `~/.ava/logs/`
2. credential and config files under `~/.ava/`
3. audit-related state in the permissions/config layers

For users, the key point is that AVA keeps meaningful operational state locally. That improves debuggability and auditability, but it also means your local machine remains the main trust boundary.

## What this means in practice

If you use AVA on a real codebase, the safest mental model is:

1. trust projects deliberately
2. keep credentials in local secure storage when possible
3. treat local logs and state as part of your development environment
4. do not assume every surface or plugin has the same security posture

That is the safest mental model for using AVA on a real codebase.
