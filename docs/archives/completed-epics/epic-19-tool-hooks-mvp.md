# Epic 19: Tool Hooks & MVP Polish

> Add tool lifecycle hooks and polish missing MVP features based on Cline comparison

**Status**: Complete (implemented in core)
**Estimated Lines**: ~2,500
**Dependencies**: Epics 1-17 (all complete)

---

## Goals

1. **Tool Hooks System** - Let operators/workers customize tool behavior via lifecycle hooks
2. **MVP Gaps** - Fill missing features identified from Cline comparison
3. **System Prompt Improvements** - Better tool descriptions and rules

---

## Implementation Status

Implemented in:
- `packages/core/src/hooks/` (tool lifecycle hooks)
- `packages/core/src/tools/completion.ts` (attempt_completion)
- `packages/core/src/tools/sanitize.ts` (model content fixes)
- `packages/core/src/agent/prompts/variants/` (model-specific prompts)

Remaining gaps:
- Workspace-level ignore config (Cline’s `.clineignore` equivalent)
- UI wiring for approvals, diffs, and metadata streams

---

## Analysis: What We're Missing vs Cline

### Tool System Gaps

| Feature | Cline | AVA | Priority |
|---------|-------|--------|----------|
| Tool hooks (PreToolUse, PostToolUse) | ✅ | ✅ | High |
| `requires_approval` self-report | ✅ | ✅ | High |
| `consecutiveMistakeCount` / doom loop | ✅ | ✅ | Medium |
| Model-specific content fixes | ✅ | ✅ | Medium |
| `.clineignore` / file exclusion | ✅ | ❌ | Medium |
| `attempt_completion` tool | ✅ | ✅ | High |
| `task_progress` tracking in prompts | ✅ | Via todoread/write | Low |

### System Prompt Gaps

| Feature | Cline | AVA | Priority |
|---------|-------|--------|----------|
| RULES section with detailed guidance | ✅ 10k | ✅ | High |
| CAPABILITIES section | ✅ | ✅ | Medium |
| Model-family variants (GPT, Claude, Gemini) | ✅ | ✅ | Low |
| Environment details injection | ✅ | ✅ | Medium |

---

## Sprint Plan

### Sprint 19.1: Tool Hooks Core (~800 lines)

**Goal**: Implement hook system that operators can use to customize tool behavior.

**Files to create**:
- `packages/core/src/hooks/types.ts` (~100 lines)
- `packages/core/src/hooks/executor.ts` (~250 lines)
- `packages/core/src/hooks/factory.ts` (~200 lines)
- `packages/core/src/hooks/index.ts` (~50 lines)

**Hook Types**:
```typescript
interface Hooks {
  PreToolUse: {
    toolName: string
    parameters: Record<string, unknown>
  }
  PostToolUse: {
    toolName: string
    parameters: Record<string, unknown>
    result: ToolResult
    success: boolean
  }
  TaskStart: { goal: string }
  TaskComplete: { success: boolean; output: string }
  TaskCancel: { reason: string }
}

interface HookResult {
  cancel?: boolean
  contextModification?: string
  errorMessage?: string
}
```

**Hook Execution Flow**:
```
Tool Execute Request
       │
       ▼
  [PreToolUse Hook]
       │
       ├─→ cancel: true → Return error, skip tool
       │
       ├─→ contextModification → Inject into conversation
       │
       ▼
  [Actual Tool Execution]
       │
       ▼
  [PostToolUse Hook]
       │
       ├─→ contextModification → Inject into conversation
       │
       ▼
  Return Result
```

**Hook Discovery**:
- Global: `~/.ava/hooks/{HookName}` (executable script)
- Project: `.ava/hooks/{HookName}` (executable script)
- Both run if present (global first, then project)

**Hook Protocol**:
- Input: JSON via stdin
- Output: JSON via stdout
- Timeout: 30 seconds
- Non-zero exit = error (but doesn't block tool unless `cancel: true`)

---

### Sprint 19.2: Tool Hook Integration (~400 lines)

**Goal**: Integrate hooks into tool execution and agent loop.

**Files to modify**:
- `packages/core/src/tools/registry.ts` - Add hook execution
- `packages/core/src/agent/loop.ts` - Add TaskStart/Complete/Cancel hooks
- `packages/core/src/commander/executor.ts` - Hooks for worker delegation

**Hook Usage Example (Operator)**:

```bash
#!/bin/bash
# .ava/hooks/PreToolUse - Run linter before file writes

INPUT=$(cat)
TOOL=$(echo "$INPUT" | jq -r '.toolName')
PATH=$(echo "$INPUT" | jq -r '.parameters.path // ""')

if [[ "$TOOL" == "write" || "$TOOL" == "create" || "$TOOL" == "edit" ]]; then
  # Inject reminder into context
  echo '{"contextModification": "Remember: Run npm run lint after making changes."}'
else
  echo '{"cancel": false}'
fi
```

```bash
#!/bin/bash
# .ava/hooks/PostToolUse - Auto-run linter after file edits

INPUT=$(cat)
TOOL=$(echo "$INPUT" | jq -r '.toolName')
SUCCESS=$(echo "$INPUT" | jq -r '.success')

if [[ "$SUCCESS" == "true" && ("$TOOL" == "write" || "$TOOL" == "edit") ]]; then
  # Run linter in background, inject results
  LINT_OUTPUT=$(npm run lint 2>&1 | head -20)
  echo "{\"contextModification\": \"Lint results:\\n$LINT_OUTPUT\"}"
else
  echo '{}'
fi
```

---

### Sprint 19.3: requires_approval & Content Fixes (~500 lines)

**Goal**: Add self-reported risk assessment and model content sanitization.

**19.3.1: requires_approval Parameter**

Add to bash tool:
```typescript
input_schema: {
  properties: {
    // ... existing
    requires_approval: {
      type: 'boolean',
      description: 'Set to true for risky operations (install packages, delete files, system changes). Set to false for safe operations (read, build, test).'
    }
  }
}
```

Modify permission check:
```typescript
// In bashTool.execute()
if (params.requires_approval && autoApprovalEnabled) {
  // Still require approval even in auto-approve mode
  const approved = await context.permissions.requestApproval(...)
  if (!approved) return { error: 'User denied risky command' }
}
```

**19.3.2: Content Sanitization**

```typescript
// packages/core/src/tools/sanitize.ts (~150 lines)

export function sanitizeContent(content: string, modelId?: string): string {
  let result = content

  // Strip markdown fences (common with Gemini, DeepSeek, Llama)
  if (result.startsWith('```')) {
    result = result.split('\n').slice(1).join('\n').trim()
  }
  if (result.endsWith('```')) {
    result = result.split('\n').slice(0, -1).join('\n').trim()
  }

  // Model-specific fixes
  if (modelId?.includes('gemini')) {
    // Gemini sometimes escapes things oddly
    result = result.replace(/\\n/g, '\n')
  }

  if (modelId?.includes('deepseek')) {
    // DeepSeek uses HTML entities in code
    result = result.replace(/&amp;&amp;/g, '&&')
    result = result.replace(/&lt;/g, '<')
    result = result.replace(/&gt;/g, '>')
  }

  return result
}
```

Apply in write/create/edit tools before writing files.

---

### Sprint 19.4: attempt_completion Tool (~400 lines)

**Goal**: Add tool for LLM to signal task completion with result summary.

**File**: `packages/core/src/tools/completion.ts`

```typescript
export const completionTool: Tool<CompletionParams> = {
  definition: {
    name: 'attempt_completion',
    description: `Signal that the task is complete. Use this after confirming all tool operations succeeded. Provide a clear result summary. The user may provide feedback for improvements.

IMPORTANT: Only use this tool after:
1. All requested changes are made
2. All tool results confirmed successful
3. No pending operations remain

Do NOT use this tool if any tool failed or if more work is needed.`,
    input_schema: {
      type: 'object',
      properties: {
        result: {
          type: 'string',
          description: 'Clear, concise summary of what was accomplished (1-2 paragraphs)'
        },
        command: {
          type: 'string',
          description: 'Optional CLI command to demonstrate the result (e.g., "npm run dev", "open index.html")'
        }
      },
      required: ['result']
    }
  },

  async execute(params, context): Promise<ToolResult> {
    // Run PostToolUse hooks
    // Signal to agent loop that completion is attempted
    context.session.setCompletionAttempted(true)

    return {
      output: `Task completion attempted.\n\nResult: ${params.result}${
        params.command ? `\n\nDemo command: ${params.command}` : ''
      }`
    }
  }
}
```

Modify agent loop to recognize completion attempts and handle user feedback loop.

---

### Sprint 19.5: System Prompt Enhancement (~400 lines)

**Goal**: Improve system prompt with RULES and CAPABILITIES sections.

**File**: `packages/core/src/agent/prompts/system.ts`

**RULES Section** (key points from Cline, adapted):
```typescript
const RULES = `
## RULES

- Working directory is {{CWD}}. Use absolute paths or paths relative to this.
- Do not use ~ or $HOME. Always use absolute paths.
- Before executing commands, consider the user's OS and tailor commands accordingly.
- When making code changes, ensure compatibility with existing codebase patterns.
- Use search tools (grep, glob) to understand context before making changes.
- After making changes, verify they work (run tests, lint, build).
- Do NOT start responses with "Great", "Certainly", "Sure". Be direct and technical.
- Wait for tool results before proceeding. Confirm each step succeeded.
- When task is complete, use attempt_completion to present the final result.
- Do NOT ask questions unless absolutely necessary. Use tools to find answers.
`
```

**CAPABILITIES Section**:
```typescript
const CAPABILITIES = `
## CAPABILITIES

You have access to 19 tools:

**File Operations**: read_file (view contents), create_file (new file), write_file (overwrite),
  delete_file (remove), edit (modify), glob (find files), grep (search content), ls (list directory)

**Execution**: bash (run commands), browser (web automation)

**Task Management**: todoread (view tasks), todowrite (update tasks), task (spawn subagent)

**Communication**: question (ask user), attempt_completion (finish task)

**Web**: websearch (search web), webfetch (fetch URL content)

**Modes**: plan_enter, plan_exit

Use these tools to accomplish tasks efficiently. Prefer tools over asking questions.
`
```

---

## Summary

| Sprint | Focus | Lines |
|--------|-------|-------|
| 19.1 | Hook system core | ~800 |
| 19.2 | Hook integration | ~400 |
| 19.3 | requires_approval + sanitization | ~500 |
| 19.4 | attempt_completion tool | ~400 |
| 19.5 | System prompt improvements | ~400 |
| **Total** | | **~2,500** |

---

## Success Criteria

- [ ] Hooks execute before/after tool use
- [ ] Hooks can cancel operations or inject context
- [ ] `requires_approval` prevents auto-approve for risky commands
- [ ] Content sanitization strips markdown fences
- [ ] `attempt_completion` signals task done
- [ ] System prompt includes RULES and CAPABILITIES
- [ ] All existing tests pass
- [ ] Hook examples documented

---

## Future Considerations (Not in this Epic)

- `.avaignore` file support (like .clineignore)
- Model-family prompt variants
- Environment details auto-injection
- Browser automation tool
- Task progress in attempt_completion
