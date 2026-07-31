# AVA Documentation Maintenance

These instructions apply to first-party documentation under `docs/` and supplement the repository-level [`AGENTS.md`](../AGENTS.md).

## Folder ownership

- `core/`: current user setup, configuration, providers, resources, tools, reasoning, and delegation.
- `interfaces/`: human-facing terminal and optional desktop interfaces.
- `extensions/`: current LSP, MCP, and plugin setup/authoring guides.
- `operations/`: build, test, Docker, terminal, diagnosis, and release operations.
- `development/`: current maintainer architecture and engineering policy; narrow implementation notes belong in `development/internals/`.
- `security/`: practical security guidance plus the fixed containment contract.
- `product/`: current feature and capability baselines.
- `plans/`, `roadmap/`, and `goals/`: future work, sequencing, approvals, and acceptance packages.
- `history/`, `versions/`, and `interop/evidence/`: dated ledgers, release-position journals, and bounded evidence.
- `schema/`: machine-readable documentation contracts.

Every category owns a concise `README.md` that links each document directly in that directory. [`README.md`](README.md) is the single human spine; root [`llms.txt`](../llms.txt) is the concise robot entry point.

## Fixed paths and authority

Do not move or rename `acp.md`, `acp-support.json`, `rpc-protocol.md`, `headless-protocol.md`, `session-format.md`, `plugin-compatibility-policy.md`, `schema/theme.schema.json`, `security/containment.md`, files under `interop/evidence/`, `goals/`, `roadmap/`, `versions/`, or `reference-code/` without a separately approved contract/evidence migration.

Current descriptive pages must follow implementation and tests. Normative contracts define compatibility and verified guarantees. Plans, roadmaps, and goals do not prove implementation. History and evidence preserve dated scope and may be intentionally stale; never use them alone as proof of current behavior.

`docs/reference-code/` is comparative input only. Never include it in AVA documentation catalogs, structure/link claims, packages, broad source searches, or generated summaries.

## Moving and linking documents

- Use `git mv` for a rename or taxonomy move; do not copy/delete or add legacy pointer stubs.
- Preserve the source/artifact split. `operations/release-artifact-readme.md` is a source template installed as the artifact's top-level README, and artifact links target the staged categorized documentation layout.
- Update every inbound and outbound relative Markdown link after a move, including links in historical first-party docs.
- Update root-relative path prose, source/test/example literals, and every absolute AVA `blob/develop` self-link that names a moved file.
- Keep fixed contract headings and schemas stable unless the contract itself changes. Repair outbound paths in evidence without rewriting the observed evidence.
- Keep the human spine, category index, and `llms.txt` synchronized. Do not add a hand-maintained JSON catalog.

## Required checks

Documentation path changes require all three layers:

1. **Source:** `python3 scripts/verify-markdown-links.py . --source-tree` and `git diff --check`.
2. **Structure:** Phase B will register the intended CTests `ava_tests.documentation_structure_checker` and `ava_tests.documentation_structure_source`; run both once available.
3. **Package/install:** run `ava_release.install_component` and, on supported Linux configurations, `ava_release.package_linux` after synchronizing CMake, packaging, and exact allowlists.

Also search every old path across first-party source, docs, goals, tests, and examples. Exclude dependency and `docs/reference-code/` trees. A package template or allowlist transition must update source and staged layouts atomically rather than making source-tree links pretend to be artifact links.
