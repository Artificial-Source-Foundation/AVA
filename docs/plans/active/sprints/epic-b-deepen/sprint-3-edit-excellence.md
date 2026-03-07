# Sprint 3: Edit Excellence

**Epic:** B — Deepen
**Duration:** 1 week
**Goal:** Best-in-class edit tool — 4-tier cascade, streaming, self-correction
**Parallel with:** Sprint 4 (Context Intelligence)

---

## Competitive Landscape

| Tool | Edit approach | Success rate | Streaming? |
|---|---|---|---|
| **Zed** | Streaming fuzzy matcher + Edit Agent | High | Yes (real-time) |
| **Gemini CLI** | 4-tier cascade + LLM self-correction | ~85% | No |
| **Aider** | SEARCH/REPLACE + relative indent + git cherry-pick | High | No |
| **OpenCode** | 9 cascading replacers (Levenshtein blocks) | High | No |
| **Pi-Mono** | Exact + Unicode normalization + fuzzy | Medium | No |
| **AVA (current)** | 8 strategies in Rust `ava-tools` | ~60% | No |

**Target:** Combine Gemini's cascade + Zed's streaming + Aider's relative indent.

---

## Story 3.1: 4-Tier Edit Cascade (from Gemini CLI)

**Reference:** `docs/reference-code/gemini-cli/packages/core/src/tools/edit.ts`

The Rust crate already has 9 strategies. Wire them into the TS edit tool as a tiered cascade:

**Tier 1: Exact** (0ms) — `ExactMatchStrategy`
**Tier 2: Flexible** (1-5ms) — `FlexibleMatchStrategy`, `IndentationAwareStrategy`, `TokenBoundaryStrategy`
**Tier 3: Structural** (5-20ms) — `BlockAnchorStrategy`, `LineNumberStrategy`, `RegexMatchStrategy`
**Tier 4: Fuzzy + Self-Correction** (20-500ms) — `FuzzyMatchStrategy` → if fails, LLM re-generates

**What to build in TS (`packages/extensions/tools-extended/src/edit-cascade.ts`):**
```typescript
export async function editWithCascade(content: string, old: string, new_: string): Promise<EditResult> {
  // Tier 1-3: Dispatch to Rust via invoke()
  const rustResult = await dispatchCompute('compute_fuzzy_replace', {
    content, old_text: old, new_text: new_, strategy: 'cascade'
  }, () => tsFallback(content, old, new_))

  if (rustResult.success) return rustResult

  // Tier 4: LLM self-correction (Gemini CLI pattern, line 445)
  return await selfCorrect(content, old, new_, rustResult.error)
}
```

**Self-correction (from Gemini CLI `attemptSelfCorrection`):**
1. Detect if file changed on disk (hash comparison)
2. Send to LLM: original instruction + failed old/new + error + latest file content
3. LLM returns corrected search/replace strings
4. Re-run cascade with corrected strings
5. Log correction success for telemetry

**Reference files:**
- Gemini cascade: `gemini-cli/packages/core/src/tools/edit.ts` (lines 130-552)
- Gemini self-correction: same file, line 445 (`attemptSelfCorrection`)
- Rust strategies: `crates/ava-tools/src/edit/` (9 strategies)

**Acceptance criteria:**
- [ ] 4-tier cascade integrated end-to-end
- [ ] Self-correction calls LLM on Tier 4 failure
- [ ] Edit success rate > 85% (benchmark against test corpus)

---

## Story 3.2: Relative Indentation (from Aider)

**Reference:** `docs/reference-code/aider/aider/coders/search_replace.py` — `RelativeIndenter` class

**What it does:** Converts absolute indentation to relative (stores only indent *changes*).
This allows matching code at different nesting levels (e.g., same logic inside a function vs class).

**What to add to TS edit tool:**
```typescript
// New preprocessor in edit cascade
class RelativeIndenter {
  // Convert "    foo\n        bar" to "+0 foo\n+4 bar"
  toRelative(text: string): string { ... }
  // Match relative patterns against any absolute indentation
  findMatch(source: string, pattern: string): { start: number, end: number } | null { ... }
}
```

Add as Tier 2.5 (between flexible and structural) in the cascade.

**Also add Unicode normalization (from Pi-Mono):**

**Reference:** `docs/reference-code/pi-mono/packages/coding-agent/src/core/tools/edit-diff.ts`

```typescript
function normalizeForMatch(text: string): string {
  return text
    .replace(/[\u2018\u2019\u201A\u201B]/g, "'")   // smart quotes
    .replace(/[\u201C\u201D\u201E\u201F]/g, '"')    // smart double quotes
    .replace(/[\u2010-\u2014]/g, '-')                // various dashes
    .replace(/[\u00A0\u2000-\u200A]/g, ' ')          // unicode spaces
    .replace(/[ \t]+$/gm, '')                         // trailing whitespace
}
```

**Acceptance criteria:**
- [ ] Relative indentation matching works across nesting levels
- [ ] Unicode normalization prevents false negatives from copy-paste
- [ ] Tests cover indentation edge cases

---

## Story 3.3: Streaming Edits (from Zed)

**Reference:** `docs/reference-code/zed/crates/agent/src/tools/streaming_edit_file_tool.rs` (1,981 lines)
**Also:** `zed/crates/agent/src/edit_agent/streaming_fuzzy_matcher.rs`

**What Zed does:**
1. Partial JSON from LLM parsed incrementally (`ToolEditParser`)
2. `StreamingFuzzyMatcher` matches `old_text` while tokens still arriving
3. `StreamingDiff` computes character-level diffs mid-stream
4. `Reindenter` auto-adjusts indentation from context
5. Edits render in UI before tool call completes

**What to build (TS, since it's LLM-stream processing):**

`packages/extensions/tools-extended/src/streaming-edit.ts`:
- Listen to LLM token stream for edit tool calls
- Parse partial JSON (old_text, new_text fields as they arrive)
- When old_text is complete enough (>80% tokens), start fuzzy matching
- When match found, begin applying new_text tokens as they arrive
- Emit per-character diffs to UI via message bus

**This is the highest-impact UX improvement** — edits appear in <500ms instead of 3-5s.

**Acceptance criteria:**
- [ ] Edits start rendering before LLM finishes generating
- [ ] Perceived latency < 500ms (vs 3-5s current)
- [ ] Per-hunk review UI shows streaming diffs (wire to Agent 3's component)
