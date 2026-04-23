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
    - Save/load/list and incremental add-message persistence.
    - Conversation-tree helpers (`get_tree`, `get_branch`, `branch_from`, `switch_branch`, branch-leaf listing).
    - Shared session/tree DTOs are treated as `ava_types`-owned data and consumed by `ava_session`.
3. Active composition wiring:
   - `ava_runtime` now links `ava_config` and `ava_session` (no longer deferred placeholders).

## Explicitly Deferred

1. Parsing/merging full Rust `config.yaml` and other TOML/YAML config surfaces.
2. OS keychain integration and secure-token migration paths.
3. OAuth refresh/device/browser flows and interactive credential prompts.
4. Advanced FTS/ranking search behavior from Rust session search paths.
5. Full Rust behavior parity across the remaining backend/runtime stack.

Milestone 4 stays a foundational slice: real persistence and contract-adjacent primitives are now active, but auth-heavy and broader runtime-parity work is intentionally left for later milestones.
