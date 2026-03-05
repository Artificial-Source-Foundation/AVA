# Sprint 4: Context Intelligence

**Epic:** B — Deepen
**Duration:** 1 week
**Goal:** Send the right code to the LLM — PageRank repo map + multi-strategy compaction
**Parallel with:** Sprint 3 (Edit Excellence)

---

## Competitive Landscape

| Tool | Context strategy | Key technique |
|---|---|---|
| **Aider** | PageRank + tree-sitter symbol extraction | Personalized graph ranking |
| **Zed** | MentionUri context (files, symbols, selections) | Rich user context |
| **Cline** | Sliding window + model-specific thresholds | Buffer calculation |
| **OpenCode** | Compaction agent + file truncation | Hidden agent summarizes |
| **Goose** | Code Mode (lazy tool discovery) | 3 meta-tools instead of 50 |

**Target:** Aider's PageRank + Cline's compaction + OpenCode's agent-based summarization.

---

## Story 4.1: PageRank Repo Map (from Aider)

**Reference:** `docs/reference-code/aider/aider/repomap.py` (~850 lines)

**What Aider does:**
1. Tree-sitter extracts definitions + references from all files
2. Builds graph: `(referencing_file) → (defining_file)` with weighted edges
3. Weights: 50x for files in chat, 10x for mentioned identifiers, 0.1x for `_private`
4. Runs PageRank (personalized to active files)
5. Binary search to fit max ranked tags within token budget
6. Renders `TreeContext` showing function/class signatures

**What to build:**

The Rust `ava-codebase` crate already has BM25 + PageRank + dependency graph.
Wire it to the TS context extension:

`packages/extensions/context/src/repo-map.ts`:
```typescript
export async function buildRepoMap(
  activeFiles: string[],
  mentionedIdentifiers: string[],
  maxTokens: number
): Promise<RankedFile[]> {
  return dispatchCompute('compute_repo_map', {
    active_files: activeFiles,
    mentioned_identifiers: mentionedIdentifiers,
    max_tokens: maxTokens
  }, () => tsRepoMapFallback(activeFiles, mentionedIdentifiers, maxTokens))
}
```

**Expose new Tauri command** `compute_repo_map` in `src-tauri/src/commands/`:
- Call `ava-codebase` crate: build graph, run PageRank, return ranked files
- Accept: active files, mentioned identifiers, token budget
- Return: `Vec<RankedFile { path, score, symbols }>` sorted by rank

**Weights (from Aider, tuned):**
- 50x if file is in active conversation
- 10x if identifier is explicitly mentioned
- 10x if identifier is long (>=8 chars) snake/camel case
- 0.1x if identifier starts with `_`
- 0.1x if defined in >5 files (too generic)
- sqrt(count) for reference frequency (dampen high-frequency)

**Acceptance criteria:**
- [ ] `compute_repo_map` Tauri command works
- [ ] Context extension uses PageRank-ranked files
- [ ] Token budget respected (binary search fitting)
- [ ] Measurably better context relevance (manual QA)

---

## Story 4.2: Multi-Strategy Compaction (from Cline + OpenCode)

**Reference (Cline):** `docs/reference-code/cline/src/core/context/context-management/ContextManager.ts`
**Reference (OpenCode):** `docs/reference-code/opencode/packages/opencode/src/agent/agent.ts` (compaction agent)

**What Cline does:**
- Model-specific thresholds: 64K window → 37K max, 128K → 98K, 200K → 160K
- Triggers compaction when token count exceeds threshold
- Tracks context history with timestamps for undo

**What OpenCode does:**
- Hidden "compaction" agent with no tools
- Summarizes old messages into condensed form
- Reduces message count while preserving key information

**What to build in TS context extension:**

`packages/extensions/context/src/compaction-cascade.ts`:

**Tier 1: Tool output truncation** (no LLM cost)
- Truncate tool results > 2000 chars to first 500 + last 500 chars
- Already partially in Rust `ava-context` condenser

**Tier 2: Sliding window** (no LLM cost)
- Drop oldest user/assistant message pairs
- Keep system prompt + last N turns
- Threshold: Cline-style model-specific buffers

**Tier 3: LLM summarization** (costs tokens but preserves info)
- Send old messages to fast/cheap model (e.g., Haiku)
- Replace with single summary message
- Keep: file paths modified, key decisions, current goal
- Reference OpenCode's compaction agent prompt

**Token budget allocation:**
```
System prompt:       15% of context window
Repo map:            20%
Conversation history: 50%
Tool results:        15%
```

**Acceptance criteria:**
- [ ] 3-tier compaction cascade works
- [ ] Model-specific thresholds (per Cline's logic)
- [ ] LLM summarization preserves critical information
- [ ] Agent can run >50 turns without context overflow

---

## Story 4.3: LSP Diagnostics in Tool Output (from OpenCode)

**Reference:** `docs/reference-code/opencode/packages/opencode/src/lsp/`

**What OpenCode does:**
After every edit/write tool execution, it runs LSP diagnostics and appends errors:
```
Edit applied successfully.

LSP errors detected in this file, please fix:
<diagnostics file="src/foo.ts">
  line 10: Cannot assign to readonly property
  line 25: Type 'string' is not assignable to type 'number'
</diagnostics>
```

This makes the agent self-correct immediately instead of discovering errors later.

**What to build:**
In the edit and write tool middleware, after successful execution:
1. Call LSP diagnostics for the modified file (already have 9 LSP tools)
2. If errors found, append to tool result
3. Agent sees errors in context and fixes them in the same turn

`packages/extensions/hooks/src/lsp-after-edit.ts`:
```typescript
api.addToolMiddleware({
  name: 'lsp-diagnostics',
  priority: 20,
  after: async (call, result) => {
    if (['edit', 'write_file', 'create_file'].includes(call.name)) {
      const diagnostics = await getLspDiagnostics(call.arguments.path)
      if (diagnostics.length > 0) {
        result.output += formatDiagnostics(diagnostics)
      }
    }
    return result
  }
})
```

**Acceptance criteria:**
- [ ] LSP diagnostics appended after edit/write
- [ ] Agent self-corrects type errors in same turn
- [ ] No performance regression (LSP call is async, non-blocking)
