# Sprint 15: UX & Desktop Polish — Implementation Prompt

> For AI coding agent. Estimated: 5 features, mix S/M effort.
> Note: Original item 4 (9 model roles) is absorbed into Sprint 17 (Praxis v2 configurable model defaults).
> Run `npm run test:run && npx tsc --noEmit` after each feature.

---

## Role

You are implementing Sprint 15 (UX & Desktop Polish) for AVA, a multi-agent AI coding assistant.

Read these files first:
- `CLAUDE.md` (conventions, architecture, dispatchCompute pattern)
- `AGENTS.md` (code standards, common workflows)

**IMPORTANT**: The frontend uses **SolidJS** (NOT React). Use `createSignal`, `Show`, `For`, `onCleanup` — zero React patterns.

---

## Pre-Implementation: Competitor Research Phase

**CRITICAL**: Before implementing each feature, you MUST read the relevant competitor reference code and extract best patterns.

For EACH feature:
1. **Read** the listed competitor reference files
2. **Extract** key patterns
3. **Adapt** to AVA's SolidJS + TypeScript architecture
4. **Implement** (<300 lines/file, no `any`)
5. **Test** + verify

---

## Feature 1: Differential TUI Rendering (CLI)

### Competitor Research
Read these files:
- `docs/reference-code/pi-mono/packages/coding-agent/src/` — Look for terminal rendering, ANSI output, synchronized updates
- `docs/research/audits/pi-mono-audit.md` — "Worth Stealing" section on TUI

### What to Build
Flicker-free CLI output by buffering and diffing terminal frames before rendering.

**File:** `cli/src/rendering/diff-renderer.ts` (new)

```typescript
export class DiffRenderer {
  private previousFrame: string[] = []

  /** Render a new frame, only outputting changed lines */
  render(lines: string[]): void

  /** Force full redraw */
  forceRedraw(): void

  /** Clear screen and reset state */
  clear(): void
}
```

**Implementation:**
- Buffer the entire output as array of lines
- On each render, diff against previous frame
- Only write changed lines using ANSI cursor positioning (`\x1b[{row};{col}H`)
- Use `\x1b[?25l` to hide cursor during update, `\x1b[?25h` to show after
- Handle terminal resize via `process.stdout.on('resize')`
- Fallback: if terminal doesn't support cursor positioning, full redraw

**Integration:** Use in `cli/src/commands/run.ts` for agent output display.

### Tests
- `cli/src/rendering/diff-renderer.test.ts`
- Test: identical frames produce no output
- Test: single line change only outputs that line
- Test: resize triggers full redraw
- Test: ANSI escape sequences are correct

---

## Feature 2: Web Trajectory Inspector

### Competitor Research
Read these files:
- `docs/reference-code/swe-agent/sweagent/` — Look for trajectory viewer, session replay
- `docs/research/audits/swe-agent-audit.md` — "Worth Stealing" section on trajectory inspector

### What to Build
A session replay viewer in the desktop app that shows the full agent execution timeline.

**File:** `src/components/panels/TrajectoryInspector.tsx` (new)

```typescript
interface TrajectoryInspectorProps {
  sessionId: string
}

// SolidJS component
export function TrajectoryInspector(props: TrajectoryInspectorProps): JSX.Element
```

**Implementation:**
- Timeline view showing every agent event in chronological order
- Event types rendered differently:
  - `turn:start/end` → numbered turn markers
  - `tool:start/finish` → tool call cards with args, result, duration
  - `thought/thinking` → collapsible thinking blocks
  - `delegation:start/complete` → nested delegation timeline
  - `error` → red error cards
  - `doom-loop/stuck:detected` → warning markers
- Filter bar: filter by event type, agent ID, time range
- Click event to expand full payload
- Export button: download session as JSON

**SolidJS patterns:**
- Use `createSignal` for filter state
- Use `For` to render event list
- Use `Show` for conditional rendering (expanded events)
- Use `onCleanup` for event listener cleanup
- Virtual scrolling for large sessions (use `@solid-primitives/virtual` if available, otherwise simple windowing)

**Integration:** Add as a new panel in `src/components/panels/`. Register in panel router. Accessible via session context menu → "View Trajectory".

### Tests
- `src/components/panels/TrajectoryInspector.test.tsx`
- Test: renders timeline from mock events
- Test: filter by event type works
- Test: expand/collapse event details
- Test: export produces valid JSON

---

## Feature 3: Model-Variant System Prompts

### Competitor Research
Read these files:
- `docs/reference-code/gemini-cli/packages/core/src/` — Look for model-specific system prompts, provider-aware prompt selection
- `docs/research/audits/gemini-cli-audit.md` — "Worth Stealing" section on model-variant prompts

### What to Build
Different system prompt sections per model family. Each model has different strengths and quirks.

**File:** `packages/extensions/prompts/src/model-variants.ts` (new)

```typescript
export type ModelFamily = 'claude' | 'gpt' | 'gemini' | 'llama' | 'mistral' | 'other'

export interface ModelVariantPrompt {
  family: ModelFamily
  toolCallGuidance: string    // How this model handles tool calls
  formattingHints: string     // Output format preferences
  thinkingMode?: string       // Extended thinking instructions (Claude)
  structuredOutput?: string   // JSON mode guidance (GPT)
}

/** Detect model family from model ID string */
export function detectModelFamily(modelId: string): ModelFamily

/** Get variant-specific prompt additions */
export function getModelVariantPrompt(modelId: string): ModelVariantPrompt
```

**Model-specific guidance:**
- **Claude**: Prefers XML tool results, supports extended thinking, handles long context well
- **GPT**: Strict JSON tool call format, benefits from explicit "respond with JSON" instructions
- **Gemini**: Native Google Search grounding, large context (1M+), benefits from structured examples
- **Llama/Mistral**: Simpler tool call format, may need more explicit instructions, shorter context
- **Other**: Generic safe defaults

**Integration:** Hook into `prompt:build` event in `packages/extensions/prompts/src/builder.ts`. Append variant-specific sections based on current model.

### Tests
- `packages/extensions/prompts/src/model-variants.test.ts`
- Test: detectModelFamily correctly identifies claude-*, gpt-*, gemini-*, llama-*, mistral-*
- Test: unknown model returns 'other'
- Test: variant prompt includes model-specific guidance
- Test: prompt:build hook appends variant sections

---

## Feature 4: File Watcher with Comment Prompts

### Competitor Research
Read these files:
- `docs/reference-code/aider/aider/watch.py` — Look for file watching, comment detection, auto-trigger
- `docs/research/audits/aider-audit.md` — "Worth Stealing" section on file watcher

### What to Build
Watch project files for special comments like `// ava: fix this` and auto-trigger the agent.

**File:** `packages/extensions/hooks/src/file-watcher/comment-watcher.ts` (new)

```typescript
export interface CommentTrigger {
  file: string
  line: number
  comment: string      // e.g., "ava: fix this function"
  surrounding: string  // 5 lines of context around the comment
}

export class CommentWatcher {
  private patterns: RegExp[]

  constructor(patterns?: string[])

  /** Scan a file for trigger comments */
  scan(filePath: string, content: string): CommentTrigger[]

  /** Process a file change event */
  onFileChanged(filePath: string): Promise<CommentTrigger[]>
}
```

**Trigger patterns (default):**
- `// ava: <instruction>` or `# ava: <instruction>` or `/* ava: <instruction> */`
- `// TODO(ava): <instruction>`
- `// FIXME(ava): <instruction>`

**Implementation:**
- Extend existing file watcher in `packages/extensions/hooks/src/file-watcher/`
- On file change, scan for trigger comments
- If found: emit `comment:trigger` event with `CommentTrigger` payload
- UI shows notification: "Found 'ava: fix this' in app.ts:42. Run agent?"
- User confirms → agent runs with the comment as goal + surrounding context
- After agent completes, remove the trigger comment from the file
- Config: `hooks.commentWatcher.enabled` (default: true), `hooks.commentWatcher.patterns` (customizable)

**Integration:** Register in `packages/extensions/hooks/src/file-watcher/index.ts`.

### Tests
- `packages/extensions/hooks/src/file-watcher/comment-watcher.test.ts`
- Test: detects `// ava: fix this` pattern
- Test: detects `# ava: fix this` pattern (Python)
- Test: detects `// TODO(ava): implement` pattern
- Test: ignores regular comments without ava prefix
- Test: returns correct line number and surrounding context
- Test: custom patterns work

---

## Feature 5: Recipe/Workflow Sharing

### Competitor Research
Read these files:
- `docs/reference-code/goose/crates/goose/src/` — Look for recipe/workflow system, YAML definitions
- `docs/research/audits/goose-audit.md` — "Worth Stealing" section on recipes

### What to Build
Shareable YAML workflows that define multi-step agent tasks.

**File:** `packages/extensions/slash-commands/src/recipes.ts` (new)

```typescript
export interface Recipe {
  name: string
  description: string
  version: string
  author?: string
  steps: RecipeStep[]
}

export interface RecipeStep {
  name: string
  goal: string
  mode?: 'full' | 'light' | 'solo'  // Praxis mode
  tools?: string[]                    // restrict tools for this step
  dependsOn?: string[]               // step names this depends on
}

/** Parse a recipe from YAML string */
export function parseRecipe(yaml: string): Recipe

/** Execute a recipe */
export async function executeRecipe(
  recipe: Recipe,
  context: ToolContext,
  onProgress?: (step: string, status: string) => void
): Promise<RecipeResult>

/** Load recipes from ~/.ava/recipes/ and .ava/recipes/ */
export function discoverRecipes(): Promise<Recipe[]>
```

**Implementation:**
- Recipes are YAML files in `~/.ava/recipes/` (global) or `.ava/recipes/` (project)
- Slash command: `/recipe list`, `/recipe run <name>`, `/recipe create`
- Steps execute sequentially respecting `dependsOn` ordering
- Each step invokes the agent with the step's goal
- Progress emitted via events for UI display
- Recipe sharing: export as YAML file, import via file drop or URL

**Example recipe:**
```yaml
name: add-feature
description: Standard feature implementation workflow
version: "1.0"
steps:
  - name: research
    goal: "Research the codebase for relevant files and patterns for: {input}"
    mode: solo
  - name: implement
    goal: "Implement the feature based on research findings"
    mode: light
    dependsOn: [research]
  - name: test
    goal: "Write tests for the implemented feature"
    mode: light
    dependsOn: [implement]
  - name: review
    goal: "Review all changes for quality and conventions"
    mode: solo
    dependsOn: [test]
```

**Integration:** Register slash commands in `packages/extensions/slash-commands/src/index.ts`. Use `js-yaml` for YAML parsing (already in dependencies, check first).

### Tests
- `packages/extensions/slash-commands/src/recipes.test.ts`
- Test: parse valid YAML recipe
- Test: reject invalid recipe (missing required fields)
- Test: step dependency ordering works
- Test: discover recipes from filesystem
- Test: recipe execution calls agent per step

---

## Post-Implementation Verification

After ALL 5 features:

1. `npm run test:run`
2. `npx tsc --noEmit`
3. `npm run lint`
4. `npm run format:check`
5. Verify no files exceed 300 lines
6. Commit: `git commit -m "feat(sprint-15): UX & desktop polish — diff renderer, trajectory inspector, model variants, comment watcher, recipes"`

---

## File Change Summary

| Action | File |
|--------|------|
| CREATE | `cli/src/rendering/diff-renderer.ts` |
| CREATE | `cli/src/rendering/diff-renderer.test.ts` |
| MODIFY | `cli/src/commands/run.ts` (use diff renderer) |
| CREATE | `src/components/panels/TrajectoryInspector.tsx` |
| CREATE | `src/components/panels/TrajectoryInspector.test.tsx` |
| MODIFY | `src/components/panels/` (register in panel router) |
| CREATE | `packages/extensions/prompts/src/model-variants.ts` |
| CREATE | `packages/extensions/prompts/src/model-variants.test.ts` |
| MODIFY | `packages/extensions/prompts/src/builder.ts` (hook model variants) |
| CREATE | `packages/extensions/hooks/src/file-watcher/comment-watcher.ts` |
| CREATE | `packages/extensions/hooks/src/file-watcher/comment-watcher.test.ts` |
| MODIFY | `packages/extensions/hooks/src/file-watcher/index.ts` (register comment watcher) |
| CREATE | `packages/extensions/slash-commands/src/recipes.ts` |
| CREATE | `packages/extensions/slash-commands/src/recipes.test.ts` |
| MODIFY | `packages/extensions/slash-commands/src/index.ts` (register /recipe commands) |
