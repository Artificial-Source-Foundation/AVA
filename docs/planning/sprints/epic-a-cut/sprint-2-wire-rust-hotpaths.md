# Sprint 2: Wire Rust Hotpaths

**Epic:** A — Cut & Clean
**Duration:** 1 week
**Goal:** Frontend calls Rust for all compute-heavy operations via Tauri invoke()
**Depends on:** Sprint 1, Agent 1 (Epic 4 gaps), Agent 3 (bridge layer)

---

## Story 2.1: Platform Dispatch Layer

**What to build:**
`packages/core-v2/src/platform-dispatch.ts` — routes compute calls to Rust or TS based on runtime.

```typescript
import { getPlatform } from './platform.js'

export function isRunningInTauri(): boolean {
  return typeof window !== 'undefined' && '__TAURI__' in window
}

export async function dispatchCompute<T>(
  rustCommand: string,
  rustArgs: Record<string, unknown>,
  tsFallback: () => Promise<T>
): Promise<T> {
  if (isRunningInTauri()) {
    const { invoke } = await import('@tauri-apps/api/core')
    return invoke<T>(rustCommand, rustArgs)
  }
  return tsFallback()
}
```

**Reference:** No competitor does this (they're all single-runtime). This is AVA's
unique hybrid advantage — desktop uses Rust, CLI uses TS.

**Acceptance criteria:**
- [ ] `dispatchCompute()` routes to Rust in Tauri, TS in Node
- [ ] Exported from core-v2
- [ ] Unit test with mocked Tauri

---

## Story 2.2: Wire Edit Tool to Rust

**What to wire:**
The core-v2 edit tool should call `compute_fuzzy_replace` for fuzzy matching.

**Reference files:**
- Rust: `src-tauri/src/commands/compute_fuzzy.rs` (4 strategies, Levenshtein)
- TS: `packages/core-v2/src/tools/edit.ts`

**Approach:**
```typescript
// In edit tool execute():
const result = await dispatchCompute('compute_fuzzy_replace', {
  content: fileContent,
  old_text: input.oldText,
  new_text: input.newText,
  strategy: 'auto'
}, () => typescriptFuzzyReplace(fileContent, input.oldText, input.newText))
```

**Acceptance criteria:**
- [ ] Edit uses Rust fuzzy match in desktop
- [ ] Falls back to TS in CLI
- [ ] Benchmark shows measurable improvement

---

## Story 2.3: Wire Grep/Glob to Rust

**What to wire:**
The core-v2 grep tool should call `compute_grep` for file search.

**Reference files:**
- Rust: `src-tauri/src/commands/compute_grep.rs` (WalkDir + regex)
- TS: `packages/core-v2/src/tools/grep.ts`

**Acceptance criteria:**
- [ ] Grep uses Rust compute in desktop
- [ ] Falls back to TS ripgrep wrapper in CLI
- [ ] Large codebase search is noticeably faster

---

## Story 2.4: Wire Memory, Permissions, Validation to Rust

**What to wire:**
These extensions should call Rust crates via invoke() in Tauri.

| Extension | Rust command | Current TS |
|---|---|---|
| memory | `memory_remember/recall/search/recent` | `packages/extensions/memory/` |
| permissions | `evaluate_permission` | `packages/extensions/permissions/` |
| validator | `validation_validate_edit` | `packages/extensions/validator/` |

**Reference:** Agent 3 built `src/services/rust-bridge.ts` with typed wrappers.
Use `dispatchCompute()` in each extension's `activate()`.

**Acceptance criteria:**
- [ ] Memory extension uses Rust SQLite in desktop
- [ ] Permissions uses Rust tree-sitter bash parser in desktop
- [ ] Validation uses Rust syntax checker in desktop
- [ ] CLI still works with pure TS
