# Cline Tasks & Hooks Architecture

> Analysis of Cline's task execution system and lifecycle hooks

---

## Overview

Cline's task execution system is built on a sophisticated state machine that coordinates streaming API responses with real-time tool execution, approval workflows, and extensible hook lifecycle events.

**Key Components:**
1. **TaskState** - Central state tracking for streaming, content processing, and error recovery
2. **ToolExecutor** - Coordinator-based handler pattern with streaming and completion phases
3. **Hook System** - 8 lifecycle hooks with subprocess isolation and cancellation support
4. **MessageStateHandler** - Mutex-protected message history with event emission

---

## TaskState (Central State Container)

**Key Properties:**

| Category | Properties |
|----------|------------|
| Streaming | `isStreaming`, `isWaitingForFirstChunk`, `didCompleteReadingStream` |
| Content | `assistantMessageContent[]`, `userMessageContent[]`, `toolUseIdMap` |
| Presentation | `presentAssistantMessageLocked`, `presentAssistantMessageHasPendingUpdates` |
| Ask/Response | `askResponse`, `askResponseText`, `askResponseImages`, `askResponseFiles` |
| Plan Mode | `isAwaitingPlanResponse`, `didRespondToPlanAskBySwitchingMode` |
| Tool Execution | `didRejectTool`, `didAlreadyUseTool`, `didEditFile`, `lastToolName` |
| Error Tracking | `consecutiveMistakeCount`, `didAutomaticallyRetryFailedApiRequest` |
| Context | `currentFocusChainChecklist`, `todoListWasUpdatedByUser` |
| Cancellation | `abort`, `didFinishAbortingStream`, `abandoned`, `activeHookExecution` |

---

## ToolExecutor Architecture

```
ToolExecutor
├── AutoApprove (auto-approval logic with caching)
├── ToolExecutorCoordinator (handler registry & routing)
└── Tool Handlers (20+ registered handlers)
```

### Execution Lifecycle

```
1. Check abort flag (early return if cancelled)
2. Validate tool is registered with coordinator
3. Check user rejection (skip if previous tool rejected)
4. Check parallel tool calling (skip if already used)
5. Enforce plan mode restrictions (file modification tools blocked)
6. Close browser for non-browser tools
7. Handle partial block → UI streaming
   OR
   Execute complete block:
   a. Run tool via coordinator.execute()
   b. Push tool result to conversation
   c. Track last executed tool
   d. Run PostToolUse hook (if not attempt_completion)
   e. Update focus chain
```

---

## Tool Handler Pattern (IFullyManagedTool)

```typescript
interface IToolHandler {
  readonly name: ClineDefaultTool
  execute(config: TaskConfig, block: ToolUse): Promise<ToolResponse>
  getDescription(block: ToolUse): string
}

interface IPartialBlockHandler {
  handlePartialBlock(block: ToolUse, uiHelpers): Promise<void>
}

interface IFullyManagedTool extends IToolHandler, IPartialBlockHandler {}
```

**Dual Responsibility:**
1. **Streaming Phase** (`handlePartialBlock`) - UI updates only
2. **Completion Phase** (`execute`) - Full execution + results

---

## Hook System Architecture

### 8 Hook Types

| Hook | Trigger | Cancellable | Use Case |
|------|---------|-------------|----------|
| **TaskStart** | Task begins | No | Log start, prerequisites |
| **TaskResume** | Task resumed | No | Refresh context |
| **TaskCancel** | Task cancelled | No | Cleanup |
| **TaskComplete** | Completion attempted | No | Log completion, metrics |
| **PreToolUse** | Before tool execution | Yes | Validate inputs, block |
| **PostToolUse** | After tool execution | No | Log results, metrics |
| **UserPromptSubmit** | After user submits | Yes | Validate user input |
| **PreCompact** | Before context compaction | No | Prepare for summarization |

### Hook Execution Flow

```
1. Early return if hooks disabled (platform check)
2. Check if hook exists via HookFactory.hasHook()
3. Show hook execution indicator message
4. Track active hook for cancellation
5. Create streaming callback for line-by-line output
6. Create HookProcess with abort signal
7. Execute hook.run() with JSON input
8. Validate output JSON
9. Handle cancellation/success
10. Add context modifications to conversation
11. Return HookExecutionResult
```

### HookProcess (Subprocess Isolation)

**Features:**
- **Real-time Streaming**: Line-by-line stdout/stderr emission
- **30-Second Timeout**: Prevents hanging hooks
- **1MB Output Limit**: Prevents memory exhaustion
- **Shell Execution**: Interprets shebangs (#! bash, #! python)
- **Detached Process Group**: On Unix, kills entire process tree on abort
- **Registry Tracking**: Global HookProcessRegistry prevents zombie processes

---

## Hook Context Modification

**Format:**
```xml
<hook_context source="PreToolUse|PostToolUse" type="general|workspace_rules|...">
Content here
</hook_context>
```

**Processing:**
1. Hook outputs `contextModification` string
2. Parse first line for type prefix: "TYPE: content"
3. Format as XML with source and type attributes
4. Add to `userMessageContent` before next API request

---

## MessageStateHandler

**Purpose:** Manages task message history with thread-safe operations

**Key Features:**
- **Mutex Protection**: `stateMutex` prevents concurrent state modifications
- **Event Emission**: Emits `clineMessagesChanged` with change details
- **Message Operations**: add, update, delete, setClineMessages

---

## Cancellation Patterns

### Hook Cancellation (HookExecution)

```typescript
interface HookExecution {
  hookName: string           // "PreToolUse", "PostToolUse", etc.
  toolName?: string          // For PreToolUse/PostToolUse
  messageTs: number          // Message timestamp for UI
  abortController: AbortController // Signal for subprocess
}
```

**Flow:**
1. User cancels hook in UI
2. Call `abortController.abort()`
3. HookProcess receives signal, kills subprocess
4. Hook executor catches cancellation error
5. Update hook message to "cancelled"

---

## Auto-Approval System

**Settings:**
- `yoloModeToggled` - Auto-approve all tools
- `autoApproveAllToggled` - Auto-approve high-risk tools
- Per-path settings for specific directories

**Return Types:**
- `boolean` - For read-only tools
- `[boolean, boolean]` - For high-risk tools → `[autoApprove, showApprovalUI]`

---

## Notable Features for Estela

### 1. Dual-Phase Tool Execution (Partial + Complete)
Tools have `handlePartialBlock()` for streaming UI + `execute()` for completion.

### 2. Hook System with Subprocess Isolation
8 lifecycle hooks, external bash scripts, streaming output, timeout/cancellation.

### 3. Mutex-Protected Message State
Prevents concurrent modifications with `p-mutex`.

### 4. Context Modification from Hooks
Hooks can inject `<hook_context>` XML into conversation.

### 5. Plan Mode Restrictions
File modification tools blocked in strict plan mode.

### 6. Auto-Approval with Path-Based Granularity
Per-path approval with workspace caching.

### 7. Tool Handler Registry Pattern
`ToolExecutorCoordinator` with shared handler support.

### 8. Focus Chain / Todo List Integration
`apiRequestsSinceLastTodoUpdate`, `currentFocusChainChecklist` state.

### 9. Consecutive Mistake Tracking
`consecutiveMistakeCount` stops execution after repeated errors.

### 10. Hook Cancellation Mid-Execution
Track active hooks, show cancellation controls in UI.
