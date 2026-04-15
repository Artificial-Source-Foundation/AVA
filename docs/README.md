---
title: "Docs"
description: "Entry point for AVA's documentation, organized by audience and purpose."
order: 1
updated: "2026-04-14"
---

# AVA Docs

AVA's documentation is organized by purpose so the repo is easier to navigate for users, contributors, and maintainers.

## Start Here

1. [Root README](../README.md) - public project overview and quick start
2. [AGENTS.md](../AGENTS.md) - source of truth for repo workflow, architecture, and coding conventions
3. [Project roadmap](project/roadmap.md) - current product direction and locked decisions
4. [Project backlog](project/backlog.md) - active work and execution priorities
5. [Backend correction plan (Milestone 3)](project/backlog.md#milestone-3-backend-correction-plan) - execution-ready backend correction tracks derived from M1 and M2
6. [AVA 3.3.1 eval plan](project/ava-3.3.1-evals.md) - upcoming core validation and benchmark expansion plan
7. [Benchmark docs](benchmark/README.md) - how the benchmark system works, how to run it, and how to compare reports
8. [Provider prompt benchmarking](project/provider-prompt-benchmarking.md) - implementation and usage docs for provider-family and system-prompt evals
9. [Architecture docs](architecture/README.md) - crate maps, capability audits, and architecture notes
10. [Cross-surface runtime map (Milestone 4)](architecture/cross-surface-runtime-map-m4.md) - interactive TUI, headless CLI, desktop, and web wiring map into the shared backend seam
11. [Cross-surface behavior audit (Milestone 5)](architecture/cross-surface-behavior-audit-m5.md) - canonical M5 shared-vs-divergent behavior audit with P0/P1/P2 drift classification for contract work
12. [Cross-surface runtime audit (supporting M5 detail)](architecture/cross-surface-runtime-audit-m5.md) - supporting parity-audit detail behind the canonical M5 behavior audit
13. [Canonical shared-backend contract (Milestone 6)](architecture/shared-backend-contract-m6.md) - concrete shared-backend contract-definition artifact based on Milestones 4 and 5
14. [Backend correction implementation roadmap (Milestone 7)](architecture/backend-correction-roadmap-m7.md) - implementation-ready backend correction roadmap derived from M5 audit and M6 contract
15. [Backend contract exceptions](architecture/backend-contract-exceptions.md) - versioned registry of intentional adapter-specific backend contract exceptions
16. [Testing docs](testing/README.md) - testing and verification concepts across Rust, frontend, and benchmark flows
17. [Operations docs](operations/README.md) - maintainer runbooks and operational guidance
18. [Changelog](../CHANGELOG.md) - shipped changes and release history

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

1. Users: `../README.md`, then `project/roadmap.md` if you want product context
2. Contributors: `../AGENTS.md`, `architecture/README.md`, `testing/README.md`, `benchmark/README.md`, `architecture/crate-map.md`
3. Maintainers: `project/backlog.md` (see Milestone 3 backend correction plan section), `architecture/README.md`, `architecture/agent-backend-capability-comparison-m2.md`, `architecture/cross-surface-runtime-map-m4.md`, `architecture/cross-surface-behavior-audit-m5.md`, `architecture/shared-backend-contract-m6.md`, `architecture/backend-correction-roadmap-m7.md`, `operations/README.md`, `benchmark/README.md`, `../CHANGELOG.md`

## Notes

1. `README.md` is the front door; `docs/` is the browsable knowledge base.
2. `AGENTS.md` remains the authoritative workflow and architecture document for this repo.
3. `CLAUDE.md`, `llms.txt`, and `CODEBASE_STRUCTURE.md` are compatibility entrypoints that point back to the active docs.
4. Historical material is preserved for context, not as current roadmap guidance.
5. Active docs pages now include frontmatter and `_meta.json` navigation manifests so the Markdown can be imported into a docs website later without a full rewrite.
6. `archive/` and `reference-code/` are intentionally kept outside the main published docs navigation because they are historical and research-oriented, not part of the primary website surface.
