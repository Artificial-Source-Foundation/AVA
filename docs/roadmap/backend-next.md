# Backend Next Approval Ledger

This ledger records the current approval boundary for the next backend workstreams. A listed implementation does not approve adjacent deferred work.

| Workstream | Status | Scope boundary |
| --- | --- | --- |
| 6 — permissive distribution/licensing closure | **Pending** | AVA's production-linked `utils` objects still require licensing resolution before an MIT-only binary distribution. No licensing migration is approved by this entry. |
| 7 — safe built-in LSP recipes | **Approved and implemented** | Global exact opt-in for installed-only `clangd`, `gopls`, and `rust-analyzer`; canonical owner-safe executable identity, per-root routing/cache, pull/publish diagnostics, passive private status, launch permission, bounds, cleanup, tests, and an optional real-clangd smoke. Project opt-in, downloads, package/toolchain managers, workspace executables, network access, watchers, and unsaved-buffer synchronization remain excluded. |
| 8 — typed settings | **Pending research** | Research typed settings and migration/reload semantics before implementation approval. This entry does not authorize a merged model-writable settings surface or weakening project trust. |

Workstream 7 preserves ACP exclusion, child-subagent LSP exclusion, restart-required LSP reload behavior, and the existing headless permission boundary.
