# Epic 26: Gemini CLI Feature Parity

> Infrastructure features from Gemini CLI that enhance AVA's safety, extensibility, and UX

**Analysis:** [`docs/analysis/gemini-cli/COMPARISON.md`](../../analysis/gemini-cli/COMPARISON.md)

---

## Overview

Based on comprehensive Gemini CLI analysis (~139KB documentation), these features close the infrastructure gap:

| Sprint | Focus | Lines | Status |
|--------|-------|-------|--------|
| 1 | Policy Engine + Message Bus | ~1,300 | Ready |
| 2 | Session Resume + TOML Commands | ~1,000 | Blocked by S1 |
| 3 | Extension System + Trusted Folders | ~1,500 | Blocked by S2 |
| 4 | Chat Compression Enhancement | ~600 | Blocked by S2 |
| **Total** | | **~4,400** | |

---

## Sprint 1: Policy Engine + Message Bus

**Goal:** Priority-based approval rules and decoupled tool/UI communication

### 1.1 Policy Engine (~800 lines)

**File:** `packages/core/src/policy/engine.ts`

The current `permissions/` system has auto-approve + rule matching, but lacks:
- Priority-based rule sorting (higher priority = checked first)
- Wildcard tool patterns (`mcp__*`, `delegate_*`)
- Regex args matching on stable JSON
- Approval mode scoping (default, yolo, plan)
- Compound shell command recursive validation
- Safety checker integration layer

```typescript
export enum PolicyDecision {
  ALLOW = 'allow',
  DENY = 'deny',
  ASK_USER = 'ask_user',
}

export interface PolicyRule {
  /** Rule identifier */
  name: string;
  /** Tool name pattern - supports wildcards: 'bash', 'mcp__*', '*' */
  toolName?: string;
  /** Regex pattern matched against stable JSON of args */
  argsPattern?: RegExp;
  /** What to do when matched */
  decision: PolicyDecision;
  /** Higher priority = checked first. Default: 0 */
  priority: number;
  /** Approval modes where rule applies. Empty = all modes */
  modes?: ApprovalMode[];
  /** Allow redirections in shell commands */
  allowRedirection?: boolean;
  /** Source of rule: 'builtin' | 'user' | 'project' | 'extension' */
  source: string;
  /** Message shown on DENY */
  denyMessage?: string;
}

export interface PolicyEngineConfig {
  rules: PolicyRule[];
  defaultDecision: PolicyDecision;
  approvalMode: ApprovalMode;
  nonInteractive: boolean;
}
```

**File:** `packages/core/src/policy/rules.ts` (~200 lines)

Built-in rules:

```typescript
export const BUILTIN_RULES: PolicyRule[] = [
  // Plan mode: only read tools
  {
    name: 'plan-mode-read-only',
    toolName: '*',
    decision: PolicyDecision.DENY,
    priority: 1000,
    modes: [ApprovalMode.PLAN],
    denyMessage: 'Write operations disabled in plan mode',
  },
  {
    name: 'plan-mode-allow-read',
    toolName: 'read_file',
    decision: PolicyDecision.ALLOW,
    priority: 1001,
    modes: [ApprovalMode.PLAN],
  },
  // ... glob, grep, ls, websearch also allowed in plan

  // Yolo mode: allow everything
  {
    name: 'yolo-allow-all',
    toolName: '*',
    decision: PolicyDecision.ALLOW,
    priority: 900,
    modes: [ApprovalMode.YOLO],
  },

  // Default: read tools auto-approve
  {
    name: 'default-allow-read',
    toolName: 'read_file',
    decision: PolicyDecision.ALLOW,
    priority: 100,
  },
  // ... glob, grep, ls auto-approve

  // Default: write tools ask user
  {
    name: 'default-ask-write',
    toolName: 'write_file',
    decision: PolicyDecision.ASK_USER,
    priority: 50,
  },
  // ... edit, create, delete ask user

  // Bash: always ask unless safe command
  {
    name: 'default-ask-bash',
    toolName: 'bash',
    decision: PolicyDecision.ASK_USER,
    priority: 50,
  },
];
```

**File:** `packages/core/src/policy/matcher.ts` (~150 lines)

Stable JSON + pattern matching utilities:

```typescript
/** Stable JSON stringify with sorted keys for regex matching */
export function stableStringify(value: unknown): string;

/** Match tool name against pattern with wildcard support */
export function matchToolName(pattern: string, toolName: string): boolean;

/** Match args against regex on stable JSON */
export function matchArgs(pattern: RegExp, args: Record<string, unknown>): boolean;

/** Check compound shell command (&&, ||, |, ;) recursively */
export function checkCompoundCommand(
  command: string,
  checkFn: (cmd: string) => PolicyDecision,
): PolicyDecision;
```

**File:** `packages/core/src/policy/index.ts` (~50 lines)

Public exports.

### 1.2 Message Bus (~500 lines)

**File:** `packages/core/src/bus/message-bus.ts`

Decoupled event-driven communication:

```typescript
export enum BusMessageType {
  // Tool confirmation flow
  TOOL_CONFIRMATION_REQUEST = 'tool-confirmation-request',
  TOOL_CONFIRMATION_RESPONSE = 'tool-confirmation-response',
  TOOL_POLICY_REJECTION = 'tool-policy-rejection',

  // Tool execution lifecycle
  TOOL_EXECUTION_START = 'tool-execution-start',
  TOOL_EXECUTION_SUCCESS = 'tool-execution-success',
  TOOL_EXECUTION_FAILURE = 'tool-execution-failure',

  // Policy updates
  UPDATE_POLICY = 'update-policy',

  // User interaction
  ASK_USER_REQUEST = 'ask-user-request',
  ASK_USER_RESPONSE = 'ask-user-response',
}

export interface BusMessage {
  type: BusMessageType;
  correlationId: string;
  timestamp: number;
}

export interface ToolConfirmationRequest extends BusMessage {
  type: BusMessageType.TOOL_CONFIRMATION_REQUEST;
  toolName: string;
  toolArgs: Record<string, unknown>;
  riskLevel: RiskLevel;
  details?: string;
}

export interface ToolConfirmationResponse extends BusMessage {
  type: BusMessageType.TOOL_CONFIRMATION_RESPONSE;
  confirmed: boolean;
  rememberChoice?: 'session' | 'persistent' | false;
}
```

```typescript
export class MessageBus {
  private listeners: Map<BusMessageType, Set<(msg: BusMessage) => void>>;

  constructor(private policyEngine: PolicyEngine) {}

  /** Fire-and-forget publish */
  async publish(message: BusMessage): Promise<void>;

  /** Subscribe to message type */
  subscribe<T extends BusMessage>(
    type: T['type'],
    handler: (msg: T) => void,
  ): () => void;

  /** Request-response with correlation ID and timeout */
  async request<TReq extends BusMessage, TRes extends BusMessage>(
    request: Omit<TReq, 'correlationId' | 'timestamp'>,
    responseType: TRes['type'],
    timeoutMs?: number,
  ): Promise<TRes>;

  /** Integrated tool confirmation flow */
  async confirmToolExecution(
    toolName: string,
    toolArgs: Record<string, unknown>,
  ): Promise<{ confirmed: boolean; remember?: string }>;
}
```

**File:** `packages/core/src/bus/types.ts` (~100 lines)

All bus message type definitions.

**File:** `packages/core/src/bus/index.ts` (~20 lines)

### Sprint 1 Integration

Update `packages/core/src/tools/registry.ts`:
- Replace direct auto-approval check with `messageBus.confirmToolExecution()`
- Policy engine decides, message bus routes to UI if ASK_USER

Update `packages/core/src/agent/loop.ts`:
- Inject message bus into tool execution context
- Subscribe to bus events for activity streaming

### Sprint 1 Acceptance Criteria

- [ ] Policy engine evaluates rules by priority (descending)
- [ ] Wildcard tool patterns work (`*`, `mcp__*`, `delegate_*`)
- [ ] Regex args matching on stable JSON
- [ ] Approval modes scope rules (default, yolo, plan)
- [ ] Compound shell commands checked recursively
- [ ] Message bus publishes/subscribes with correlation IDs
- [ ] Request-response pattern with timeout (60s default)
- [ ] Tool confirmation flows through message bus
- [ ] 30+ unit tests

---

## Sprint 2: Session Resume + TOML Commands

**Goal:** Resume conversations and user-defined custom commands

### 2.1 Session Resume (~600 lines)

**File:** `packages/core/src/session/resume.ts`

```typescript
export interface SessionInfo {
  id: string;
  startTime: string;
  lastUpdated: string;
  messageCount: number;
  displayName: string;
  firstUserMessage: string;
  summary?: string;
}

export class SessionManager {
  constructor(private sessionsDir: string) {}

  /** Save current session to disk */
  async save(sessionId: string, state: SessionState): Promise<void>;

  /** Load session by ID, index, or "latest" */
  async load(identifier: string): Promise<SessionState | null>;

  /** List all sessions with metadata */
  async list(): Promise<SessionInfo[]>;

  /** Delete a session */
  async delete(sessionId: string): Promise<void>;

  /** Search sessions by content */
  async search(query: string): Promise<SessionInfo[]>;
}
```

**Resolution strategies:**
1. UUID exact match
2. Numeric index (1-based, most recent first)
3. `"latest"` keyword
4. Error with suggestions if not found

**Storage format:** `~/.estela/sessions/{id}.json`

```json
{
  "id": "abc123",
  "startTime": "2026-02-05T10:00:00Z",
  "lastUpdated": "2026-02-05T10:30:00Z",
  "messages": [...],
  "tools": [...],
  "metadata": {
    "cwd": "/path/to/project",
    "model": "claude-opus-4-5-20251101",
    "provider": "anthropic"
  }
}
```

**File:** `packages/core/src/session/selector.ts` (~150 lines)

Session selection with deduplication and filtering.

### 2.2 TOML Custom Commands (~400 lines)

**File:** `packages/core/src/commands/toml-loader.ts`

```typescript
export interface TomlCommand {
  name: string;
  description?: string;
  prompt: string;
  args?: Record<string, { description: string; required?: boolean }>;
}

export class TomlCommandLoader {
  /** Discovery locations (in priority order) */
  private dirs = [
    '.estela/commands/',        // Project-level
    '~/.estela/commands/',      // User-level
  ];

  /** Load all TOML commands */
  async loadCommands(): Promise<TomlCommand[]>;

  /** Load single TOML file */
  async loadFile(path: string): Promise<TomlCommand>;
}
```

**TOML format:**
```toml
# ~/.estela/commands/deploy.toml
description = "Deploy to production"
prompt = """
Run the deployment process:
1. Build the project
2. Run tests
3. Deploy to production
"""

# With arguments
[args.environment]
description = "Target environment"
required = true
```

**Naming convention:** File paths become colon-separated names
- `deploy.toml` -> `/deploy`
- `aws/lambda.toml` -> `/aws:lambda`

**File:** `packages/core/src/commands/processor.ts` (~150 lines)

Prompt processing pipeline:
1. `@file` injection (expand `@path/to/file` to file contents)
2. Argument substitution (`{{args}}`, `{{args.environment}}`)
3. Default argument handling (append raw args if no template vars)

**File:** `packages/core/src/commands/index.ts` (~50 lines)

Integrate with existing slash commands registry.

### Sprint 2 Acceptance Criteria

- [ ] Sessions save to `~/.estela/sessions/`
- [ ] Resume by ID, index, or "latest"
- [ ] Session list with metadata (time, messages, summary)
- [ ] Session search by content
- [ ] TOML commands discovered from 2 locations
- [ ] Prompt processing with @file injection
- [ ] Argument substitution works
- [ ] Commands register as slash commands
- [ ] 20+ unit tests

---

## Sprint 3: Extension System + Trusted Folders

**Goal:** Plugin ecosystem and security boundaries

### 3.1 Extension System (~1,200 lines)

**File:** `packages/core/src/extensions/manifest.ts` (~100 lines)

```typescript
export interface ExtensionManifest {
  name: string;
  version: string;
  description: string;
  author?: string;
  capabilities: ExtensionCapability[];
  entrypoint?: string;
  settings?: ExtensionSetting[];
}

export type ExtensionCapability =
  | { type: 'commands'; commands: string[] }
  | { type: 'mcp-servers'; servers: McpServerConfig[] }
  | { type: 'skills'; skills: string[] }
  | { type: 'tools'; tools: string[] };

export interface ExtensionSetting {
  name: string;
  description: string;
  type: 'string' | 'number' | 'boolean';
  required?: boolean;
  default?: unknown;
}
```

**File:** `packages/core/src/extensions/loader.ts` (~300 lines)

```typescript
export class ExtensionLoader {
  /** Load extension from manifest */
  async load(path: string): Promise<Extension>;

  /** Validate manifest against schema */
  validate(manifest: ExtensionManifest): ValidationResult;

  /** Resolve extension capabilities */
  resolveCapabilities(ext: Extension): ResolvedCapabilities;
}
```

**File:** `packages/core/src/extensions/manager.ts` (~400 lines)

```typescript
export class ExtensionManager {
  private extensions: Map<string, Extension>;
  private store: ExtensionStore;

  /** Install extension from source */
  async install(source: ExtensionSource): Promise<Extension>;

  /** Uninstall extension */
  async uninstall(name: string): Promise<void>;

  /** Enable/disable extension */
  async setEnabled(name: string, enabled: boolean): Promise<void>;

  /** List installed extensions */
  list(): Extension[];

  /** Get extension by name */
  get(name: string): Extension | undefined;

  /** Load all enabled extensions */
  async loadAll(): Promise<void>;
}

export type ExtensionSource =
  | { type: 'github'; repo: string; ref?: string }
  | { type: 'local'; path: string }
  | { type: 'link'; path: string }
  | { type: 'npm'; package: string; version?: string };
```

**File:** `packages/core/src/extensions/store.ts` (~200 lines)

Persistence layer for installed extensions:
- Install metadata (source, time, version)
- Enable/disable state
- Settings values

**Storage:** `~/.estela/extensions/{name}/`
```
~/.estela/extensions/
  my-extension/
    manifest.json
    install-metadata.json
    settings.json
    ...extension files
```

**File:** `packages/core/src/extensions/installers/` (~200 lines)

Install methods:
- `github.ts` - Clone from GitHub repo
- `local.ts` - Copy from local path
- `link.ts` - Symlink (development mode)

### 3.2 Trusted Folders (~300 lines)

**File:** `packages/core/src/security/trusted-folders.ts`

```typescript
export enum TrustLevel {
  TRUSTED = 'trusted',
  UNTRUSTED = 'untrusted',
}

export interface TrustConfig {
  /** Folder trust mappings */
  folders: Record<string, TrustLevel>;
  /** Whether trust checking is enabled */
  enabled: boolean;
}

export class TrustedFolders {
  private config: TrustConfig;

  constructor(configPath?: string) {
    // Default: ~/.estela/trusted-folders.json
  }

  /** Check if path is in a trusted folder */
  isTrusted(path: string): boolean | undefined;

  /** Set trust level for folder */
  setTrust(folderPath: string, level: TrustLevel): void;

  /** Remove trust setting */
  removeTrust(folderPath: string): void;

  /** List all trust settings */
  listTrust(): Record<string, TrustLevel>;

  /** Save config to disk */
  async save(): Promise<void>;

  /** Load config from disk */
  async load(): Promise<void>;
}
```

**Integration points:**
- Hook runner: Block project hooks in untrusted folders
- TOML commands: Block project commands in untrusted folders
- Extension loader: Respect trust settings
- Auto-approval: Factor trust level into decisions

### Sprint 3 Acceptance Criteria

- [ ] Extension manifest with capabilities
- [ ] Install from GitHub, local path, link
- [ ] Enable/disable extensions
- [ ] Extensions can provide commands, MCP servers, skills
- [ ] Trusted folders config at `~/.estela/trusted-folders.json`
- [ ] Trust check integrated with hooks and commands
- [ ] Project hooks blocked in untrusted folders
- [ ] 25+ unit tests

---

## Sprint 4: Chat Compression Enhancement

**Goal:** LLM-powered summarization with verification

### 4.1 LLM Compression (~600 lines)

**File:** `packages/core/src/context/compression.ts`

Enhance existing context compaction with Gemini CLI's three-phase approach:

```typescript
export interface CompressionConfig {
  /** Trigger compression at this % of context window. Default: 0.5 */
  threshold: number;
  /** Token budget for function responses. Default: 50000 */
  responseBudget: number;
  /** % of chat to preserve (recent). Default: 0.3 */
  preserveRatio: number;
}

export interface CompressionResult {
  messages: Message[];
  info: {
    originalTokens: number;
    newTokens: number;
    status: CompressionStatus;
  };
}

export enum CompressionStatus {
  COMPRESSED = 'compressed',
  NOOP = 'noop',
  FAILED_EMPTY = 'failed_empty',
  FAILED_INFLATED = 'failed_inflated',
}

export class ChatCompressor {
  constructor(
    private llmClient: LLMClient,
    private config: CompressionConfig,
  ) {}

  /** Three-phase compression */
  async compress(messages: Message[], tokenLimit: number): Promise<CompressionResult> {
    // Phase 1: Find split point and truncate large tool outputs
    // Phase 2: LLM summarization of older messages
    // Phase 3: Self-correction verification pass
  }
}
```

**Phase 1: Split & Truncate**
```typescript
/** Find safe split point (last user message without pending tool response) */
function findSplitPoint(messages: Message[], preserveRatio: number): number;

/** Truncate large tool outputs to budget */
function truncateResponses(
  messages: Message[],
  budget: number,
): Message[];
```

**Phase 2: LLM Summarization**
```typescript
const COMPRESSION_PROMPT = `Summarize the following conversation into a <state_snapshot>.
Include: all key decisions, file paths, code changes, current task state.
Format as structured sections for easy resumption.`;
```

**Phase 3: Self-Correction**
```typescript
const VERIFICATION_PROMPT = `Critically evaluate this summary.
Does it capture all: file modifications, pending tasks, key decisions?
Fix anything missing. Return the corrected summary.`;
```

**File:** `packages/core/src/context/compression-prompts.ts` (~100 lines)

Prompt templates for compression and verification.

### Sprint 4 Acceptance Criteria

- [ ] Compression triggers at configurable threshold (default 50%)
- [ ] Split point preserves recent context (30%)
- [ ] Large tool outputs truncated to budget
- [ ] LLM summarization produces `<state_snapshot>`
- [ ] Self-correction pass catches missing info
- [ ] Compression status reported (compressed/noop/failed)
- [ ] Hooks fire before compression (PreCompress)
- [ ] 15+ unit tests

---

## Summary

| Sprint | Files | New | Modified |
|--------|-------|-----|----------|
| 1 | Policy Engine + Message Bus | 7 | 2 |
| 2 | Session Resume + TOML Commands | 6 | 2 |
| 3 | Extension System + Trusted Folders | 8 | 3 |
| 4 | Chat Compression | 2 | 1 |
| **Total** | | **23** | **8** |

### New Files

```
packages/core/src/policy/
  engine.ts             # Priority-based rule engine
  rules.ts              # Built-in policy rules
  matcher.ts            # Pattern matching utilities
  index.ts              # Public exports

packages/core/src/bus/
  message-bus.ts        # Event-driven communication
  types.ts              # Message type definitions
  index.ts              # Public exports

packages/core/src/session/
  resume.ts             # Session persistence + resume
  selector.ts           # Session resolution strategies

packages/core/src/commands/
  toml-loader.ts        # TOML command discovery
  processor.ts          # Prompt processing pipeline
  index.ts              # Integration with slash commands

packages/core/src/extensions/
  manifest.ts           # Extension manifest schema
  loader.ts             # Extension loading
  manager.ts            # Lifecycle management
  store.ts              # Persistence layer
  installers/
    github.ts           # GitHub install
    local.ts            # Local path install
    link.ts             # Symlink install

packages/core/src/security/
  trusted-folders.ts    # Per-folder trust levels

packages/core/src/context/
  compression.ts        # LLM-powered compression
  compression-prompts.ts # Prompt templates
```

### Modified Files

```
packages/core/src/tools/registry.ts     # Message bus integration
packages/core/src/agent/loop.ts         # Bus events, compression trigger
packages/core/src/hooks/index.ts        # Trust check integration
packages/core/src/config/schema.ts      # New config sections
packages/core/src/slash-commands/       # TOML command registration
```

---

## Risk Assessment

| Risk | Impact | Mitigation |
|------|--------|------------|
| Policy engine conflicts with existing permissions | High | Gradual migration, fallback to current system |
| Extension security (malicious extensions) | High | Manifest validation, trust checks, sandboxing |
| Session file corruption | Medium | Atomic writes, backup before overwrite |
| Compression info loss | Medium | Self-correction pass, user confirmation |

---

*Created: 2026-02-05*
