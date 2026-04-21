---
title: "Docs Manifest"
description: "Machine-readable-for-humans list of public Diataxis pages and why each exists."
updated: "2026-04-21"
---

# Docs Manifest

This manifest lists the Diataxis-facing pages in the public docs layer and why each exists.

## Public layer

| Page | Diátaxis Type | Why it exists |
|---|---|---|
| `docs/index.md` | Landing / routing | Public entrypoint that separates quick-start docs from internal architecture/project material. |
| `docs/tutorials/README.md` | Tutorial section index | Explains when to use tutorials and routes users into guided first-success walkthroughs. |
| `docs/tutorials/first-run.md` | Tutorial | Step-by-step first success path: install, auth, and launch using real repo commands. |
| `docs/tutorials/your-first-workflow.md` | Tutorial | Guided walkthrough for a small edit-review-verify loop in a real repository. |
| `docs/how-to/README.md` | How-to section index | Explains the task-focused how-to section and keeps contributor-only workflow pages out of the public path. |
| `docs/how-to/install.md` | How-to | Task-focused install entrypoint that splits CLI and desktop surfaces clearly. |
| `docs/how-to/download-desktop.md` | How-to | Desktop-specific download and local-build guide grounded in the current Tauri release flow and current release-availability limits. |
| `docs/how-to/configure.md` | How-to | Task-focused provider and local configuration setup. |
| `docs/how-to/agents.md` | How-to | Practical setup for startup primary-agent profiles and delegated subagent profiles, including trust and legacy compatibility behavior. |
| `docs/how-to/run-locally.md` | How-to | Task-focused commands for TUI, headless, desktop, and feature-gated web mode. |
| `docs/how-to/ci-headless-automation.md` | How-to | Task-focused CI/unattended automation guidance grounded in current headless behavior, JSON mode, env setup, and repository workflows. |
| `docs/how-to/ollama-local-models.md` | How-to | Ollama-only local model usage guide grounded in current provider defaults, env overrides, and verification paths. |
| `docs/explanation/README.md` | Explanation section index | Explains when to use explanation pages for background and rationale. |
| `docs/explanation/ava-surfaces-and-doc-boundaries.md` | Explanation | Clarifies why public docs are separated from contributor and architecture material. |
| `docs/explanation/security-and-trust.md` | Explanation | Explains AVA's trust model, local credential handling, permissions, sandboxing, and logging boundaries. |

## Existing pages intentionally reused (not replaced)

| Page | Why it is linked instead of duplicated |
|---|---|
| `docs/reference/README.md` | Existing stable reference index already covers commands/providers/credentials. |
| `docs/reference/commands.md` | Canonical command surface reference used by tutorials and how-to pages. |
| `docs/reference/providers-and-auth.md` | Canonical provider/auth behavior used by first-run tutorial. |
| `docs/project/README.md`, `docs/architecture/README.md`, `docs/contributing/README.md`, `docs/operations/README.md` | Internal or maintainer-oriented docs preserved as-is and linked from the new index under a separate section. |

## Reference pages in the public layer

| Page | Diataxis Type | Why it exists |
|---|---|---|
| `docs/reference/README.md` | Reference | Section index for factual command, configuration, and runtime reference pages. |
| `docs/reference/overview.md` | Reference | Explains how to read the reference section and where its source-of-truth files live. |
| `docs/reference/configuration.md` | Reference | Documents config sources, resolver behavior, and current path caveats from code. |
| `docs/reference/environment-variables.md` | Reference | Lists runtime env vars clearly consumed by the current codebase. |
| `docs/reference/filesystem-layout.md` | Reference | Documents the current `~/.ava` and project-local `.ava` file layout. |
| `docs/reference/install-and-release-paths.md` | Reference | Maps public binary/source install paths and release automation to concrete repo files. |
| `docs/reference/providers-and-auth.md` | Reference | Canonical provider IDs, aliases, auth surfaces, and credential lookup order. |
| `docs/reference/commands.md` | Reference | Canonical slash commands, CLI subcommands, and important runtime flags. |
| `docs/reference/credential-storage.md` | Reference | Documents credential storage paths and security posture. |
| `docs/reference/web-api.md` | Reference | Feature-gated implementation reference for the `ava serve` backend surface. |
| `docs/_meta.json` plus section `_meta.json` files | Navigation metadata | Keep the docs tree importable into docs-site generators without restructuring the Markdown files. |

## Troubleshooting pages in the public layer

| Page | Diataxis Type | Why it exists |
|---|---|---|
| `docs/troubleshooting/README.md` | Troubleshooting index | Routes users to common setup/runtime recovery pages by failure type. |
| `docs/troubleshooting/common-errors.md` | Troubleshooting | Fast fixes for common provider/auth/config/CLI startup failures. |
| `docs/troubleshooting/ollama-local-models.md` | Troubleshooting | Focused Ollama local-model failure recovery and verification guidance. |
| `docs/troubleshooting/tauri-toolchain-checklist.md` | Troubleshooting | Linux desktop toolchain checklist for Tauri development setup. |
| `docs/troubleshooting/webkitgtk-rendering.md` | Troubleshooting | Linux WebKitGTK rendering failure diagnosis and mitigation steps. |

## Section Health

| Section | Current coverage | Notes |
|---|---|---|
| Tutorials | 2 guides plus section index | Covers first setup and first workflow. |
| How-to | 7 guides plus section index | Covers install, desktop download, configure, primary/subagent profile setup, Ollama local-model usage, local runs, and CI/headless automation. |
| Reference | 9 user-facing pages plus 1 implementation-reference page and navigation metadata | Covers install/release paths, commands, providers, config, env vars, filesystem, credential storage, and the feature-gated web API. |
| Troubleshooting | 5 pages plus navigation metadata | Covers common runtime failures, Ollama-specific local-model diagnostics, and Linux desktop setup/rendering issues. |
| Explanation | 2 pages plus section index | Covers docs boundaries plus security/trust concepts. |
