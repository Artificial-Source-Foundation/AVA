# C++ Milestone 35 Boundaries — Permission Classification Parity (Small Safe Slice)

Milestone 35 is a narrow parity hardening pass for C++ headless permission classification. It adds a focused subset of high-value Rust parity patterns without broadening into a full `ava-permissions` port.

## In Scope

1. Expand C++ bash classification with additional **security-critical** detections aligned to Rust intent:
   - recursive+force `rm` targeting critical paths (for example `~`, `/home/*`)
   - semantic deletion bypass patterns (`find ... -delete`, `find ... -exec rm -rf ...`)
2. Add focused **parser-differential hardening** detections as high-risk ask paths:
   - IFS manipulation (`IFS=`, `$IFS`, `${IFS...}`)
   - dangerous brace expansion payloads (for example `{rm,-rf,/}`)
   - ANSI-C shell quoting escapes (`$'\\x..'`, `$'\\u..'`, `$'\\U..'`)
   - unicode whitespace separator tricks
3. Keep existing permission middleware behavior unchanged at policy level:
   - `Critical` bash classifications deny before approval
   - `High` classifications require explicit approval bridge
4. Add focused `ava_tools_tests` coverage for the new classifier and inspector behavior.

## Out Of Scope (Explicitly Deferred)

1. Full Rust `ava-permissions` parser/policy parity.
2. Tree-sitter command parsing and AST-driven command splitting in C++.
3. Persistent permission rules / allow-always storage.
4. Path/glob rule engines, warning tags, and richer policy metadata.
5. Permission audit store and denial/audit persistence surfaces.
6. Plugin/custom-tool permission hook parity beyond current source-aware middleware seams.

## Residual Extraction Notes

- `cpp/src/tools/command_classifier.cpp` remains a monolithic heuristic classifier (shell parsing + wrapper unwrapping + parser-differential handling in one unit).
- M35 intentionally landed only targeted parity hardening; a follow-up milestone should extract shell parser/wrapper logic into smaller translation units with dedicated tests.
- Explicit residual: extract a shared shell scanner utility (tokenization, delimiter matching, nested payload extraction) used by classifier paths such as nested command substitution and `find -exec` payload inspection.

## Validation Commands

```bash
git diff --check
ionice -c 3 nice -n 15 just cpp-build cpp-debug
ionice -c 3 nice -n 15 ./build/cpp/debug/tests/ava_tools_tests "[ava_tools]"
```

## Decision Point For Follow-Up

After M35, decide whether the C++ lane should stop at this heuristic security slice or promote a broader Rust-policy port (tree-sitter + persistent rules + audit/policy surfaces) as an explicitly scoped future milestone.
