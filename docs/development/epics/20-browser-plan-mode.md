# Epic 20: Browser, Plan Mode & Safety

> Browser automation, read-only analysis mode, and safety improvements

**Status**: Planning
**Estimated Lines**: ~2,000
**Dependencies**: Epic 19 (Tool Hooks & MVP Polish)

---

## Goals

1. **Browser Automation** - Puppeteer-based browser tool for web testing
2. **Plan Mode** - Read-only analysis mode with tool restrictions
3. **Safety Improvements** - Doom loop detection, auto-approval, error handling

---

## Analysis: Key Features from Cline & OpenCode

### From Cline
- Browser automation with Puppeteer (6 actions)
- Screenshot capture with WebP encoding
- Console log capture
- Path-aware auto-approval (local vs external)
- Yolo mode toggle
- apply_patch tool for unified diffs

### From OpenCode
- Doom loop detection (3x identical tool calls)
- CorrectedError pattern (continue with user feedback)
- Plan mode with read-only tool restrictions

---

## Sprint Plan

### Sprint 20.1: Browser Tool (~700 lines)

**Goal**: Implement browser automation tool with Puppeteer.

**Files to create**:
- `packages/core/src/tools/browser/session.ts` (~300 lines)
- `packages/core/src/tools/browser/actions.ts` (~200 lines)
- `packages/core/src/tools/browser/index.ts` (~200 lines)

**Browser Actions**:
```typescript
type BrowserAction = 'launch' | 'click' | 'type' | 'scroll_down' | 'scroll_up' | 'close'

interface BrowserActionResult {
  screenshot?: string  // data:image/webp;base64,...
  logs?: string        // Console output
  currentUrl?: string
  currentMousePosition?: string  // "x,y"
}
```

**Browser Tool Parameters**:
```typescript
interface BrowserToolParams {
  action: BrowserAction
  url?: string       // For 'launch'
  coordinate?: string // For 'click' (e.g., "450,300")
  text?: string      // For 'type'
}
```

**Key Implementation Details**:
- Headless mode by default
- WebP screenshots (better compression)
- 600px scroll increments
- Console log capture with 3s timeout
- Viewport: 900x600 default (configurable)

---

### Sprint 20.2: Plan Mode (~400 lines)

**Goal**: Read-only analysis mode with tool restrictions.

**Files to create/modify**:
- `packages/core/src/agent/modes/plan.ts` (~200 lines)
- `packages/core/src/agent/loop.ts` - Add mode checking
- `packages/core/src/tools/registry.ts` - Tool filtering

**Plan Mode Features**:
```typescript
interface PlanModeConfig {
  enabled: boolean
  allowedTools: string[]  // ['read', 'glob', 'grep', 'ls']
  restrictedTools: string[]  // ['write', 'create', 'delete', 'bash', 'edit']
}

// Tool restriction check
function isPlanModeToolRestricted(toolName: string): boolean {
  if (!planModeEnabled) return false
  return PLAN_MODE_RESTRICTED_TOOLS.includes(toolName)
}
```

**Mode Switching**:
- `plan_enter` tool - Enter read-only mode
- `plan_exit` tool - Exit to normal mode
- System prompt updates based on mode

---

### Sprint 20.3: Doom Loop Detection (~300 lines)

**Goal**: Detect and handle infinite tool loops.

**Files to modify**:
- `packages/core/src/agent/loop.ts` - Add detection logic
- `packages/core/src/agent/types.ts` - Add tracking types

**Detection Algorithm**:
```typescript
const DOOM_LOOP_THRESHOLD = 3

interface ToolCallRecord {
  toolName: string
  input: Record<string, unknown>
  timestamp: number
}

function detectDoomLoop(history: ToolCallRecord[]): boolean {
  const lastThree = history.slice(-DOOM_LOOP_THRESHOLD)

  if (lastThree.length < DOOM_LOOP_THRESHOLD) return false

  const first = lastThree[0]
  return lastThree.every(call =>
    call.toolName === first.toolName &&
    JSON.stringify(call.input) === JSON.stringify(first.input)
  )
}
```

**Response to Doom Loop**:
- Pause execution
- Ask user for permission to continue
- Option to break loop or provide guidance

---

### Sprint 20.4: Auto-Approval System (~400 lines)

**Goal**: Path-aware auto-approval with configurable settings.

**Files to create**:
- `packages/core/src/permissions/auto-approve.ts` (~250 lines)
- `packages/core/src/permissions/types.ts` - Add settings types

**Auto-Approval Settings**:
```typescript
interface AutoApprovalSettings {
  enabled: boolean
  actions: {
    readFiles: boolean           // Read within workspace
    readFilesExternally: boolean // Read outside workspace
    editFiles: boolean           // Edit within workspace
    editFilesExternally: boolean // Edit outside workspace
    executeSafeCommands: boolean // Safe bash commands
    executeAllCommands: boolean  // All bash commands
    useBrowser: boolean          // Browser tool
    useMcp: boolean              // MCP tools
  }
}
```

**Path-Aware Checking**:
```typescript
async function shouldAutoApprove(
  toolName: string,
  path?: string
): Promise<boolean> {
  const settings = getAutoApprovalSettings()
  const [localApproval, externalApproval] = getToolApprovalTuple(toolName)

  if (!path) return localApproval

  const isLocal = isPathInWorkspace(path)
  return isLocal ? localApproval : (localApproval && externalApproval)
}
```

**Yolo Mode**:
- Global toggle to auto-approve everything
- Overrides all individual settings
- Warning in system prompt

---

### Sprint 20.5: Error Handling Improvements (~200 lines)

**Goal**: Better error classification and recovery.

**Files to create/modify**:
- `packages/core/src/errors/types.ts` (~100 lines)
- `packages/core/src/agent/loop.ts` - Error handling

**Error Types**:
```typescript
// User rejected without feedback - halt execution
export class RejectedError extends Error {
  constructor() {
    super('User rejected permission for this tool call')
  }
}

// User rejected with feedback - continue with guidance
export class CorrectedError extends Error {
  constructor(public feedback: string) {
    super(`User provided feedback: ${feedback}`)
  }
}

// Auto-rejected by config - halt execution
export class DeniedError extends Error {
  constructor(public rule: string) {
    super(`Denied by permission rule: ${rule}`)
  }
}
```

**Error Handling Flow**:
```typescript
try {
  await executeTool(tool, params)
} catch (error) {
  if (error instanceof CorrectedError) {
    // Continue with user feedback as context
    addToContext(`User feedback: ${error.feedback}`)
    continue
  }
  if (error instanceof RejectedError || error instanceof DeniedError) {
    // Halt execution
    return { status: 'blocked', error }
  }
  // Other errors - normal handling
  throw error
}
```

---

## Summary

| Sprint | Focus | Lines |
|--------|-------|-------|
| 20.1 | Browser automation (Puppeteer) | ~700 |
| 20.2 | Plan mode (read-only) | ~400 |
| 20.3 | Doom loop detection | ~300 |
| 20.4 | Auto-approval system | ~400 |
| 20.5 | Error handling improvements | ~200 |
| **Total** | | **~2,000** |

---

## Success Criteria

- [ ] Browser tool captures screenshots and console logs
- [ ] Plan mode restricts write/execute tools
- [ ] Doom loops detected after 3 identical calls
- [ ] Auto-approval respects path (local vs external)
- [ ] Yolo mode auto-approves everything
- [ ] CorrectedError allows continuation with feedback
- [ ] All existing tests pass

---

## Future Considerations (Not in this Epic)

- Remote browser connection (port 9222)
- Viewport presets
- apply_patch tool (unified diffs)
- Multi-root workspace checkpoints
