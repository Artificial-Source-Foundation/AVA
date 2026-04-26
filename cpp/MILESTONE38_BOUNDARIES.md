# C++ Milestone 38 Boundaries: Config State Primitives

Milestone 38 adds narrow C++ config parity primitives for routing profiles, project-local state, OAuth credential updates, and safe key redaction. It does not promote the full Rust config/keychain stack.

## In Scope

1. `RoutingConfig` data types with JSON conversion, normalization, and complete-target lookup for cheap/capable profiles.
2. Project state persistence at `.ava/state.json` for last provider/model, recent models, and plan/code model selections with a five-entry recent-model cap.
3. Credential-store OAuth token updates that preserve existing static credential fields.
4. Rust-compatible key redaction for logs and an explicit native-keychain unavailable helper.
5. Focused `ava_config_tests` coverage for routing, project state persistence, OAuth credential round-trip, key redaction, path helpers, and owner-only project-state permissions.

## Out of Scope

1. Native OS keychain integration.
2. Encrypted file fallback, password prompting, and keychain migration flows.
3. OAuth refresh HTTP flows and browser/device-code auth UX.
4. Full YAML config loading or config-manager parity.
5. Wiring routing profiles into provider/runtime model selection.

## Validation

```bash
ionice -c 3 nice -n 15 just cpp-build cpp-debug --target ava_config_tests
ionice -c 3 nice -n 15 ./build/cpp/debug/tests/ava_config_tests "[ava_config]"
git diff --check
```

## Residual Risk

M38 makes config-state primitives available and tested, but it is still not full Rust config parity. Callers must treat `os_keychain_available()` as an explicit false capability, route OAuth refresh through future auth work, and wire routing/project-state selection deliberately before claiming product-level behavior parity.
