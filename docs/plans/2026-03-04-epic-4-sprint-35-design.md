# Epic 4 Sprint 35 Design (Performance, Testing, Documentation)

**Goal:** Optimize Sprint 33-34 Rust backend hot paths, expand test confidence toward the sprint coverage target, and finalize backend documentation for production readiness.

## Scope

Sprint 35 stories:

1. Performance optimization
2. Comprehensive test suite
3. Documentation completion

## Performance Strategy

Focus optimization on Epic 4 backend paths introduced in Rust migration:

- `ava-validator` (validation + retry loop + syntax scan)
- `ava-agent` reflection classification/fix path
- `ava-extensions` registration/hook routing paths
- `src-tauri` adapter glue that maps DTOs into these crates

Optimization themes:

- reduce avoidable allocations in hot paths
- remove repeated scans where one-pass logic is sufficient
- use borrowed data and `Cow` where practical
- benchmark before/after with stable local harnesses

## Testing Strategy

Expand beyond behavior tests to include:

- property-style tests for parser/classifier robustness
- benchmark harnesses (Criterion) for hot paths
- additional edge-case tests for validation/retry/reflection

Coverage emphasis is Sprint 33-35 backend crates and Tauri command adapters.

## Documentation Strategy

- add rustdoc comments for public APIs in `ava-extensions`, `ava-validator`, and `ava-agent`
- add architecture notes for extension/validation/reflection interaction
- document performance decisions and benchmark procedure

## Acceptance Mapping

- Performance target: benchmark deltas reported for optimized hot paths
- Test target: expanded suite and benchmark harness in repo
- Docs target: rustdoc + architecture docs complete for new backend systems
