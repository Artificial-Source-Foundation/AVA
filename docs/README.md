---
title: "Docs"
description: "Entry point for AVA's documentation, organized by audience and purpose."
order: 1
updated: "2026-04-20"
---

# AVA Docs

This page is the internal map of the repository docs tree.

If you want the public user docs first, start at [docs/index.md](index.md). Keep maintainer, architecture, and historical planning material out of the normal product-reading path.

## Start Here

1. [Public docs index](index.md) - public tutorials, how-to guides, troubleshooting, and reference pages
2. [Root README](../README.md) - project overview and fastest install paths
3. [How-to: Install AVA](how-to/install.md) - choose the right CLI, web, source, or desktop install path
4. [How-to: Download AVA Desktop](how-to/download-desktop.md) - desktop download and source-build path
5. [Troubleshooting: Common errors](troubleshooting/common-errors.md) - fast recovery for common runtime failures
6. [Project roadmap](project/roadmap.md) - current product direction and locked decisions
7. [Project backlog](project/backlog.md) - active `0.6 -> V1` checklist plus archived prior backlog
8. [AGENTS.md](../AGENTS.md) - repo workflow, architecture, and coding conventions for contributors and AI coding agents
9. [Contributing docs](contributing/README.md) - contributor workflow and release docs
10. [Testing docs](testing/README.md) - maintainer and contributor verification guidance
11. [Architecture docs](architecture/README.md) - canonical crate maps plus historical architecture notes
12. [Operations docs](operations/README.md) - maintainer runbooks and operational guidance
13. [Changelog](../CHANGELOG.md) - shipped changes and release history

## Sections

1. [project/](project/) - roadmap, active backlog, and eval planning
2. [tutorials/](tutorials/) - guided first-success walkthroughs
3. [how-to/](how-to/) - task-focused user guides
4. [explanation/](explanation/) - background and rationale for product behavior
5. [reference/](reference/) - providers, commands, credentials, and other stable reference material
6. [troubleshooting/](troubleshooting/) - environment and platform-specific fixes
7. [contributing/](contributing/) - contributor and release workflow docs
8. [testing/](testing/) - maintainer and contributor verification material
9. [benchmark/](benchmark/) - benchmark architecture, workflows, reports, and prompt tuning
10. [operations/](operations/) - maintainer runbooks and operational guidance
11. [architecture/](architecture/) - crate maps, canonical owner docs, and historical transition notes
12. [extend/](extend/) - advanced plugins, MCP, custom tools, and instruction surfaces

## Recommended Reading By Audience

1. Users: `../README.md`, then `index.md`, then the relevant tutorial or how-to page
2. Contributors: `../AGENTS.md`, `contributing/README.md`, `testing/README.md`, `architecture/README.md`, `architecture/crate-map.md`
3. Maintainers: `project/backlog.md`, `testing/README.md`, `architecture/README.md`, `architecture/shared-backend-contract-m6.md`, `architecture/backend-correction-roadmap-m7.md`, `archive/architecture/README.md`, `operations/README.md`, `benchmark/README.md`, `../CHANGELOG.md`

## Reading Rule

1. Start with `README.md` and `docs/index.md` for product understanding.
2. Use `project/` for current direction and priorities.
3. Use `contributing/`, `testing/`, `benchmark/`, `operations/`, and most of `architecture/` only when doing implementation or maintenance work.

## Notes

1. `README.md` is the front door; `docs/index.md` is the public docs landing page; this file is the internal map.
2. `AGENTS.md` remains the authoritative workflow and architecture document for this repo and is written primarily for AI assistants and automated coding agents.
3. `CLAUDE.md`, `llms.txt`, and `CODEBASE_STRUCTURE.md` are compatibility entrypoints that point back to the active docs.
4. Historical material is preserved for context, not as current roadmap guidance.
5. Active docs pages now include frontmatter and `_meta.json` navigation manifests so the Markdown can be imported into a docs website later without a full rewrite.
6. `archive/` and `reference-code/` are intentionally kept outside the main published docs navigation because they are historical and research-oriented, not part of the primary website surface.
