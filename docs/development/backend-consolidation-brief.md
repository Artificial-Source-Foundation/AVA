# Backend Consolidation Brief
## CLI vs Desktop Discrepancy Analysis

**Date:** March 3, 2026  
**Status:** Critical architectural issues identified

---

## Executive Summary

The codebase has **two divergent backend implementations** that should be unified:

1. **CLI** (`cli/`) - Uses `@ava/core-v2` with full extension system, ~50+ tools
2. **Desktop** (`src/`) - Also uses `@ava/core-v2` but runs in **Tauri WebView** with severely limited platform capabilities

**The Desktop app has critical bugs causing "tons of errors" while CLI works perfectly.** These aren't configuration issues—they're fundamental gaps in the Tauri platform implementation.

---

## Architecture Overview

### Current Stack (Both CLI & Desktop)

```
┌─────────────────────────────────────────────────────────────┐
│                      Frontend Layer                        │
│  ┌──────────────┐  ┌─────────────────────────────────────┐ │
│  │ CLI (Node)   │  │ Desktop (Tauri WebView)             │ │
│  │ readline UI  │  │ SolidJS UI → useAgent() hook        │ │
│  └──────────────┘  └─────────────────────────────────────┘ │
└─────────────────────────────────────────────────────────────┘
                              │
                              ▼
┌─────────────────────────────────────────────────────────────┐
│                     Core-v2 Agent Layer                    │
│         @ava/core-v2 (AgentExecutor, Tools, LLM)           │
│                                                             │
│  ┌─────────────────────────────────────────────────────┐   │
│  │ Extensions (~48 total)                              │   │
│  │ • tools-extended (27 tools)                         │   │
│  │ • providers (anthropic, openai, openrouter, etc.)   │   │
│  │ • permissions (approval system)                     │   │
│  │ • agent-modes (doom-loop detection)                 │   │
│  │ • prompts, memory, lsp, sandbox, etc.               │   │
│  └─────────────────────────────────────────────────────┘   │
└─────────────────────────────────────────────────────────────┘
                              │
                              ▼
┌─────────────────────────────────────────────────────────────┐
│                    Platform Abstraction                    │
│  ┌──────────────────────┐  ┌──────────────────────────────┐ │
│  │ @ava/platform-node   │  │ @ava/platform-tauri          │ │
│  │ Node.js primitives   │  │ Tauri WebView + Rust bridge  │ │
│  └──────────────────────┘  └──────────────────────────────┘ │
└─────────────────────────────────────────────────────────────┘
```

**Key Insight:** Both CLI and Desktop use the **same core-v2 agent code**, but the **platform implementations are wildly different in capability**.

---

## 🔴 Critical Issues in Desktop (platform-tauri)

### 1. **Bash Tool Returns NO OUTPUT** (CRITICAL)

**Files:** `packages/platform-tauri/src/shell.ts:64-66`, `packages/core/src/tools/bash.ts:270-305`

**Problem:**
```typescript
// platform-tauri/src/shell.ts
return {
    pid: undefined,
    stdin: null,    // ← NULL
    stdout: null,   // ← NULL
    stderr: null,   // ← NULL
    wait: async () => ({ stdout: accumulated, stderr: '', exitCode: 0 })
};
```

The bash tool tries to read output via `child.stdout.getReader()`, but since `stdout` is `null`, it skips reading entirely. The `wait()` method does buffer output internally, but bash.ts never reads those fields.

**Result:** Every bash command appears to run but returns `(no output)`. The agent gets no feedback from commands.

**Affected Tools:**
- `bash` (all commands)
- `sandbox_noop` (uses same spawn pattern)
- `sandbox_docker` (uses same spawn pattern)

---

### 2. **Environment Variables Unavailable** (CRITICAL)

**Files:** `packages/core/src/tools/websearch.ts:162`, `packages/core/src/tools/codesearch.ts:96,119`

**Problem:** In Tauri WebView, `process.env` is `undefined`. Tools directly access `process.env.TAVILY_API_KEY` and `process.env.EXA_API_KEY`.

**Result:**
- `websearch` tool always fails with "API key not configured"
- `codesearch` tool always fails with "Exa API key not configured"
- Browser tool (Puppeteer) can't be imported in webview

---

### 3. **Process Management Broken**

**Files:** `packages/platform-tauri/src/shell.ts:35`, `packages/core/src/tools/bash.ts:244-245`

**Missing:**
- `inactivityTimeout: 30_000` - ignored (commands that hang aren't killed)
- `killProcessGroup: true` - ignored (orphaned processes continue running)
- `pid` - always `undefined` (no process tracking)

**Result:** Long-running or hanging commands aren't properly managed. Zombie processes accumulate.

---

### 4. **PTY Is Not a True PTY**

**Files:** `packages/platform-tauri/src/pty.ts:94-133`

**Problems:**
```typescript
async resize(): Promise<void> {
    // No-op — Tauri's shell API does not support terminal resize
}
```

- `resize()` is a no-op (terminal stuck at default size)
- No `TERM` environment variable set
- No raw terminal mode (curses apps like `vim` won't work)
- Not a real pseudo-terminal, just a subprocess wrapper

**Result:** Interactive commands fail or behave incorrectly.

---

### 5. **File System Gaps**

**Files:** `packages/platform-tauri/src/fs.ts:112-249`

**Problems:**
- `realpath()` is a stub (returns path as-is, no symlink resolution)
  - Security issue: path traversal detection in `utils.ts:285-287` won't work
- `glob()` uses custom implementation vs `fast-glob`:
  - Different skip directories (14 hardcoded vs Node's 2)
  - 1000 result cap
  - Possible regex edge cases

---

### 6. **Credential Store Limitations**

**Files:** `packages/platform-tauri/src/credentials.ts:51-57`

**Problems:**
- Only reads from `localStorage` (no environment variable fallback)
- Size limits (~5MB)
- Not encrypted at rest
- Write failures silently swallowed (`.catch(() => {})`)
- No legacy migration from `~/.estela/` to `~/.ava/`

---

## 📊 Side-by-Side Comparison

| Feature | CLI (platform-node) | Desktop (platform-tauri) | Impact |
|---------|---------------------|--------------------------|--------|
| **Bash output** | ✅ Full streaming | 🔴 **NULL streams** | Agent blind to command output |
| **Environment vars** | ✅ `process.env` | 🔴 **Undefined** | API keys not found |
| **Spawn stdin** | ✅ Writable stream | 🔴 **null** | Can't pipe input to commands |
| **Spawn PID** | ✅ Real PID | 🔴 **undefined** | No process tracking |
| **Inactivity timeout** | ✅ Auto-kill at 30s | 🔴 **Ignored** | Hanging commands persist |
| **Process group kill** | ✅ SIGTERM→SIGKILL | 🔴 **Ignored** | Orphaned processes |
| **True PTY** | ✅ `node-pty` | 🔴 **Subprocess only** | Interactive apps broken |
| **PTY resize** | ✅ Native resize | 🔴 **No-op** | Terminal size fixed |
| **realpath()** | ✅ Symlink resolve | 🔴 **Identity function** | Security gap |
| **glob()** | ✅ `fast-glob` | ⚠️ Custom (1000 cap) | Different behavior |
| **Browser tool** | ✅ Puppeteer | 🔴 **Unavailable** | Web automation broken |
| **Credential env** | ✅ Checks env vars | 🔴 **localStorage only** | CLI credentials not shared |
| **Extension loading** | ✅ Dynamic FS scan | ✅ Static imports | Different loading |
| **DB transactions** | ✅ Transactional | ⚠️ Non-transactional | Migration safety |

---

## Root Causes

### 1. **Tauri's Shell API Limitations**

Tauri's `@tauri-apps/plugin-shell` is designed for simple command execution, not as a full shell abstraction:

```typescript
// Tauri provides:
Command.create('sh', ['-c', cmd])
  .execute()  // Returns { stdout, stderr, exitCode } when DONE

// Node's child_process provides:
spawn('sh', ['-c', cmd])
  // Returns immediately with streams + PID
  // Supports stdin/stdout/stderr streaming
  // Can kill process groups
  // Full PTY via node-pty
```

**The `execute()` method buffers everything internally—no streaming.** The Tauri platform implementation tries to fake a spawn interface but returns `null` for all streams.

### 2. **WebView Environment Differences**

- `process.env` is a Node.js feature, not available in browsers/WebViews
- `puppeteer` is a Node.js package, can't run in WebView
- `localStorage` has size limits and isn't shared with CLI's file-based storage

### 3. **Different Initialization Paths**

| Aspect | CLI | Desktop |
|--------|-----|---------|
| Entry | `cli/src/index.ts` | `src/index.tsx` → `src/App.tsx` |
| Platform | `createNodePlatform()` | `createTauriPlatform()` |
| Agent | `AgentExecutor` via import | `AgentExecutor` via import |
| UI | readline | SolidJS |
| Extensions | Dynamic FS scan | Static imports (36 hardcoded) |
| Approval | readline prompt | UI dialog |

**Both use the same core code, but platform capabilities differ drastically.**

---

## Recommended Solutions

### Option 1: Fix platform-tauri (Short-term)

**Priority: Critical**

1. **Fix bash output buffering**
   - Change `TauriShell.spawn()` to return the accumulated output immediately instead of `null` streams
   - Or modify bash.ts to read from `wait()` result instead of streams

2. **Add environment variable bridge**
   - Create Tauri command to fetch env vars from Rust side
   - Polyfill `process.env` in WebView

3. **Implement proper process management**
   - Use Rust sidecar for process control with PID tracking
   - Implement inactivity timeout in Rust

4. **Fix credential sharing**
   - Read from `~/.ava/credentials.json` via Tauri FS API
   - Sync with localStorage

### Option 2: Run Agent in Rust/Node Sidecar (Medium-term)

**Architecture Change:**

```
Desktop App:
┌─────────────────┐     ┌──────────────────────────────┐
│  Tauri WebView  │────▶│  Rust Backend / Node Sidecar │
│  (SolidJS UI)   │     │  • AgentExecutor             │
│                 │◀────│  • All tools execute here    │
└─────────────────┘     │  • Full Node.js capabilities │
                        └──────────────────────────────┘
```

**Benefits:**
- Use `platform-node` implementation (works perfectly)
- Full access to Node.js APIs
- Can use Puppeteer, full PTY, environment variables
- Same code path as CLI

**Implementation:**
- Embed Node.js binary or use Tauri's sidecar feature
- Communicate via IPC (WebSocket or Tauri events)

### Option 3: Unified Backend Service (Long-term)

Extract the agent runtime into a standalone service:

```
┌─────────┐  ┌──────────┐  ┌──────────────┐
│   CLI   │  │  Desktop │  │  Other Apps  │
└────┬────┘  └────┬─────┘  └──────┬───────┘
     │            │               │
     └────────────┼───────────────┘
                  ▼
        ┌──────────────────┐
        │  AVA Agent Daemon │
        │  (Node.js process)│
        │  • HTTP/WebSocket │
        │  • Full platform  │
        └──────────────────┘
```

**Benefits:**
- Single backend for all clients
- Consistent behavior everywhere
- Can run headless or with UI
- Easier to test and debug

---

## Immediate Action Items

1. **Fix bash tool output** (1-2 days)
   - Modify `platform-tauri/src/shell.ts` to properly buffer and return output
   - This alone will fix "tons of errors" in Desktop

2. **Add env var bridge** (1 day)
   - Tauri command: `get_env_vars()`
   - Polyfill in WebView bootstrap

3. **Audit all tools** (2-3 days)
   - Check every tool in `packages/core/src/tools/` for Node.js dependencies
   - Document which won't work in Desktop

4. **Consider sidecar approach** (1-2 weeks)
   - Evaluate if running agent in Node sidecar is feasible
   - This would eliminate most platform differences

---

## Appendix: Tool Status Matrix

| Tool | CLI Status | Desktop Status | Root Cause |
|------|------------|----------------|------------|
| bash | ✅ Works | 🔴 No output | Null streams |
| read_file | ✅ Works | ✅ Works | FS abstraction OK |
| write_file | ✅ Works | ✅ Works | FS abstraction OK |
| edit | ✅ Works | ✅ Works | FS abstraction OK |
| glob | ✅ Works | ⚠️ Different | Custom glob impl |
| grep | ✅ Works | ✅ Works | FS abstraction OK |
| create_file | ✅ Works | ✅ Works | FS abstraction OK |
| delete_file | ✅ Works | ✅ Works | FS abstraction OK |
| ls | ✅ Works | ✅ Works | FS abstraction OK |
| websearch | ✅ Works | 🔴 Broken | process.env undefined |
| codesearch | ✅ Works | 🔴 Broken | process.env undefined |
| browser | ✅ Works | 🔴 Unavailable | Puppeteer not in WebView |
| sandbox | ✅ Works | 🔴 Broken | Same as bash |
| todo | ✅ Works | ✅ Works | No platform deps |
| task | ✅ Works | ✅ Works | No platform deps |
| question | ✅ Works | ✅ Works | No platform deps |
| completion | ✅ Works | ✅ Works | No platform deps |

---

## Conclusion

The "backend mess" is actually **two separate platform implementations with different capabilities**. The CLI works because `platform-node` properly implements all required primitives. The Desktop has critical bugs because `platform-tauri` attempts to adapt Tauri's limited shell API to a full platform abstraction.

**The fix requires either:**
1. Improving `platform-tauri` to buffer output properly (quick fix)
2. Running the agent in a Node sidecar (better long-term solution)
3. Creating a unified backend service (best architecture)

The most impactful immediate fix is **making bash tool return output in Desktop**—this alone will resolve most of the "tons of errors" you're seeing.
