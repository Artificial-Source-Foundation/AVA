# C++ Milestone 4 Boundaries

This note records what is implemented in the C++ Milestone 4 config/session foundation slice and what remains intentionally deferred.

## Implemented in Milestone 4

1. `ava_config` foundational runtime pieces:
    - XDG + legacy-aware path resolution (`config/data/state/cache`, trusted-projects path, credentials path, app DB path).
    - Trust store persistence in JSON (`trusted_projects.json`) with process cache + invalidation.
    - JSON credential store persistence with provider lookup and env-var override precedence.
    - Embedded model registry fixture with lookup/alias normalization/pricing/loop-prone helpers.
    - File-backed config/trust/credential writes currently go through the active `ava_platform` filesystem seam rather than bypassing it directly.
2. `ava_session` foundational SQLite persistence:
     - Real SQLite schema initialization for `sessions` + `messages`.
     - Session/message DTO+schema parity at the Milestone 4 foundation level for Rust-compatible persistence fields (including richer message JSON fields and session parent/token usage persistence).
     - Idempotent legacy-schema migration handling for newly added `sessions`/`messages` columns.
     - SQLite connection policy now enforces WAL + `synchronous=NORMAL` + `foreign_keys=ON` + `busy_timeout=5000` + `cache_size=-64000`.
     - Save/load/list and incremental add-message persistence.
     - Conversation-tree helpers (`get_tree`, `get_branch`, `branch_from`, `switch_branch`, branch-leaf listing).
     - Shared session/tree DTOs are treated as `ava_types`-owned data and consumed by `ava_session`.
3. Sensitive JSON write safety:
   - `credentials.json` and `trusted_projects.json` writes now end with owner-only permissions on POSIX (`0600`) while remaining safe no-op on Windows.
4. Active composition wiring:
   - `ava_runtime` now links `ava_config` and `ava_session` (no longer deferred placeholders).

## Explicitly Deferred

1. Parsing/merging full Rust `config.yaml` and other TOML/YAML config surfaces.
2. OS keychain integration and secure-token migration paths.
3. OAuth refresh/device/browser flows and interactive credential prompts.
4. Advanced FTS/ranking search behavior from Rust session search paths.
5. Full Rust behavior parity across the remaining backend/runtime stack.
6. Any expansion into web/desktop parity behavior, keychain integration, or broader runtime behavior changes beyond M4 config/session foundations.

Milestone 4 stays a foundational slice: real persistence and contract-adjacent primitives are now active, but auth-heavy and broader runtime-parity work is intentionally left for later milestones.
