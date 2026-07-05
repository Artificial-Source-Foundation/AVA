# AVA Goals

This directory contains goal packages for long-running AVA product work. A goal package is a docs-first execution surface that can be handed to Codex `/goal` or another agent as a bounded objective.

Goal packages do not replace the live product baseline in `docs/product/`. They decompose that baseline into independently executable areas with reference paths, acceptance criteria, and validation commands.

## Active Packages

| Package | Purpose |
| --- | --- |
| `pi-mvp-parity/` | Make AVA a Pi-style native C++ coding agent with AVA's stronger safety, MCP, LSP, and tool features. |

## Related State

- Product baseline: `docs/product/mvp-baseline.md`
- Coverage ledger: `docs/product/mvp-coverage-ledger.md`
- Existing execution ledger: `goals/ava-mvp-baseline-pi-tui/`
- Pi reference repo: `docs/reference-code/pi/`

Reference repositories are behavior references only. Do not copy Pi source or architecture into AVA.
