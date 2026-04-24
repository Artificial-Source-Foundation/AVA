# C++ Milestone 3 Boundaries

This note records what is intentionally in-scope vs deferred for the foundational-library milestone.

## Implemented in Milestone 3

1. `ava_types`: foundational DTOs/enums/helpers, core message JSON DTO coverage, conversation repair helpers, and mention parsing.
2. `ava_control_plane`: canonical command/event inventories, queue-tier mapping helpers, queue clear-target/session-owner helpers, and basic session precedence/replay payload helpers.
3. `ava_platform`: blocking local filesystem primitives and base execution DTOs.
4. `ava_m3_foundation_tests`: dedicated Milestone 3 verification target linking only `ava_types`, `ava_control_plane`, and `ava_platform`.

## Explicitly Deferred

1. `ava_config` implementation parity (Milestone 4 target).
2. `ava_session` implementation parity (Milestone 4 target).
3. Async/streaming command execution runtime parity in C++ platform layer.
4. Session persistence, queue orchestration behavior beyond pure contract tables.

Current full `ava_runtime` app composition has evolved with later milestones. Milestone 3 scope is now preserved by the dedicated M3 foundation test target above rather than by the full runtime aggregate.

This boundary is intentional to keep Milestone 3 small, coherent, and honest.
