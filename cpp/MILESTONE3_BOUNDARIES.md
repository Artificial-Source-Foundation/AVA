# C++ Milestone 3 Boundaries

This note records what is intentionally in-scope vs deferred for the foundational-library milestone.

## Implemented in Milestone 3

1. `ava_types`: foundational DTOs/enums/helpers and mention parsing.
2. `ava_control_plane`: canonical command/event inventories and queue-tier mapping helpers.
3. `ava_platform`: blocking local filesystem primitives and base execution DTOs.
4. `ava_runtime`: Milestone 3 composition target for `ava_types`, `ava_control_plane`, and `ava_platform` only.

## Explicitly Deferred

1. `ava_config` implementation parity (Milestone 4 target).
2. `ava_session` implementation parity (Milestone 4 target).
3. Async/streaming command execution runtime parity in C++ platform layer.
4. Session persistence, queue orchestration behavior beyond pure contract tables.

Current app/test wiring intentionally does not consume `ava_config` or `ava_session`; those placeholder targets remain in the tree for later milestones but are outside the Milestone 3 composition surface.

This boundary is intentional to keep Milestone 3 small, coherent, and honest.
