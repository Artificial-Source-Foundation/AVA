---
title: "Docs"
description: "Entry point for AVA's documentation, organized by audience and purpose."
order: 1
updated: "2026-04-20"
---

# AVA Docs

AVA's documentation is organized by purpose so the repo is easier to navigate for users, contributors, maintainers, and AI coding agents.

Public-facing entrypoint: [docs/index.md](index.md)

## Start Here

1. [Public docs index](index.md) - public tutorials, how-to guides, explanations, and reference pages
2. [Root README](../README.md) - project overview and fastest install paths
3. [How-to: Install AVA](how-to/install.md) - choose the right CLI, web, source, or desktop install path
4. [How-to: Download AVA Desktop](how-to/download-desktop.md) - desktop download and source-build path
5. [Troubleshooting: Common errors](troubleshooting/common-errors.md) - fast recovery for common runtime failures
6. [Reference: Install and release paths](reference/install-and-release-paths.md) - release artifacts and source-of-truth install surface map
7. [AGENTS.md](../AGENTS.md) - repo workflow, architecture, and coding conventions for contributors and AI coding agents
8. [Testing docs](testing/README.md) - Rust, frontend, desktop, and benchmark verification guidance
9. [Architecture docs](architecture/README.md) - crate maps, capability audits, and architecture notes
10. [Project roadmap](project/roadmap.md) - current product direction and locked decisions
11. [Project backlog](project/backlog.md) - active `0.6 -> V1` checklist plus archived prior backlog
12. [Operations docs](operations/README.md) - maintainer runbooks and operational guidance
13. [Changelog](../CHANGELOG.md) - shipped changes and release history

## Sections

1. [project/](project/) - roadmap, active backlog, and eval planning
2. [benchmark/](benchmark/) - benchmark architecture, workflows, reports, and prompt tuning
3. [testing/](testing/) - testing and verification concepts, including benchmark-backed validation
4. [operations/](operations/) - maintainer runbooks and operational guidance
5. [architecture/](architecture/) - crate maps, capability audits, and architecture checklists
6. [extend/](extend/) - plugins, MCP, custom tools, and instruction surfaces
7. [reference/](reference/) - providers, commands, credentials, and other stable reference material
8. [contributing/](contributing/) - contributor and release workflow docs
9. [troubleshooting/](troubleshooting/) - environment and platform-specific fixes

## Recommended Reading By Audience

1. Users: `../README.md`, then `index.md`, then the relevant tutorial or how-to page
2. Contributors: `../AGENTS.md`, `testing/desktop-testing.md`, `testing/README.md`, `architecture/README.md`, `benchmark/README.md`, `architecture/crate-map.md`
3. Maintainers: `project/backlog.md`, `testing/desktop-testing.md`, `architecture/README.md`, `architecture/agent-backend-capability-comparison-m2.md`, `architecture/cross-surface-runtime-map-m4.md`, `architecture/cross-surface-behavior-audit-m5.md`, `architecture/shared-backend-contract-m6.md`, `architecture/backend-correction-roadmap-m7.md`, `operations/README.md`, `benchmark/README.md`, `../CHANGELOG.md`

## Notes

1. `README.md` is the front door; `docs/` is the browsable knowledge base.
2. `AGENTS.md` remains the authoritative workflow and architecture document for this repo and is written primarily for AI assistants and automated coding agents.
3. `CLAUDE.md`, `llms.txt`, and `CODEBASE_STRUCTURE.md` are compatibility entrypoints that point back to the active docs.
4. Historical material is preserved for context, not as current roadmap guidance.
5. Active docs pages now include frontmatter and `_meta.json` navigation manifests so the Markdown can be imported into a docs website later without a full rewrite.
6. `archive/` and `reference-code/` are intentionally kept outside the main published docs navigation because they are historical and research-oriented, not part of the primary website surface.
