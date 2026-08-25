# Backend Next Approval Ledger

This ledger records the current approval boundary for the next backend workstreams. A listed implementation does not approve adjacent deferred work.

| Workstream | Status | Scope boundary |
| --- | --- | --- |
| 6 — permissive distribution/licensing closure | **Implemented static package/provenance gates for x64 and AArch64** | `THIRD_PARTY_NOTICES.md`, `PROVENANCE.json`, matching initialized gitlinks, direct-license SHA-256 policy, and strict source-build packaging close the static native x64/AArch64 source/license/architecture boundary. This is not full release qualification: the [current release ledger](../product/release-readiness.md) keeps first publication Linux x64 only and requires exact native retained-byte evidence. |
| 7 — safe built-in LSP recipes | **Approved and implemented (narrowed)** | Global exact opt-in for one installed-only `clangd` recipe; hardlink-safe sealed executable identity and identity-bound launch permission, per-root routing/cache, bounded pull/centrally cached publish diagnostics, full-text on-disk `didChange`, passive private status, cleanup, tests, and an optional real-clangd smoke. This installed-only `clangd` integration is the sole automatic LSP recipe. Every other server requires explicit configuration behind the existing trust/launch-permission boundary. Project opt-in, downloads, package/toolchain managers, workspace executables, network access, watchers, and unsaved-buffer synchronization remain excluded. |
| 8 — typed settings | **Pending research** | Research typed settings and migration/reload semantics before implementation approval. This entry does not authorize a merged model-writable settings surface or weakening project trust. |

Workstream 7 preserves ACP exclusion, child-subagent LSP exclusion, restart-required LSP reload behavior, and the existing headless permission boundary.
