# Sprint 6: Sandbox & Safety

**Epic:** B — Deepen
**Duration:** 1 week
**Goal:** Users feel safe running the agent autonomously — sandbox, permissions, checkpoints
**Parallel with:** Sprint 5 (Agent Reliability)

---

## Competitive Landscape

| Tool | Sandbox | Permissions | Checkpoints |
|---|---|---|---|
| **Codex CLI** | Seatbelt + bwrap + seccomp + Landlock | Declarative .rules files + escalation | Ghost commits (invisible snapshots) |
| **Goose** | MCP extension-based | Tool inspection manager + levels | Session state serialization |
| **Gemini CLI** | sandbox-exec / docker / podman | Policy engine + message bus | None |
| **Cline** | None (VS Code trust) | Auto-approval + pre-tool hooks | None |
| **OpenCode** | None | Wildcard pattern rules + path expansion | None |

**Target:** Codex's sandbox + permissions + ghost checkpoints.

---

## Story 6.1: OS-Level Sandbox (from Codex CLI)

**Reference (Linux):** `docs/reference-code/codex-cli/codex-rs/core/src/landlock.rs`
**Reference (macOS):** `docs/reference-code/codex-cli/codex-rs/cli/src/debug_sandbox/seatbelt.rs`
**Rust crate:** `crates/ava-sandbox/` (already implemented — bwrap + sandbox-exec)

The Rust crate is built. Wire it into the bash tool:

`packages/extensions/permissions/src/sandbox-middleware.ts`:

```typescript
api.addToolMiddleware({
  name: 'sandbox',
  priority: 3, // Before permissions check
  before: async (call) => {
    if (call.name === 'bash' && shouldSandbox(call.arguments.command)) {
      // Route through Rust sandbox
      call.arguments._sandboxed = true
      call.arguments._sandbox_policy = determineSandboxPolicy(call)
    }
  }
})

function shouldSandbox(command: string): boolean {
  // Always sandbox: npm install, pip install, cargo build, make
  // Never sandbox: git status, ls, cat, echo
  // Ask for: curl, wget, ssh, anything with pipes to unknown commands
}

function determineSandboxPolicy(call: ToolCall): SandboxPolicy {
  return {
    writable_paths: [process.cwd(), os.tmpdir()],
    network: false, // Block by default (like Codex)
    max_time_ms: 120_000,
  }
}
```

**Tauri command:** `sandbox_run` already exists.
Wire: bash tool → if `_sandboxed` → invoke `sandbox_run` instead of direct execution.

**Acceptance criteria:**
- [ ] Bash commands sandboxed by default on Linux (bwrap) and macOS (sandbox-exec)
- [ ] Network blocked in sandbox
- [ ] Writable only to CWD + tmp
- [ ] Clear error message if sandbox unavailable

---

## Story 6.2: Git Checkpoints (from Codex CLI)

**Reference:** `docs/reference-code/codex-cli/codex-rs/utils/git/src/ghost_commits.rs`

**What Codex does:**
- Creates invisible git commits ("ghost snapshots") at each agent turn
- Ignores: node_modules, .venv, dist, build, files >10MB, dirs >200 items
- On failure: `restore_ghost_commit()` rolls back to last good state
- User can undo with explicit "undo" command

**What to build:**

`packages/extensions/git/src/checkpoints.ts`:

```typescript
export async function createCheckpoint(label: string): Promise<string> {
  // Stash current state
  await exec('git stash push -u -m "ava-checkpoint: ' + label + '"')
  // Pop immediately (we just want the stash ref for rollback)
  await exec('git stash pop')
  // Actually: use git commit on a detached orphan branch
  const sha = await createGhostCommit(label)
  return sha
}

export async function rollbackToCheckpoint(sha: string): Promise<void> {
  await exec(`git checkout ${sha} -- .`)
}
```

**When to checkpoint:**
- Before each agent run (auto)
- Before destructive tool calls (rm, git reset, etc.)
- On explicit user request

**Acceptance criteria:**
- [ ] Checkpoint created before each agent run
- [ ] Rollback restores all files to checkpoint state
- [ ] Ghost commits don't pollute git log (use refs/ava/ namespace)
- [ ] Undo command works in UI

---

## Story 6.3: Dynamic Permissions (from Codex CLI + Goose)

**Reference (Codex):** `docs/reference-code/codex-cli/codex-rs/core/src/exec_policy.rs`
**Reference (Goose):** `docs/reference-code/goose/crates/goose/src/agents/tool_inspection.rs`

**Current state:** AVA has static permission rules + Rust tree-sitter bash parser.
**Missing:** Learning from user decisions within a session.

**What to build:**

`packages/extensions/permissions/src/dynamic-rules.ts`:

```typescript
interface SessionRule {
  tool: string
  pattern: string // glob pattern for args
  action: 'allow' | 'deny'
  count: number  // how many times applied
}

const sessionRules: SessionRule[] = []

// When user approves a tool call:
export function onApproval(call: ToolCall, decision: 'allow' | 'deny' | 'always') {
  if (decision === 'always') {
    // Extract pattern: e.g., "bash" + "git *" → allow all git commands
    const pattern = extractPattern(call)
    sessionRules.push({ tool: call.name, pattern, action: 'allow', count: 0 })
  }
}

// Before each tool call: check session rules first
export function checkSessionRules(call: ToolCall): 'allow' | 'deny' | 'ask' {
  const match = sessionRules.find(r =>
    r.tool === call.name && globMatch(r.pattern, extractArgs(call))
  )
  if (match) { match.count++; return match.action }
  return 'ask' // Fall through to static rules
}
```

**Pattern extraction (from Codex):**
- `git status` → pattern: `git *`
- `npm test` → pattern: `npm test*`
- `rm -rf node_modules` → pattern: `rm -rf node_modules` (exact, dangerous)

**Acceptance criteria:**
- [ ] "Always allow" remembers decision for session
- [ ] Pattern extraction generalizes correctly
- [ ] Dangerous commands never auto-generalized (rm, mkfs, etc.)
- [ ] Session rules reset on new session
