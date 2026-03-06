# Sprint 18: CLI Agent Providers — Implementation Prompt

> For AI coding agent. Estimated: 6 features, mix M/L effort.
> Run `npm run test:run && npx tsc --noEmit` after each feature.

---

## Role

You are implementing Sprint 18 (CLI Agent Providers) for AVA, a multi-agent AI coding assistant.

Read these files first:
- `CLAUDE.md` (conventions, architecture, dispatchCompute pattern)
- `AGENTS.md` (code standards, common workflows)
- `docs/planning/praxis-v2-design.md` (Praxis v2 hierarchy — model defaults per tier)

**Context**: AVA currently calls LLM APIs directly. This sprint adds a new class of "provider" — one that spawns an external coding agent CLI (Claude Code, Gemini CLI, Codex CLI, OpenCode, Aider) as a subprocess. The user's existing subscriptions power the agents — no API keys needed for subscription-based tools.

**Why**: Anthropic, Google, and OpenAI don't allow third-party OAuth for their consumer subscriptions. But their CLIs work with the user's own login. By spawning their CLIs as subprocesses, AVA can leverage any subscription the user already has.

---

## Pre-Implementation: Read Existing Provider System

Before writing any code, read:
- `packages/extensions/providers/src/index.ts` — Current provider registration
- `packages/extensions/providers/src/dynamic-loader.ts` — Dynamic provider loading (Sprint 14)
- `packages/extensions/commander/src/invoke-team.ts` — How agents are invoked (Sprint 17)
- `packages/extensions/commander/src/invoke-subagent.ts` — Subagent invocation (Sprint 17)
- `packages/extensions/commander/src/model-config.ts` — Per-tier model config (Sprint 17)

---

## Feature 1: CLI Agent Provider Interface

### What to Build
A generic interface for wrapping any coding agent CLI as an AVA provider.

**File:** `packages/extensions/providers/src/cli-agent/types.ts` (new)

```typescript
export interface CLIAgentConfig {
  /** Unique provider name (e.g., 'claude-code', 'gemini-cli', 'codex') */
  name: string
  /** CLI binary name or path (e.g., 'claude', 'gemini', 'codex') */
  binary: string
  /** How to pass the prompt */
  promptFlag: string               // e.g., '-p' for Claude, '-p' for Gemini, 'exec' for Codex
  /** How to enable non-interactive mode */
  nonInteractiveFlags: string[]    // e.g., ['--no-user-prompt'] or ['--quiet']
  /** How to skip permission prompts */
  yoloFlags: string[]              // e.g., ['--dangerously-skip-permissions'] or ['--yolo']
  /** How to get structured output */
  outputFormatFlag?: string        // e.g., '--output-format stream-json'
  /** How to scope allowed tools */
  allowedToolsFlag?: string        // e.g., '--allowedTools'
  /** How to set working directory */
  cwdFlag?: string                 // e.g., '--cwd' or '-C'
  /** How to set model */
  modelFlag?: string               // e.g., '--model'
  /** How to continue a session */
  sessionFlag?: string             // e.g., '--session-id'
  /** Whether this CLI supports structured JSON output */
  supportsStreamJson: boolean
  /** Whether this CLI supports scoped tool permissions */
  supportsToolScoping: boolean
  /** Default tool scoping per Praxis tier */
  tierToolScopes?: Record<string, string[]>
  /** How to detect if binary is installed */
  versionCommand: string[]         // e.g., ['claude', '--version']
}

export interface CLIAgentResult {
  success: boolean
  output: string
  exitCode: number
  events?: CLIAgentEvent[]         // Parsed from stream-json if supported
  tokensUsed?: { input: number; output: number }
  durationMs: number
}

export interface CLIAgentEvent {
  type: string
  content?: string
  toolName?: string
  toolArgs?: Record<string, unknown>
  toolResult?: string
  error?: string
}
```

### Tests
- `packages/extensions/providers/src/cli-agent/types.test.ts`
- Test: Type validation for CLIAgentConfig

---

## Feature 2: CLI Agent Runner (Core Engine)

### What to Build
The subprocess spawner that runs any CLI agent and parses its output.

**File:** `packages/extensions/providers/src/cli-agent/runner.ts` (new)

```typescript
export class CLIAgentRunner {
  constructor(private config: CLIAgentConfig)

  /** Check if the CLI binary is installed and accessible */
  async isAvailable(): Promise<boolean>

  /** Run the CLI agent with a prompt, return structured result */
  async run(options: RunOptions): Promise<CLIAgentResult>

  /** Run with streaming — emit events as they arrive */
  async *stream(options: RunOptions): AsyncGenerator<CLIAgentEvent>

  /** Abort a running agent */
  abort(): void
}

interface RunOptions {
  prompt: string
  cwd: string
  model?: string
  yolo?: boolean                   // Skip permissions (default: true for engineers)
  allowedTools?: string[]          // Scope tools
  sessionId?: string               // Continue session
  timeout?: number                 // Max runtime in ms
  env?: Record<string, string>     // Extra env vars
}
```

**Implementation:**
- Spawn CLI as child process via `child_process.spawn()`
- Build args array from config + options
- If `supportsStreamJson`: parse stdout as newline-delimited JSON → emit `CLIAgentEvent`s
- If not: collect stdout as plain text → return as single output
- Stream stderr for progress/logging
- Handle timeout with `AbortController` + `setTimeout`
- Handle exit codes: 0 = success, non-zero = failure
- Capture duration via `performance.now()`
- Parse token usage from JSON output if available

**Key patterns:**
```typescript
// Building the command
const args: string[] = []

// Prompt mode
if (config.promptFlag === 'exec') {
  args.push('exec', options.prompt)      // Codex: codex exec "prompt"
} else {
  args.push(config.promptFlag, options.prompt)  // Claude/Gemini: claude -p "prompt"
}

// YOLO mode
if (options.yolo) {
  args.push(...config.yoloFlags)
}

// Output format
if (config.supportsStreamJson && config.outputFormatFlag) {
  args.push(config.outputFormatFlag, 'stream-json')
}

// Tool scoping
if (options.allowedTools && config.allowedToolsFlag) {
  args.push(config.allowedToolsFlag, options.allowedTools.join(','))
}

const child = spawn(config.binary, args, { cwd: options.cwd, env: { ...process.env, ...options.env } })
```

### Tests
- `packages/extensions/providers/src/cli-agent/runner.test.ts`
- Test: Builds correct args for Claude Code config
- Test: Builds correct args for Gemini CLI config
- Test: Builds correct args for Codex CLI config
- Test: Parses stream-json output into events
- Test: Plain text fallback when no stream-json
- Test: Timeout aborts child process
- Test: Exit code mapped to success/failure

---

## Feature 3: Built-in CLI Agent Configs

### Competitor Research
Read the headless mode docs for each CLI:
- Claude Code: `claude -p "prompt" --dangerously-skip-permissions --output-format stream-json`
- Gemini CLI: `gemini -p "prompt" --output-format json`
- Codex CLI: `codex exec "prompt" --json`
- OpenCode: `opencode run --quiet "prompt"`
- Aider: `aider --message "prompt" --yes-always`

### What to Build
Pre-configured CLIAgentConfig for each major coding agent CLI.

**File:** `packages/extensions/providers/src/cli-agent/configs.ts` (new)

```typescript
export const CLI_AGENT_CONFIGS: Record<string, CLIAgentConfig> = {
  'claude-code': { ... },
  'gemini-cli': { ... },
  'codex': { ... },
  'opencode': { ... },
  'aider': { ... },
}
```

**Configs:**

**Claude Code:**
```typescript
{
  name: 'claude-code',
  binary: 'claude',
  promptFlag: '-p',
  nonInteractiveFlags: ['--no-user-prompt'],
  yoloFlags: ['--dangerously-skip-permissions'],
  outputFormatFlag: '--output-format',
  allowedToolsFlag: '--allowedTools',
  cwdFlag: '--cwd',
  modelFlag: '--model',
  sessionFlag: '--session-id',
  supportsStreamJson: true,
  supportsToolScoping: true,
  tierToolScopes: {
    engineer: ['Edit', 'Write', 'Bash', 'Read', 'Glob', 'Grep'],
    reviewer: ['Read', 'Bash(npx biome check*)', 'Bash(npx tsc*)', 'Bash(npx vitest*)'],
    subagent: ['Read', 'Glob', 'Grep'],
  },
  versionCommand: ['claude', '--version'],
}
```

**Gemini CLI:**
```typescript
{
  name: 'gemini-cli',
  binary: 'gemini',
  promptFlag: '-p',
  nonInteractiveFlags: [],
  yoloFlags: ['--yolo'],
  outputFormatFlag: '--output-format',
  allowedToolsFlag: undefined,       // Gemini doesn't support per-tool scoping
  cwdFlag: undefined,
  modelFlag: '--model',
  sessionFlag: undefined,
  supportsStreamJson: false,         // JSON but not stream-json
  supportsToolScoping: false,
  versionCommand: ['gemini', '--version'],
}
```

**Codex CLI:**
```typescript
{
  name: 'codex',
  binary: 'codex',
  promptFlag: 'exec',               // codex exec "prompt"
  nonInteractiveFlags: [],
  yoloFlags: ['--full-auto'],
  outputFormatFlag: '--json',
  allowedToolsFlag: undefined,
  cwdFlag: undefined,
  modelFlag: '--model',
  sessionFlag: undefined,
  supportsStreamJson: true,
  supportsToolScoping: false,
  versionCommand: ['codex', '--version'],
}
```

**OpenCode:**
```typescript
{
  name: 'opencode',
  binary: 'opencode',
  promptFlag: 'run',                 // opencode run "prompt"
  nonInteractiveFlags: ['--quiet'],
  yoloFlags: [],                     // OpenCode auto-approves in headless
  outputFormatFlag: undefined,
  allowedToolsFlag: undefined,
  cwdFlag: undefined,
  modelFlag: '--model',
  sessionFlag: undefined,
  supportsStreamJson: false,
  supportsToolScoping: false,
  versionCommand: ['opencode', '--version'],
}
```

**Aider:**
```typescript
{
  name: 'aider',
  binary: 'aider',
  promptFlag: '--message',
  nonInteractiveFlags: ['--yes-always', '--no-git'],
  yoloFlags: ['--yes-always'],
  outputFormatFlag: undefined,
  allowedToolsFlag: undefined,
  cwdFlag: undefined,
  modelFlag: '--model',
  sessionFlag: undefined,
  supportsStreamJson: false,
  supportsToolScoping: false,
  versionCommand: ['aider', '--version'],
}
```

### Tests
- `packages/extensions/providers/src/cli-agent/configs.test.ts`
- Test: All 5 configs have required fields
- Test: Binary names are correct
- Test: Claude Code config supports stream-json + tool scoping

---

## Feature 4: CLI Agent Provider Registration

### What to Build
Register CLI agent providers alongside existing API providers so they appear in model/provider selection.

**File:** `packages/extensions/providers/src/cli-agent/provider.ts` (new)

```typescript
export class CLIAgentProvider {
  private runners: Map<string, CLIAgentRunner> = new Map()

  /** Discover which CLI agents are installed on this system */
  async discoverAvailable(): Promise<string[]>

  /** Get a runner for a specific CLI agent */
  getRunner(name: string): CLIAgentRunner | undefined

  /** Register all available CLI agents as providers */
  async registerAll(): Promise<void>
}
```

**Implementation:**
- On extension activation, scan for installed CLIs:
  - Run each `versionCommand` — if exits 0, CLI is available
  - Register available ones as providers
- Provider names prefixed: `cli:claude-code`, `cli:gemini-cli`, `cli:codex`, etc.
- In Praxis model config, users can now set:
```json
{
  "praxis": {
    "models": {
      "engineer": { "provider": "cli:claude-code" },
      "reviewer": { "provider": "cli:gemini-cli" },
      "subagent": { "provider": "cli:codex" }
    }
  }
}
```
- When `invoke_team` or `invoke_subagent` resolves a `cli:*` provider, it uses `CLIAgentRunner` instead of API calls
- Emit `cli-agent:discovered` event with list of available CLIs

**Integration:** Register in `packages/extensions/providers/src/index.ts`.

### Tests
- `packages/extensions/providers/src/cli-agent/provider.test.ts`
- Test: Discovers installed CLIs (mock spawn)
- Test: Registers available CLIs as providers
- Test: Unavailable CLIs skipped gracefully
- Test: Provider name prefix `cli:` applied

---

## Feature 5: Praxis Integration — CLI Agents as Tier Backends

### What to Build
Wire CLI agent providers into the Praxis v2 hierarchy so any tier can use a CLI agent.

**File:** `packages/extensions/commander/src/cli-agent-bridge.ts` (new)

```typescript
export async function executeWithCLIAgent(
  provider: string,          // e.g., 'cli:claude-code'
  task: string,
  tier: AgentRole,
  options: {
    cwd: string
    files?: string[]
    worktreePath?: string
    timeout?: number
  }
): Promise<CLIAgentResult>
```

**Implementation:**
- Resolve `cli:*` provider → get CLIAgentRunner
- Build prompt from task + tier context:
  - Engineer: "You are an engineer. Implement: {task}. Files: {files}"
  - Reviewer: "Review these changes for correctness. Run lint and tests. Files: {files}"
  - Subagent: "Research: {task}"
- Apply tier-appropriate settings:
  - Engineer: yolo=true, full tool access, worktree as cwd
  - Reviewer: yolo=false (or scoped tools), read + bash only
  - Subagent: yolo=false, read-only
- If CLI supports tool scoping, apply tier-specific tool lists from config
- If CLI supports session continuation, pass session ID for multi-turn
- Parse result and map to standard `InvokeTeamResult` or `InvokeSubagentResult`
- Stream events to message bus for UI display

**Modify:** `packages/extensions/commander/src/invoke-team.ts` — Add CLI agent path:
```typescript
if (resolvedProvider.startsWith('cli:')) {
  return executeWithCLIAgent(resolvedProvider, task, role, options)
} else {
  // Existing API-based agent execution
}
```

### Tests
- `packages/extensions/commander/src/cli-agent-bridge.test.ts`
- Test: Engineer task routed to CLI agent with yolo
- Test: Reviewer task routed with scoped tools
- Test: Subagent task routed as read-only
- Test: Worktree path used as cwd for engineers
- Test: Fallback to API if CLI unavailable

---

## Feature 6: UI — Provider Selection & Status

### What to Build
Show available CLI agent providers in settings and allow per-tier assignment.

**IMPORTANT**: Desktop UI uses **SolidJS** (NOT React).

**Files to modify:**
- `src/components/settings/tabs/LLMTab.tsx` — Add CLI agents section
- `src/components/settings/tabs/agents-tab-detail.tsx` — Per-tier provider picker

**Implementation:**

In LLM settings tab, add a "CLI Agent Providers" section:
```
CLI Agent Providers
  claude-code  ✅ Installed (v2.1.0)
  gemini-cli   ✅ Installed (v0.32.1)
  codex        ❌ Not installed
  opencode     ✅ Installed (v0.5.0)
  aider        ❌ Not installed
```

In agent/Praxis settings, per-tier model picker now shows CLI agents:
```
Engineer Model:
  [dropdown]
    API Providers:
      anthropic / claude-haiku-4-5
      openrouter / claude-haiku-4-5
    CLI Agents:
      cli:claude-code (installed)
      cli:gemini-cli (installed)
      cli:opencode (installed)
```

Show status indicator when a CLI agent is running:
- "Engineer: Running via Claude Code CLI..." with spinner

### Tests
- Update existing settings tests to include CLI agent options

---

## Post-Implementation Verification

After ALL 6 features:

1. `npm run test:run`
2. `npx tsc --noEmit`
3. `npm run lint`
4. `npm run format:check`
5. Verify no files exceed 300 lines
6. Manual test: Verify `claude --version` detection works
7. Commit: `git commit -m "feat(sprint-18): CLI agent providers for subscription-based tools"`

---

## File Change Summary

| Action | File |
|--------|------|
| CREATE | `packages/extensions/providers/src/cli-agent/types.ts` |
| CREATE | `packages/extensions/providers/src/cli-agent/types.test.ts` |
| CREATE | `packages/extensions/providers/src/cli-agent/runner.ts` |
| CREATE | `packages/extensions/providers/src/cli-agent/runner.test.ts` |
| CREATE | `packages/extensions/providers/src/cli-agent/configs.ts` |
| CREATE | `packages/extensions/providers/src/cli-agent/configs.test.ts` |
| CREATE | `packages/extensions/providers/src/cli-agent/provider.ts` |
| CREATE | `packages/extensions/providers/src/cli-agent/provider.test.ts` |
| CREATE | `packages/extensions/commander/src/cli-agent-bridge.ts` |
| CREATE | `packages/extensions/commander/src/cli-agent-bridge.test.ts` |
| MODIFY | `packages/extensions/providers/src/index.ts` (register CLI agent provider) |
| MODIFY | `packages/extensions/commander/src/invoke-team.ts` (add CLI agent path) |
| MODIFY | `packages/extensions/commander/src/invoke-subagent.ts` (add CLI agent path) |
| MODIFY | `src/components/settings/tabs/LLMTab.tsx` (CLI agents section) |
| MODIFY | `src/components/settings/tabs/agents-tab-detail.tsx` (per-tier picker) |
