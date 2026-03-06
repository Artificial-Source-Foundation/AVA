# Sprint 12: Edit Excellence II — Implementation Prompt

> For AI coding agent. Estimated: 6 features, mix S/M effort.
> Run `npm run test:run && npx tsc --noEmit` after each feature.

---

## Role

You are implementing Sprint 12 (Edit Excellence II) for AVA, a multi-agent AI coding assistant.

Read these files first:
- `CLAUDE.md` (conventions, architecture, dispatchCompute pattern)
- `AGENTS.md` (code standards, common workflows)

---

## Pre-Implementation: Competitor Research Phase

**CRITICAL**: Before implementing each feature, you MUST read the relevant competitor reference code and extract best patterns. The reference code is in `docs/reference-code/`. The audit summaries are in `docs/research/audits/`.

For EACH feature below, follow this workflow:

1. **Read** the listed competitor reference files
2. **Extract** the key algorithm, data structures, thresholds, and edge cases
3. **Adapt** the pattern to AVA's TypeScript + SolidJS + dispatchCompute architecture
4. **Implement** using AVA conventions (strict TS, no `any`, explicit return types, <300 lines/file)
5. **Test** with unit tests
6. **Verify** by running `npm run test:run && npx tsc --noEmit`

---

## Feature 1: Concurrent Edit Race (4 Strategies)

### Competitor Research
Read these files and extract the racing pattern:
- `docs/reference-code/plandex/app/server/model/plan/build.go` — Look for goroutine racing, `raceResult`, channel-based first-valid-wins
- `docs/reference-code/plandex/app/server/model/plan/build_race.go` — Look for `buildRace`, cascading fallbacks, error counting
- `docs/research/audits/plandex-audit.md` — "Worth Stealing" section on concurrent builds

**Key patterns to extract from Plandex:**
- How 4 strategies race in parallel (auto-apply, fast-apply, validation loop, whole-file)
- How first valid result wins and cancels others
- How errors are counted before triggering fallbacks
- How validation is integrated into the race

### What to Build
Race multiple edit strategies in parallel. First valid result wins.

**File:** `packages/extensions/tools-extended/src/edit/race.ts` (new)

```typescript
export interface RaceStrategy {
  name: string
  apply(content: string, oldText: string, newText: string): Promise<RaceResult | null>
}

export interface RaceResult {
  content: string
  strategy: string
  confidence: number
}

export async function raceEditStrategies(
  content: string,
  oldText: string,
  newText: string,
  strategies: RaceStrategy[],
  signal?: AbortSignal
): Promise<RaceResult>
```

**Strategies to race:**
1. **Exact cascade** — Current 4-tier cascade from `cascade.ts` (exact → flexible → structural → fuzzy)
2. **Levenshtein sliding window** — Normalized similarity ≥0.8 (from `edit-replacers.ts` block anchor)
3. **AST-aware replace** — Use dispatchCompute to Rust tree-sitter parser if available, TS fallback with regex structural match
4. **Whole-content rewrite** — If file is small (<100 lines), just return the new content with the replacement applied via line-by-line diff

**Implementation:**
- Use `Promise.race()` with `AbortController` — when first valid result resolves, abort others
- Each strategy has a timeout (5s default)
- Validate result: ensure newText appears in output, file is parseable (use existing `syntaxValidator`)
- Return winning strategy name + confidence for telemetry

**Integration:** Wire into `packages/extensions/tools-extended/src/edit/cascade.ts` as a new top-level option. When `race: true` in edit config, use race instead of sequential cascade.

### Tests
- `packages/extensions/tools-extended/src/edit/race.test.ts`
- Test: exact match wins immediately (fastest)
- Test: fuzzy wins when exact fails
- Test: timeout triggers fallback
- Test: all fail → return error with strategy names attempted

---

## Feature 2: Streaming Fuzzy Matcher

### Competitor Research
Read these files and extract the incremental matching algorithm:
- `docs/reference-code/zed/crates/streaming_diff/src/streaming_diff.rs` — Look for `StreamingDiff` struct, `CharOperation` enum, scoring constants (`INSERTION_SCORE`, `DELETION_SCORE`, `EQUALITY_BASE`), exponential run scoring
- `docs/reference-code/zed/crates/agent/src/edit_agent/streaming_fuzzy_matcher.rs` — Look for `StreamingFuzzyMatcher`, `SearchMatrix`, line-level DP, `line_hint`, costs (`REPLACEMENT_COST=1`, `INSERTION_COST=3`, `DELETION_COST=10`)
- `docs/research/audits/zed-audit.md` — "Worth Stealing" section on streaming diff

**Key patterns to extract from Zed:**
- How the DP matrix expands incrementally as tokens arrive
- The exponential scoring for consecutive matches (`1.8^(run/4)`)
- How `line_hint` breaks ties for multiple matches
- The 80% similarity threshold

### What to Build
Incremental fuzzy matching as LLM tokens stream in, so edits can preview before generation completes.

**File:** `packages/extensions/tools-extended/src/edit/streaming-fuzzy-matcher.ts` (new)

```typescript
export class StreamingFuzzyMatcher {
  private queryLines: string[]
  private matrix: number[][]  // DP scoring matrix
  private equalRuns: Map<string, number>  // Track consecutive matches

  constructor(private fileContent: string, private threshold: number = 0.8)

  /** Push a new chunk of LLM output. Returns match ranges if found. */
  pushChunk(chunk: string): MatchResult | null

  /** Get current best match range in the file */
  getBestMatch(): { startLine: number; endLine: number; confidence: number } | null
}

// Scoring constants (adapted from Zed)
const INSERTION_COST = -1
const DELETION_COST = -20
const EQUALITY_BASE = 1.8  // Exponential reward for consecutive matches
```

**Algorithm (adapted from Zed to TypeScript):**
1. Split file content into lines
2. As each chunk arrives, buffer incomplete lines
3. On complete line: expand DP matrix (one new row)
4. Score: `EQUALITY_BASE ^ (consecutiveMatches / 4)` for matches, penalties for insertions/deletions
5. Backtrack to find best match range
6. If match confidence ≥ threshold → emit match with byte offsets

**Integration:** Wire into `streaming-edit-parser.ts` to replace the current `findFuzzyWindow()` (which is simpler). Emit `edit:stream-preview` events with matched ranges for UI highlighting.

**Rust hotpath:** Add `compute_streaming_fuzzy_match` command in `src-tauri/src/commands/` with TS fallback via `dispatchCompute`.

### Tests
- Test: exact content matches immediately with confidence 1.0
- Test: content with minor whitespace differences matches at ≥0.8
- Test: incremental chunks converge to correct match
- Test: no match below threshold returns null
- Test: exponential scoring prefers contiguous matches over scattered

---

## Feature 3: External Editor for Tool Args

### Competitor Research
Read these files and extract the modifiable tool pattern:
- `docs/reference-code/gemini-cli/packages/core/src/tools/modifiable-tool.ts` — Look for `ModifiableDeclarativeTool`, `ModifyContext`, `getFilePath`, `getCurrentContent`, `getProposedContent`, `createUpdatedParams`
- `docs/research/audits/gemini-cli-audit.md` — "Worth Stealing" section

**Key patterns to extract from Gemini CLI:**
- How temp files are created for old vs proposed content
- How the external editor is invoked in diff mode
- How modified content is read back and params are updated
- The `ModifyContext` interface design

### What to Build
Allow users to edit tool arguments (especially edit/write content) in their `$EDITOR` before execution.

**File:** `packages/extensions/tools-extended/src/modifiable-tool.ts` (new)

```typescript
export interface ModifyContext<T> {
  getFilePath(params: T): string
  getCurrentContent(params: T): Promise<string>
  getProposedContent(params: T): string
  createUpdatedParams(current: string, modified: string, original: T): T
}

export function makeModifiable<T extends object>(
  tool: AnyTool,
  context: ModifyContext<T>
): AnyTool
```

**Implementation:**
- Middleware (priority 10) intercepts tools marked as `modifiable: true`
- Creates temp files: `/tmp/ava-edit-{id}-current` and `/tmp/ava-edit-{id}-proposed`
- Opens `$EDITOR` (or `$VISUAL`, fallback `vi`) with both files
- Reads back modified proposed file
- Updates tool params with `createUpdatedParams()`
- Cleans up temp files

**Apply to:** `edit`, `write_file`, `apply_patch` tools

**Desktop integration:** In Tauri, emit `tool:modify-request` event so the UI can show an inline diff editor instead of spawning a terminal editor.

### Tests
- Test: unmodified content passes through unchanged
- Test: modified content updates params correctly
- Test: missing $EDITOR falls back gracefully
- Test: temp files are cleaned up on success and error

---

## Feature 4: Auto-Formatting Detection

### Competitor Research
Read these files and extract the formatter detection pattern:
- `docs/reference-code/cline/src/integrations/editor/DiffViewProvider.ts` — Look for formatter detection, pre/post comparison
- `docs/reference-code/cline/src/core/task/tools/handlers/` — Look for auto-format handling in edit tool handlers
- `docs/research/audits/cline-audit.md` — "Worth Stealing" section on auto-formatting

**Key patterns to extract from Cline:**
- How file state is captured before/after edit
- How formatter-induced changes are detected (timing, comparison)
- How detected changes are reported back to the model
- How cascading match failures from formatting are prevented

### What to Build
Detect when an auto-formatter modifies a file after an edit, and report the formatter's changes back to the model to prevent cascading match failures.

**File:** `packages/extensions/hooks/src/formatter-detection.ts` (new)

```typescript
export interface FormatterChange {
  file: string
  editChanges: string  // What the agent changed
  formatterChanges: string  // What the formatter additionally changed
  formatterName: string  // e.g., "biome", "prettier"
}

export function createFormatterDetectionMiddleware(
  platform: Platform,
  logger: Logger
): ToolMiddleware
```

**Implementation:**
- Priority 51 (runs right after existing formatter middleware at 50)
- `before`: snapshot file content before edit
- `after`: compare post-edit content vs post-formatter content
- If different → compute diff of formatter-only changes
- Inject into tool result metadata: `{ formatterApplied: true, formatterDiff: "..." }`
- Agent sees: "Note: auto-formatter (biome) also made these changes: [diff]"
- This prevents the agent from thinking its edit didn't apply correctly

**Integration:** Works alongside existing `formatter.ts` middleware. The existing middleware runs the formatter; this new middleware detects and reports the delta.

### Tests
- Test: no formatter → no detection metadata
- Test: formatter changes whitespace → reports whitespace diff
- Test: formatter changes nothing → no detection metadata
- Test: metadata is correctly structured for agent consumption

---

## Feature 5: 4-Pass Patch Matcher

### Competitor Research
Read these files and extract the multi-pass matching algorithm:
- `docs/reference-code/cline/src/core/diff/strategies/new-unified/parser.ts` — Look for `findContext()`, 4-pass cascade
- `docs/reference-code/cline/src/core/task/tools/utils/PatchParser.ts` — Look for `findContext()` (lines 259-336), canonicalize, Levenshtein distance, similarity calculation, fuzz levels (0, 1, 100, 1000)
- `docs/research/audits/cline-audit.md` — "Worth Stealing" section on patch matching

**Key patterns to extract from Cline:**
- The 4-pass cascade: exact → rstrip → trim → Levenshtein@66%
- How `fuzz` levels (0, 1, 100, 1000) track match quality
- How `canonicalize()` works
- The Levenshtein similarity calculation and 66% threshold
- How match quality is reported back to the model

### What to Build
Add a 4-pass patch context matcher that complements the existing cascade.

**File:** `packages/extensions/tools-extended/src/edit/four-pass-matcher.ts` (new)

```typescript
export type FuzzLevel = 0 | 1 | 100 | 1000

export interface MatchResult {
  index: number      // Line index in file
  fuzzLevel: FuzzLevel
  similarity: number // 0-1
}

export function findContext(
  lines: string[],
  context: string[],
  startIdx?: number
): MatchResult | null
```

**4 passes (adapted from Cline):**
1. **Pass 1 (fuzz=0):** Exact match after canonicalization (normalize unicode, smart quotes)
2. **Pass 2 (fuzz=1):** Match with `.trimEnd()` on each line (trailing whitespace ignored)
3. **Pass 3 (fuzz=100):** Match with `.trim()` on each line (all leading/trailing whitespace ignored)
4. **Pass 4 (fuzz=1000):** Levenshtein similarity ≥ 0.66 threshold

**Integration:** Add as a new tier in the existing cascade (`cascade.ts`), between `structural` and `fuzzy`. The fuzz level is returned in edit metadata so the agent learns which contexts need more precision.

**Rust hotpath:** The Levenshtein computation in Pass 4 should use `dispatchCompute('compute_levenshtein_similarity', ...)` with TS fallback.

### Tests
- Test: exact match returns fuzz=0
- Test: trailing whitespace diff returns fuzz=1
- Test: leading whitespace diff returns fuzz=100
- Test: 70% similar content returns fuzz=1000
- Test: 50% similar content returns null (below 66% threshold)
- Test: unicode normalization (smart quotes, em dashes)

---

## Feature 6: Windowed File Editing

### Competitor Research
Read these files and extract the windowed viewing pattern:
- `docs/reference-code/swe-agent/tools/windowed/lib/windowed_file.py` — Look for `WindowedFile` class, `window` size, `get_window_text()`, `replace_in_window()`, navigation methods
- `docs/reference-code/swe-agent/sweagent/tools/` — Look for scroll/navigate tool definitions
- `docs/research/audits/swe-agent-audit.md` — "Worth Stealing" section on windowed editing

**Key patterns to extract from SWE-agent:**
- The 100-line window size and why it's optimal
- How edits are scoped to the visible window only
- Navigation tools: open, goto, scroll_up, scroll_down
- Status line format: `[File: path (N lines total)] (M more lines above/below)`
- How the window auto-centers after an edit

### What to Build
Agent mode that restricts file viewing to a sliding window, reducing context usage on large files.

**File:** `packages/extensions/tools-extended/src/windowed-view.ts` (new)

```typescript
export interface WindowState {
  path: string
  firstLine: number
  windowSize: number
  totalLines: number
}

export class WindowedFileView {
  constructor(private windowSize: number = 100)

  open(path: string, startLine?: number): WindowState
  goto(line: number): WindowState
  scrollUp(lines?: number): WindowState
  scrollDown(lines?: number): WindowState
  getWindowText(state: WindowState): string

  /** Format status line: [File: path (N lines)] (M more above) */
  formatStatus(state: WindowState): string
}
```

**New tools to register:**
- `scroll_up` — Move window up by N lines (default: half window)
- `scroll_down` — Move window down by N lines
- `goto_line` — Jump to specific line number
- `view_window` — Show current window with status

**Integration:** Register as an agent mode (`windowed` mode) that:
- Replaces `read_file` output with windowed view for files >200 lines
- Auto-centers window on the edit location after successful edits
- Adds navigation tools to the tool set
- Adds status line to each file view showing position

**Behavior:**
- Files ≤200 lines: show entire file (no windowing)
- Files >200 lines: show 100-line window with navigation
- After edit: re-center window on edited region

### Tests
- Test: small file shows entirely (no windowing)
- Test: large file shows 100-line window with status
- Test: scroll_down advances window correctly
- Test: goto_line centers on target
- Test: edit auto-centers window on changed region
- Test: status line shows correct counts

---

## Post-Implementation Verification

After ALL 6 features are implemented:

1. Run full test suite: `npm run test:run`
2. Run type check: `npx tsc --noEmit`
3. Run linter: `npm run lint`
4. Run format check: `npm run format:check`
5. Verify no files exceed 300 lines
6. Commit with: `git commit -m "feat(sprint-12): edit excellence II — race, streaming fuzzy, modifiable tools, formatter detection, 4-pass matcher, windowed editing"`

---

## File Change Summary

| Action | File |
|--------|------|
| CREATE | `packages/extensions/tools-extended/src/edit/race.ts` |
| CREATE | `packages/extensions/tools-extended/src/edit/race.test.ts` |
| CREATE | `packages/extensions/tools-extended/src/edit/streaming-fuzzy-matcher.ts` |
| CREATE | `packages/extensions/tools-extended/src/edit/streaming-fuzzy-matcher.test.ts` |
| CREATE | `packages/extensions/tools-extended/src/modifiable-tool.ts` |
| CREATE | `packages/extensions/tools-extended/src/modifiable-tool.test.ts` |
| CREATE | `packages/extensions/hooks/src/formatter-detection.ts` |
| CREATE | `packages/extensions/hooks/src/formatter-detection.test.ts` |
| CREATE | `packages/extensions/tools-extended/src/edit/four-pass-matcher.ts` |
| CREATE | `packages/extensions/tools-extended/src/edit/four-pass-matcher.test.ts` |
| CREATE | `packages/extensions/tools-extended/src/windowed-view.ts` |
| CREATE | `packages/extensions/tools-extended/src/windowed-view.test.ts` |
| MODIFY | `packages/extensions/tools-extended/src/edit/cascade.ts` (integrate race + 4-pass) |
| MODIFY | `packages/extensions/tools-extended/src/edit/streaming-edit-parser.ts` (use new matcher) |
| MODIFY | `packages/extensions/hooks/src/index.ts` (register formatter-detection middleware) |
| MODIFY | `packages/extensions/tools-extended/src/index.ts` (register windowed tools) |
| MODIFY | `packages/extensions/agent-modes/src/index.ts` (register windowed mode) |
| MAYBE CREATE | `src-tauri/src/commands/fuzzy_match.rs` (Rust hotpath for streaming fuzzy) |
| MAYBE MODIFY | `src-tauri/src/commands/mod.rs` + `src-tauri/src/lib.rs` (register new commands) |
