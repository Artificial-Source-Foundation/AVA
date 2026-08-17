# Backend Next Approval Ledger

This ledger records the current approval boundary for the next backend workstreams. A listed implementation does not approve adjacent deferred work.

| Workstream | Status | Scope boundary |
| --- | --- | --- |
| 6 — permissive distribution/licensing closure | **Implemented for qualified x64 and AArch64 artifacts** | `THIRD_PARTY_NOTICES.md`, deterministic `PROVENANCE.json`, matching initialized gitlinks, committed direct-license SHA-256 policy verified against actual hashes, and strict source-build packaging close the native x86_64/x64 and AArch64/arm64 boundary. Carlo-owned `utils` paths at pinned revision `5ed11a1763eb982efcbc4d8407433010a8a317be`, including AArch64 CPU relaxation, are MIT; guarded BSD headers remain excluded by `MIT_LICENSE_ONLY`. Other architectures remain unqualified. |
| 7 — safe built-in LSP recipes | **Approved and implemented (narrowed)** | Global exact opt-in for one installed-only `clangd` recipe; hardlink-safe sealed executable identity and identity-bound launch permission, per-root routing/cache, bounded pull/centrally cached publish diagnostics, full-text on-disk `didChange`, passive private status, cleanup, tests, and an optional real-clangd smoke. This installed-only `clangd` integration is the sole automatic LSP recipe. Every other server requires explicit configuration behind the existing trust/launch-permission boundary. Project opt-in, downloads, package/toolchain managers, workspace executables, network access, watchers, and unsaved-buffer synchronization remain excluded. |
| 8 — typed settings | **Pending research** | Research typed settings and migration/reload semantics before implementation approval. This entry does not authorize a merged model-writable settings surface or weakening project trust. |

Workstream 7 preserves ACP exclusion, child-subagent LSP exclusion, restart-required LSP reload behavior, and the existing headless permission boundary.
