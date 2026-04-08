---
title: "Docs"
description: "Entry point for AVA's documentation, organized by audience and purpose."
order: 1
updated: "2026-04-08"
---

# AVA Docs

AVA's documentation is organized by purpose so the repo is easier to navigate for users, contributors, and maintainers.

## Start Here

1. [Root README](../README.md) - public project overview and quick start
2. [AGENTS.md](../AGENTS.md) - source of truth for repo workflow, architecture, and coding conventions
3. [Project roadmap](project/roadmap.md) - current product direction and locked decisions
4. [Project backlog](project/backlog.md) - active work and execution priorities
5. [Changelog](../CHANGELOG.md) - shipped changes and release history

## Sections

1. [project/](project/) - roadmap and active backlog
2. [architecture/](architecture/) - crate map and architecture checklists
3. [extend/](extend/) - plugins, MCP, custom tools, and instruction surfaces
4. [reference/](reference/) - providers, commands, credentials, and other stable reference material
5. [contributing/](contributing/) - contributor and release workflow docs
6. [troubleshooting/](troubleshooting/) - environment and platform-specific fixes

## Recommended Reading By Audience

1. Users: `../README.md`, then `project/roadmap.md` if you want product context
2. Contributors: `../AGENTS.md`, `architecture/crate-map.md`, `contributing/releasing.md`
3. Maintainers: `project/backlog.md`, `architecture/plugin-boundary.md`, `../CHANGELOG.md`

## Notes

1. `README.md` is the front door; `docs/` is the browsable knowledge base.
2. `AGENTS.md` remains the authoritative workflow and architecture document for this repo.
3. `CLAUDE.md`, `llms.txt`, and `CODEBASE_STRUCTURE.md` are compatibility entrypoints that point back to the active docs.
4. Historical material is preserved for context, not as current roadmap guidance.
5. Active docs pages now include frontmatter and `_meta.json` navigation manifests so the Markdown can be imported into a docs website later without a full rewrite.
6. `archive/` and `reference-code/` are intentionally kept outside the main published docs navigation because they are historical and research-oriented, not part of the primary website surface.
