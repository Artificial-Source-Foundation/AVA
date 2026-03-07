# Sprint 5: Agent Reliability

**Epic:** B — Deepen
**Duration:** 1 week
**Goal:** Agent doesn't get stuck, recovers from errors, knows when to stop
**Parallel with:** Sprint 6 (Sandbox & Safety)

---

## Competitive Landscape

| Tool | Stuck detection | Error recovery | Completion |
|---|---|---|---|
| **Goose** | RepetitionInspector (detects loops) | Permission + Security inspectors | Final output tool |
| **Gemini CLI** | Error classification (recoverable vs fatal) | LLM self-correction on edit fail | Tool-based |
| **Codex CLI** | Max turns + token budget | Ghost checkpoints + undo | Status = Final |
| **Zed** | Exponential backoff (max 4 retries) | Batch event processing | EndTurn stop reason |
| **OpenCode** | Session loop with `shouldStop` | Error type in tool result | attempt_completion |

**Target:** Goose's inspection system + Gemini's error classification + Codex's checkpoints.

---

## Story 5.1: Stuck Detection (from Goose)

**Reference:** `docs/reference-code/goose/crates/goose/src/agents/tool_inspection.rs`

**What Goose does:**
- `RepetitionInspector`: detects when agent calls same tool with same args repeatedly
- `SecurityInspector`: catches prompt injection patterns
- `PermissionInspector`: approval flow for dangerous operations
- All run as middleware before tool execution

**What to build:**

`packages/extensions/agent-modes/src/stuck-detection.ts`:

```typescript
const stuckDetector = {
  recentCalls: [] as ToolCall[],

  check(call: ToolCall): StuckStatus {
    // Same tool + same args > 2 times in last 5 calls
    const duplicates = this.recentCalls.filter(c =>
      c.name === call.name && JSON.stringify(c.arguments) === JSON.stringify(call.arguments)
    )
    if (duplicates.length >= 2) return 'stuck-repeat'

    // No file modifications in > 5 turns
    if (this.turnsWithoutFileChange > 5) return 'stuck-spinning'

    // Token budget > 90% with no completion signal
    if (this.tokenUsage > 0.9 * this.tokenLimit) return 'stuck-budget'

    return 'ok'
  },

  resolve(status: StuckStatus): string {
    switch (status) {
      case 'stuck-repeat': return 'You are repeating the same action. Try a different approach.'
      case 'stuck-spinning': return 'You have not modified any files in 5 turns. Either make progress or use attempt_completion.'
      case 'stuck-budget': return 'Context is nearly full. Summarize progress and complete or compact.'
    }
  }
}
```

Register as tool middleware at priority 5 (before permissions):
- On stuck: inject steering message into context
- On persistent stuck (2 steering messages ignored): force compact + ask user

**Acceptance criteria:**
- [ ] Detects repeated tool calls (same name + args)
- [ ] Detects spinning (no file changes in 5+ turns)
- [ ] Detects budget exhaustion (>90% tokens)
- [ ] Injects steering message that changes agent behavior
- [ ] Agent breaks out of loops in test scenarios

---

## Story 5.2: Error Recovery with Validation (from Gemini CLI)

**Reference:** `docs/reference-code/gemini-cli/packages/core/src/tools/tool-error.ts`

**What Gemini does:**
- Classifies errors as **recoverable** vs **fatal**
- Recoverable: `EDIT_NO_OCCURRENCE`, `FILE_NOT_FOUND`, `INVALID_PARAMS` → LLM retries
- Fatal: `NO_SPACE_LEFT` → exit
- Edit failures trigger `attemptSelfCorrection()` (LLM re-generates search/replace)

**What to build:**

`packages/extensions/hooks/src/error-recovery.ts`:

Wire the Rust `ava-validator` + `ava-agent` reflection loop into the TS agent:

```typescript
api.addToolMiddleware({
  name: 'error-recovery',
  priority: 15,
  after: async (call, result) => {
    if (!result.success && isEditTool(call.name)) {
      // Step 1: Validate via Rust
      const validation = await dispatchCompute('validation_validate_edit', {
        content: result.output
      }, () => tsValidate(result.output))

      // Step 2: If invalid, try Rust reflection
      if (!validation.valid) {
        const fixed = await dispatchCompute('reflection_reflect_and_fix', {
          error: validation.details,
          tool_result: result
        }, () => tsReflect(validation, result))

        if (fixed.success) return fixed
      }
    }
    return result
  }
})
```

Max 3 retries per edit, then surface error to user.

**Acceptance criteria:**
- [ ] Failed edits auto-retry with validation
- [ ] Rust validation + reflection used in desktop
- [ ] Max 3 retries prevents infinite loops
- [ ] Error classification (recoverable vs fatal) works

---

## Story 5.3: Completion Detection

**Reference:** Multiple — all competitors use `attempt_completion` or similar.

**Current problem:** Agent sometimes doesn't call `attempt_completion` and keeps going.

**What to build:**

`packages/extensions/agent-modes/src/completion-detection.ts`:

```typescript
function detectImplicitCompletion(messages: Message[]): boolean {
  const last = messages[messages.length - 1]
  if (!last) return false

  // No tool calls in response = likely done
  if (last.role === 'assistant' && (!last.tool_calls || last.tool_calls.length === 0)) {
    // Check if message looks like a summary
    const summaryPatterns = [
      /I've (completed|finished|implemented|fixed)/i,
      /The (changes|updates|fix) (are|is) (now|ready)/i,
      /Let me know if/i,
    ]
    return summaryPatterns.some(p => p.test(last.content))
  }
  return false
}
```

When implicit completion detected:
1. Run validator on all modified files
2. If validation passes → auto-complete
3. If validation fails → inject "please fix these issues" message

**Acceptance criteria:**
- [ ] Detects when agent is implicitly done
- [ ] Validates modified files before completing
- [ ] Auto-completes on success, steers on failure
