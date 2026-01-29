# Advanced OpenCode Plugin Patterns

> Deep dive into sophisticated patterns from production plugins.

---

## Overview

This document captures advanced patterns discovered from studying the oh-my-opencode codebase (22.7K+ stars) and other production plugins. These patterns represent battle-tested solutions for complex multi-agent orchestration.

---

## 1. Background Agent System

### Architecture

The BackgroundManager class implements a sophisticated concurrent task execution system:

```typescript
class BackgroundManager {
  private tasks: Map<string, BackgroundTask>
  private notifications: Map<string, BackgroundTask[]>
  private pendingByParent: Map<string, Set<string>>
  private concurrencyManager: ConcurrencyManager

  async launch(input: LaunchInput): Promise<BackgroundTask>
  async resume(input: ResumeInput): Promise<BackgroundTask>
  async trackTask(input: TrackInput): Promise<BackgroundTask>
}
```

### Key Patterns

**Concurrency Control by Model/Provider:**
```typescript
private getConcurrencyKeyFromInput(input: LaunchInput): string {
  if (input.model) {
    return `${input.model.providerID}/${input.model.modelID}`
  }
  return input.agent
}
```

**Task Queuing with Rate Limiting:**
```typescript
private queuesByKey: Map<string, QueueItem[]> = new Map()
private processingKeys: Set<string> = new Set()

private async processKey(key: string): Promise<void> {
  if (this.processingKeys.has(key)) return
  this.processingKeys.add(key)

  try {
    const queue = this.queuesByKey.get(key)
    while (queue && queue.length > 0) {
      await this.concurrencyManager.acquire(key)
      await this.startTask(queue.shift())
    }
  } finally {
    this.processingKeys.delete(key)
  }
}
```

**Stability Detection (Polling):**
```typescript
// Wait for message stability after prompt
const POLL_INTERVAL_MS = 500
const MIN_STABILITY_TIME_MS = 10000
const STABILITY_POLLS_REQUIRED = 3

while (Date.now() - pollStart < MAX_POLL_TIME_MS) {
  const messagesCheck = await client.session.messages({ path: { id: sessionID } })
  const currentMsgCount = msgs.length

  if (currentMsgCount === lastMsgCount) {
    stablePolls++
    if (stablePolls >= STABILITY_POLLS_REQUIRED) break
  } else {
    stablePolls = 0
    lastMsgCount = currentMsgCount
  }
}
```

---

## 2. Dynamic Agent Prompt Builder

### Purpose

Dynamically construct agent prompts based on available agents, tools, and skills.

### Pattern

```typescript
interface AvailableAgent {
  name: BuiltinAgentName
  description: string
  metadata: AgentPromptMetadata
}

interface AgentPromptMetadata {
  category: "advisor" | "researcher" | "utility"
  cost: "FREE" | "CHEAP" | "EXPENSIVE"
  promptAlias: string
  triggers: Array<{ domain: string; trigger: string }>
  useWhen: string[]
  avoidWhen: string[]
}

function buildDynamicSisyphusPrompt(
  availableAgents: AvailableAgent[],
  availableTools: AvailableTool[] = [],
  availableSkills: AvailableSkill[] = [],
  availableCategories: AvailableCategory[] = []
): string
```

### Tool Selection Table Generation

```typescript
function buildToolSelectionTable(
  agents: AvailableAgent[],
  tools: AvailableTool[],
  _skills: AvailableSkill[]
): string {
  const rows = [
    "| Resource | Cost | When to Use |",
    "|----------|------|-------------|",
  ]

  if (tools.length > 0) {
    rows.push(`| ${formatToolsForPrompt(tools)} | FREE | Direct, Scope Clear |`)
  }

  const sortedAgents = [...agents].sort(
    (a, b) => costOrder[a.metadata.cost] - costOrder[b.metadata.cost]
  )

  for (const agent of sortedAgents) {
    rows.push(`| \`${agent.name}\` | ${agent.metadata.cost} | ${agent.description} |`)
  }

  return rows.join("\n")
}
```

---

## 3. Category + Skills Delegation System

### Architecture

Categories define model configurations; skills inject domain expertise.

```typescript
// Built-in categories with model configurations
const DEFAULT_CATEGORIES: Record<string, CategoryConfig> = {
  "visual-engineering": {
    model: "google/gemini-2.5-flash",
    description: "Frontend/UI development",
  },
  "ultrabrain": {
    model: "anthropic/claude-opus-4-5",
    description: "Complex reasoning, architecture",
    thinking: { type: "enabled", budgetTokens: 32000 },
  },
  "quick": {
    model: "anthropic/claude-sonnet-4-5",
    description: "Fast, efficient tasks",
  },
}

// Category prompt appendixes
const CATEGORY_PROMPT_APPENDS: Record<string, string> = {
  "visual-engineering": `You are a frontend specialist...`,
  "ultrabrain": `Use extended thinking for complex problems...`,
}
```

### Delegation with Skills

```typescript
delegate_task({
  category: "visual-engineering",
  load_skills: ["playwright", "frontend-ui-ux"],
  prompt: "Build a responsive dashboard component...",
  run_in_background: false,
})
```

### Model Resolution with Fallback

```typescript
function resolveModelWithFallback(options: {
  userModel?: string
  fallbackChain: string[]
  availableModels: Set<string>
  systemDefaultModel: string
}): { model: string; source: string } {
  const { userModel, fallbackChain, availableModels, systemDefaultModel } = options

  // User override takes priority
  if (userModel && availableModels.has(userModel)) {
    return { model: userModel, source: "override" }
  }

  // Try fallback chain
  for (const candidate of fallbackChain) {
    if (availableModels.has(candidate)) {
      return { model: candidate, source: "provider-fallback" }
    }
  }

  // System default
  return { model: systemDefaultModel, source: "system-default" }
}
```

---

## 4. Boulder State (Persistent Plan Tracking)

### Purpose

Track active plans across sessions with file-based persistence.

### Structure

```typescript
interface BoulderState {
  active_plan: string        // Path to .md plan file
  started_at: string         // ISO timestamp
  session_ids: string[]      // Sessions working on this plan
  plan_name: string          // Human-readable name
}

interface PlanProgress {
  total: number
  completed: number
  isComplete: boolean
}
```

### Storage Pattern

```typescript
const BOULDER_DIR = ".sisyphus"
const BOULDER_FILE = "boulder.json"
const PROMETHEUS_PLANS_DIR = ".sisyphus/plans"

function readBoulderState(directory: string): BoulderState | null {
  const filePath = join(directory, BOULDER_DIR, BOULDER_FILE)
  if (!existsSync(filePath)) return null
  return JSON.parse(readFileSync(filePath, "utf-8"))
}

function writeBoulderState(directory: string, state: BoulderState): boolean {
  const filePath = join(directory, BOULDER_DIR, BOULDER_FILE)
  mkdirSync(dirname(filePath), { recursive: true })
  writeFileSync(filePath, JSON.stringify(state, null, 2))
  return true
}
```

### Plan Progress Parsing

```typescript
function getPlanProgress(planPath: string): PlanProgress {
  const content = readFileSync(planPath, "utf-8")

  // Match markdown checkboxes
  const unchecked = content.match(/^[-*]\s*\[\s*\]/gm) || []
  const checked = content.match(/^[-*]\s*\[[xX]\]/gm) || []

  const total = unchecked.length + checked.length
  const completed = checked.length

  return {
    total,
    completed,
    isComplete: total === 0 || completed === total,
  }
}
```

---

## 5. Hook Composition Pattern

### Hook Factory Pattern

```typescript
interface HookOptions {
  experimental?: ExperimentalConfig
}

function createContextWindowMonitorHook(ctx: PluginInput): {
  event: (input: EventInput) => Promise<void>
  "tool.execute.after": (input: ToolInput, output: ToolOutput) => Promise<void>
} {
  let tokenCount = 0

  return {
    async event(input) {
      if (input.event.type === "session.created") {
        tokenCount = 0
      }
    },

    async "tool.execute.after"(input, output) {
      if (output.usage) {
        tokenCount += output.usage.totalTokens
        if (tokenCount > WARNING_THRESHOLD) {
          await notifyContextWarning(ctx, tokenCount)
        }
      }
    },
  }
}
```

### Hook Composition in Plugin Entry

```typescript
const OhMyOpenCodePlugin: Plugin = async (ctx) => {
  const contextWindowMonitor = createContextWindowMonitorHook(ctx)
  const sessionRecovery = createSessionRecoveryHook(ctx)
  const commentChecker = createCommentCheckerHooks(config)

  return {
    event: async (input) => {
      await contextWindowMonitor?.event(input)
      await sessionRecovery?.event(input)
      // ... more hooks
    },

    "tool.execute.after": async (input, output) => {
      await contextWindowMonitor?.["tool.execute.after"](input, output)
      await commentChecker?.["tool.execute.after"](input, output)
    },
  }
}
```

---

## 6. Session State Management

### Tracking Subagent Sessions

```typescript
// Global session tracking
export const subagentSessions = new Set<string>()

// Session-to-agent mapping
const sessionAgentMap = new Map<string, string>()

export function setSessionAgent(sessionID: string, agent: string): void {
  sessionAgentMap.set(sessionID, agent)
}

export function getSessionAgent(sessionID: string): string | undefined {
  return sessionAgentMap.get(sessionID)
}

export function clearSessionAgent(sessionID: string): void {
  sessionAgentMap.delete(sessionID)
}
```

### Main Session Tracking

```typescript
let mainSessionID: string | undefined

export function setMainSession(id: string | undefined): void {
  mainSessionID = id
}

export function getMainSessionID(): string | undefined {
  return mainSessionID
}
```

---

## 7. Sisyphus Orchestrator Prompt Structure

### Phase-Based Execution

```
<Role>
You are "Sisyphus" - Powerful AI Agent with orchestration capabilities.
</Role>

<Behavior_Instructions>
## Phase 0 - Intent Gate (EVERY message)
- Key Triggers check
- Request classification
- Ambiguity check
- Delegation check (MANDATORY before acting)

## Phase 1 - Codebase Assessment (Open-ended tasks)
- Check config files, patterns
- Classify: Disciplined/Transitional/Legacy/Greenfield

## Phase 2A - Exploration & Research
- Tool selection table
- Parallel execution (explore + librarian)
- Background result collection

## Phase 2B - Implementation
- Create todos IMMEDIATELY (2+ steps)
- Delegation with mandatory 6-section prompts
- Session continuity with resume

## Phase 2C - Failure Recovery
- 3 consecutive failures → STOP, REVERT, CONSULT Oracle

## Phase 3 - Completion
- All todos done
- Diagnostics clean
- background_cancel(all=true)
</Behavior_Instructions>

<Task_Management>
## Todo Management (CRITICAL)
- ALWAYS create todos before multi-step tasks
- Mark in_progress before starting
- Mark completed IMMEDIATELY after each step
</Task_Management>

<Tone_and_Style>
- Start work immediately (no acknowledgments)
- No flattery, no status updates
- Match user's communication style
</Tone_and_Style>

<Constraints>
## Hard Blocks (NEVER violate)
- Type error suppression
- Commit without request
- Speculate about unread code
- Leave code in broken state
</Constraints>
```

---

## 8. Oracle Consultation Pattern

### Agent Definition

```typescript
function createOracleAgent(model: string): AgentConfig {
  const restrictions = createAgentToolRestrictions([
    "write", "edit", "task", "delegate_task"  // Read-only
  ])

  return {
    description: "Read-only high-IQ reasoning specialist",
    mode: "subagent",
    model,
    temperature: 0.1,  // Deterministic
    ...restrictions,
    prompt: ORACLE_SYSTEM_PROMPT,
    thinking: { type: "enabled", budgetTokens: 32000 },
  }
}
```

### Prompt Structure

```
You are a strategic technical advisor with deep reasoning capabilities.

## Decision Framework
- Bias toward simplicity
- Leverage what exists
- Prioritize developer experience
- One clear path (single recommendation)
- Match depth to complexity
- Signal the investment (Quick/Short/Medium/Large)
- Know when to stop

## Response Structure
Essential: Bottom line + Action plan + Effort estimate
Expanded: Why this approach + Watch out for
Edge cases: Escalation triggers + Alternative sketch
```

### Metadata for Dynamic Prompts

```typescript
const ORACLE_PROMPT_METADATA: AgentPromptMetadata = {
  category: "advisor",
  cost: "EXPENSIVE",
  triggers: [
    { domain: "Architecture decisions", trigger: "Multi-system tradeoffs" },
    { domain: "Self-review", trigger: "After significant implementation" },
    { domain: "Hard debugging", trigger: "After 2+ failed attempts" },
  ],
  useWhen: [
    "Complex architecture design",
    "2+ failed fix attempts",
    "Security/performance concerns",
  ],
  avoidWhen: [
    "Simple file operations",
    "First attempt at any fix",
    "Trivial decisions",
  ],
}
```

---

## 9. Skills System (Anthropic Spec)

### Skill Discovery

```typescript
const SKILL_LOCATIONS = [
  ".opencode/skills/",           // Project
  "~/.opencode/skills/",         // User
  "~/.config/opencode/skills/",  // Config
]

interface Skill {
  name: string
  description: string
  template: string  // Markdown content
  location: "user" | "project" | "plugin"
}
```

### Skill Loading Tool

```typescript
tool({
  description: "Load one or more skills into the chat",
  args: {
    skill_names: z.array(z.string()),
  },
  async execute(args, ctx) {
    const results = await api.loadSkill(args.skill_names)

    for await (const skill of results.loaded) {
      await sendPrompt(renderer({ data: skill, type: "Skill" }), {
        sessionId: ctx.sessionID,
      })
    }

    return JSON.stringify({
      loaded: results.loaded.map(s => s.toolName),
      not_found: results.notFound,
    })
  },
})
```

### Model-Aware Prompt Formatting

```typescript
function getModelFormat(options: {
  modelId?: string
  providerId?: string
  config: Config
}): "xml" | "json" | "markdown" {
  const { modelId, providerId, config } = options

  // Check config for model-specific renderers
  if (config.modelRenderers?.[`${providerId}/${modelId}`]) {
    return config.modelRenderers[`${providerId}/${modelId}`]
  }

  // Provider defaults
  if (providerId === "anthropic") return "xml"
  if (providerId === "openai") return "json"

  return "markdown"
}
```

---

## 10. Configuration Schema Pattern

### Zod-Based Validation

```typescript
import { z } from "zod"

const AgentOverrideConfigSchema = z.object({
  model: z.string().optional(),
  variant: z.string().optional(),
  category: z.string().optional(),
  skills: z.array(z.string()).optional(),
  temperature: z.number().min(0).max(2).optional(),
  prompt_append: z.string().optional(),
  tools: z.record(z.string(), z.boolean()).optional(),
  disable: z.boolean().optional(),
})

const OhMyOpenCodeConfigSchema = z.object({
  $schema: z.string().optional(),
  disabled_mcps: z.array(McpNameSchema).optional(),
  disabled_agents: z.array(AgentNameSchema).optional(),
  disabled_hooks: z.array(HookNameSchema).optional(),
  agents: AgentOverridesSchema.optional(),
  categories: CategoriesConfigSchema.optional(),
  experimental: ExperimentalConfigSchema.optional(),
})

export type OhMyOpenCodeConfig = z.infer<typeof OhMyOpenCodeConfigSchema>
```

### Hook Configuration Schema

```typescript
const HookNameSchema = z.enum([
  "todo-continuation-enforcer",
  "context-window-monitor",
  "session-recovery",
  "session-notification",
  "comment-checker",
  "tool-output-truncator",
  "directory-agents-injector",
  "directory-readme-injector",
  "think-mode",
  "ralph-loop",
  // ... more hooks
])
```

---

## 11. Toast/Notification Manager

### Task Progress Tracking

```typescript
interface TaskToast {
  id: string
  description: string
  agent: string
  isBackground: boolean
  status: "queued" | "running" | "completed" | "failed"
  skills?: string[]
  modelInfo?: ModelFallbackInfo
}

class TaskToastManager {
  private tasks: Map<string, TaskToast>
  private client: OpencodeClient

  addTask(task: TaskToast): void
  updateTask(id: string, status: TaskToast["status"]): void
  removeTask(id: string): void
  showCompletionToast(info: CompletionInfo): void
}
```

---

## 12. Resume Pattern for Session Continuity

### Why Resume Matters

```typescript
// WRONG: Starting fresh loses all context
delegate_task({
  category: "quick",
  prompt: "Fix the type error in auth.ts...",
})

// CORRECT: Resume preserves everything
delegate_task({
  resume: "ses_abc123",
  prompt: "Fix: Type error on line 42",
})
```

### Benefits:
- Subagent has FULL conversation context preserved
- No repeated file reads, exploration, or setup
- Saves 70%+ tokens on follow-ups
- Subagent knows what it already tried/learned

---

## Application to Delta9

### Patterns to Adopt

1. **Background Agent Manager** - For parallel Operator execution
2. **Category + Skills System** - For Commander's task routing
3. **Boulder State** - Inspiration for mission.json persistence
4. **Dynamic Prompt Builder** - Generate Council prompts based on config
5. **Resume Pattern** - Continue tasks after context compaction
6. **Hook Composition** - Modular event handling

### Unique Differentiators

1. **Council System** - Multi-model deliberation (not in oh-my-opencode)
2. **Protected Commander** - Never writes code (oh-my Sisyphus can)
3. **Validation Gate** - Dedicated verification agent
4. **Consensus Building** - Council synthesis logic
5. **Checkpoint/Rollback** - Git-based recovery

---

## 13. LSP Tool Integration

### Purpose

Integrate Language Server Protocol features directly into agent tools for code intelligence.

### Implementation

```typescript
import { type ToolDefinition, tool } from '@opencode-ai/plugin/tool';

export const lsp_goto_definition: ToolDefinition = tool({
  description: 'Jump to symbol definition. Find WHERE something is defined.',
  args: {
    filePath: tool.schema.string().describe('Absolute path to the file'),
    line: tool.schema.number().min(1).describe('1-based line number'),
    character: tool.schema.number().min(0).describe('0-based character offset'),
  },
  execute: async (args) => {
    const result = await withLspClient(args.filePath, async (client) => {
      return await client.definition(args.filePath, args.line, args.character);
    });

    if (!result) return 'No definition found';
    const locations = Array.isArray(result) ? result : [result];
    return locations.map(formatLocation).join('\n');
  },
});

export const lsp_find_references: ToolDefinition = tool({
  description: 'Find ALL usages/references of a symbol across the entire workspace.',
  args: {
    filePath: tool.schema.string(),
    line: tool.schema.number().min(1),
    character: tool.schema.number().min(0),
    includeDeclaration: tool.schema.boolean().optional(),
  },
  execute: async (args) => {
    const result = await withLspClient(args.filePath, async (client) => {
      return await client.references(args.filePath, args.line, args.character, true);
    });
    // ... format and return
  },
});

export const lsp_diagnostics: ToolDefinition = tool({
  description: 'Get errors, warnings, hints from language server BEFORE running build.',
  args: {
    filePath: tool.schema.string(),
    severity: tool.schema.enum(['error', 'warning', 'information', 'hint', 'all']).optional(),
  },
  execute: async (args) => { /* ... */ },
});

export const lsp_rename: ToolDefinition = tool({
  description: 'Rename symbol across entire workspace. APPLIES changes to all files.',
  args: {
    filePath: tool.schema.string(),
    line: tool.schema.number(),
    character: tool.schema.number(),
    newName: tool.schema.string(),
  },
  execute: async (args) => { /* ... */ },
});
```

---

## 14. AST-Grep Code Transformation

### Purpose

Structural code search and replace using AST-aware patterns.

### Pattern

```typescript
export const ast_grep_search: ToolDefinition = tool({
  description:
    'Search code patterns across filesystem using AST-aware matching. ' +
    'Supports 25 languages. Use meta-variables: $VAR (single node), $$$ (multiple nodes). ' +
    'IMPORTANT: Patterns must be complete AST nodes (valid code). ' +
    "Examples: 'console.log($MSG)', 'def $FUNC($$$):', 'async function $NAME($$$)'",
  args: {
    pattern: tool.schema.string().describe('AST pattern with meta-variables'),
    lang: tool.schema.enum(CLI_LANGUAGES).describe('Target language'),
    paths: tool.schema.array(tool.schema.string()).optional(),
    globs: tool.schema.array(tool.schema.string()).optional(),
    context: tool.schema.number().optional(),
  },
  execute: async (args, context) => {
    const result = await runSg({
      pattern: args.pattern,
      lang: args.lang as CliLanguage,
      paths: args.paths,
      globs: args.globs,
      context: args.context,
    });
    return formatSearchResult(result);
  },
});

export const ast_grep_replace: ToolDefinition = tool({
  description:
    'Replace code patterns with AST-aware rewriting. Dry-run by default. ' +
    "Example: pattern='console.log($MSG)' rewrite='logger.info($MSG)'",
  args: {
    pattern: tool.schema.string().describe('AST pattern to match'),
    rewrite: tool.schema.string().describe('Replacement pattern'),
    lang: tool.schema.enum(CLI_LANGUAGES),
    dryRun: tool.schema.boolean().optional().describe('Preview only (default: true)'),
  },
  execute: async (args) => {
    const result = await runSg({
      pattern: args.pattern,
      rewrite: args.rewrite,
      lang: args.lang,
      updateAll: args.dryRun === false,
    });
    return formatReplaceResult(result, args.dryRun !== false);
  },
});
```

---

## 15. Phase Reminder Hook (Instruction Injection)

### Purpose

Combat instruction-following degradation in long contexts by injecting reminders.

### Research Basis

> "LLMs Get Lost In Multi-Turn Conversation" (arXiv:2505.06120) shows ~40% compliance drop after 2-3 turns without reminders.

### Implementation

```typescript
const PHASE_REMINDER = `<reminder>⚠️ MANDATORY: Understand→DELEGATE(!)→Split-and-Parallelize(?)→Plan→Execute→Verify
Available Specialist: @oracle @librarian @explorer @designer @fixer
</reminder>`;

export function createPhaseReminderHook() {
  return {
    // Uses experimental hook - doesn't show in UI
    'experimental.chat.messages.transform': async (
      _input: Record<string, never>,
      output: { messages: MessageWithParts[] },
    ): Promise<void> => {
      const { messages } = output;
      if (messages.length === 0) return;

      // Find last user message
      let lastUserMessageIndex = -1;
      for (let i = messages.length - 1; i >= 0; i--) {
        if (messages[i].info.role === 'user') {
          lastUserMessageIndex = i;
          break;
        }
      }

      if (lastUserMessageIndex === -1) return;

      const lastUserMessage = messages[lastUserMessageIndex];

      // Only inject for orchestrator
      const agent = lastUserMessage.info.agent;
      if (agent && agent !== 'orchestrator') return;

      // Prepend reminder to text
      const textPartIndex = lastUserMessage.parts.findIndex(p => p.type === 'text');
      if (textPartIndex === -1) return;

      const originalText = lastUserMessage.parts[textPartIndex].text ?? '';
      lastUserMessage.parts[textPartIndex].text = `${PHASE_REMINDER}\n\n---\n\n${originalText}`;
    },
  };
}
```

---

## 16. Plugin Composition Utility

### Purpose

Compose multiple plugins into a single plugin with merged hooks.

### Implementation

```typescript
import type { Plugin, PluginInput, Hooks } from "@opencode-ai/plugin"

export const compose = (plugins: Array<Plugin>): Plugin => {
  return async (input: PluginInput): Promise<Hooks> => {
    const initializedPlugins = await Promise.all(
      plugins.map(async (plugin) => await plugin(input))
    );

    return {
      event: async ({ event }) => {
        for (const plugin of initializedPlugins) {
          if (plugin.event) await plugin.event({ event });
        }
      },

      "chat.message": async (input, output) => {
        for (const plugin of initializedPlugins) {
          if (plugin["chat.message"]) await plugin["chat.message"](input, output);
        }
      },

      "tool.execute.before": async (input, output) => {
        for (const plugin of initializedPlugins) {
          if (plugin["tool.execute.before"]) await plugin["tool.execute.before"](input, output);
        }
      },

      "tool.execute.after": async (input, output) => {
        for (const plugin of initializedPlugins) {
          if (plugin["tool.execute.after"]) await plugin["tool.execute.after"](input, output);
        }
      },
    };
  };
};
```

---

## 17. Command Loader (Markdown-Based Commands)

### Purpose

Load slash commands from markdown files with YAML frontmatter.

### Implementation

```typescript
interface CommandFrontmatter {
  description?: string;
  agent?: string;
  model?: string;
  subtask?: boolean;
}

interface ParsedCommand {
  name: string;
  frontmatter: CommandFrontmatter;
  template: string;
}

function parseFrontmatter(content: string): { frontmatter: CommandFrontmatter; body: string } {
  const frontmatterRegex = /^---\n([\s\S]*?)\n---\n([\s\S]*)$/;
  const match = content.match(frontmatterRegex);

  if (!match) return { frontmatter: {}, body: content.trim() };

  const [, yamlContent, body] = match;
  const frontmatter: CommandFrontmatter = {};

  for (const line of yamlContent.split('\n')) {
    const colonIndex = line.indexOf(':');
    if (colonIndex === -1) continue;
    const key = line.slice(0, colonIndex).trim();
    const value = line.slice(colonIndex + 1).trim();

    if (key === 'description') frontmatter.description = value;
    if (key === 'agent') frontmatter.agent = value;
    if (key === 'model') frontmatter.model = value;
    if (key === 'subtask') frontmatter.subtask = value === 'true';
  }

  return { frontmatter, body: body.trim() };
}

async function loadCommands(): Promise<ParsedCommand[]> {
  const commands: ParsedCommand[] = [];
  const commandDir = path.join(import.meta.dir, 'command');
  const glob = new Bun.Glob('**/*.md');

  for await (const file of glob.scan({ cwd: commandDir, absolute: true })) {
    const content = await Bun.file(file).text();
    const { frontmatter, body } = parseFrontmatter(content);
    const name = path.relative(commandDir, file).replace(/\.md$/, '').replace(/\//g, '-');

    commands.push({ name, frontmatter, template: body });
  }

  return commands;
}
```

### Config Hook Registration

```typescript
export const MyPlugin: Plugin = async () => {
  const commands = await loadCommands();

  return {
    async config(config) {
      config.command = config.command ?? {};

      for (const cmd of commands) {
        config.command[cmd.name] = {
          template: cmd.template,
          description: cmd.frontmatter.description,
          agent: cmd.frontmatter.agent,
          model: cmd.frontmatter.model,
          subtask: cmd.frontmatter.subtask,
        };
      }
    },
  };
};
```

---

## 18. Notification Plugin (Event-Based)

### Purpose

Send desktop notifications when agent becomes idle after work.

### Implementation

```typescript
export interface NotificationPluginOptions {
  idleTime?: number;  // Default: 60000 (1 minute)
  notificationCommand?: string[];
  additionalCommands?: string[][];
  getMessage?: (params: { sessionID: string; client: Client }) => Promise<string> | string;
}

export const notification = (options: NotificationPluginOptions = {}): Plugin => {
  const idleTime = options.idleTime ?? 1000 * 60;
  const notificationCommand = options.notificationCommand ?? ["notify-send", "--app-name", "opencode"];
  const getMessage = options.getMessage ?? defaultGetMessage;

  let lastUserMessage = Date.now();

  return ({ client }) => ({
    event: async ({ event }) => {
      if (event.type === "message.updated" && event.properties.info.role === "user") {
        lastUserMessage = Date.now();
      }

      if (event.type === "session.idle") {
        const timeSince = Date.now() - lastUserMessage;
        if (timeSince < idleTime) return;

        const message = await getMessage({
          sessionID: event.properties.sessionID,
          client,
        });

        Bun.spawnSync([...notificationCommand, message]);
      }
    },
  });
};
```

---

## 19. Inspector Plugin (Debugging UI)

### Purpose

Real-time debugging web UI showing all hook activity via SSE.

### Implementation

```typescript
export const inspector = (options: { port?: number } = {}): Plugin => {
  const port = options.port ?? 6969;

  return () => {
    const hooksHistory: Array<unknown> = [];
    const activeControllers = new Set<ReadableStreamController<unknown>>();

    function broadcast(data: unknown) {
      hooksHistory.push(data);
      for (const controller of activeControllers) {
        controller.enqueue(data);
      }
    }

    const app = new Hono()
      .get("/", (c) => c.text(html, 200, { "Content-Type": "text/html" }))
      .get("/hooks", (c) => streamSSE(c, async (sse) => {
        const hooksStream = new ReadableStream<unknown>({
          start(controller) {
            activeControllers.add(controller);
            for (const hook of hooksHistory) controller.enqueue(hook);
            sse.onAbort(() => activeControllers.delete(controller));
          },
        });

        const reader = hooksStream.getReader();
        while (true) {
          const { value, done } = await reader.read();
          if (done) break;
          await sse.writeSSE({ data: JSON.stringify(value), event: "hook" });
        }
      }));

    const server = Bun.serve({ fetch: app.fetch, port });

    return {
      event: (input) => broadcast({ hook: "event", input }),
      "chat.message": (input, output) => broadcast({ hook: "chat.message", input, output }),
      "tool.execute.after": (input, output) => broadcast({ hook: "tool.execute.after", input, output }),
    };
  };
};
```

---

## 20. Skills with MCP Integration

### Purpose

Skills can provide MCP servers, and tools can invoke them dynamically.

### Implementation

```typescript
interface SkillDefinition {
  name: string;
  description: string;
  template: string;
  mcpConfig?: Record<string, McpServerConfig>;  // Skills can provide MCPs
}

export function createSkillTools(manager: SkillMcpManager, pluginConfig?: PluginConfig) {
  // Main skill loader
  const omos_skill: ToolDefinition = tool({
    description: 'Load a skill and its MCP capabilities',
    args: {
      name: tool.schema.string().describe('The skill identifier'),
    },
    async execute(args, toolContext) {
      const agentName = toolContext?.agent ?? 'orchestrator';
      const skillDefinition = getSkillByName(args.name);

      if (!skillDefinition) throw new Error(`Skill "${args.name}" not found`);
      if (!canAgentUseSkill(agentName, args.name, pluginConfig)) {
        throw new Error(`Agent "${agentName}" cannot use skill "${args.name}"`);
      }

      const output = [`## Skill: ${skillDefinition.name}`, '', skillDefinition.template.trim()];

      // Include MCP capabilities if skill provides them
      if (skillDefinition.mcpConfig) {
        const mcpInfo = await formatMcpCapabilities(skillDefinition, manager, sessionId, agentName);
        if (mcpInfo) output.push(mcpInfo);
      }

      return output.join('\n');
    },
  });

  // MCP tool invoker
  const omos_skill_mcp: ToolDefinition = tool({
    description: 'Invoke an MCP tool provided by a skill',
    args: {
      skillName: tool.schema.string(),
      mcpName: tool.schema.string(),
      toolName: tool.schema.string(),
      toolArgs: tool.schema.record(tool.schema.string(), tool.schema.any()).optional(),
    },
    async execute(args, toolContext) {
      const agentName = toolContext?.agent ?? 'orchestrator';

      // Validate permissions
      if (!canAgentUseSkill(agentName, args.skillName, pluginConfig)) {
        throw new Error(`Agent cannot use skill "${args.skillName}"`);
      }
      if (!canAgentUseMcp(agentName, args.mcpName, pluginConfig)) {
        throw new Error(`Agent cannot use MCP "${args.mcpName}"`);
      }

      const config = skillDefinition.mcpConfig[args.mcpName];
      return await manager.callTool(info, config, args.toolName, args.toolArgs || {});
    },
  });

  return { omos_skill, omos_skill_list, omos_skill_mcp };
}
```

---

## 21. Streamlined Orchestrator (oh-my-opencode-slim)

### Purpose

Token-efficient orchestrator with clear phase-based workflow.

### Agent Definition

```typescript
const ORCHESTRATOR_PROMPT = `<Role>
You are an AI coding orchestrator.

**You are excellent in finding the best path towards achieving user's goals while optimizing speed, reliability, quality and cost.**
**You are excellent in utilizing parallel background tasks and flow wisely for increased efficiency.**
</Role>

<Agents>
@explorer - Rapid repo search with glob, grep, AST queries (read-only)
@librarian - Documentation and library research (read-only)
@oracle - Architecture, debugging, strategic review (advisory only)
@designer - UI/UX design leader (executes aesthetic work)
@fixer - Fast, cost-effective implementation (execution only)
</Agents>

<Workflow>
## Phase 1: Understand
Parse request thoroughly. Identify explicit and implicit needs.

## Phase 2: Best Path Analysis
Evaluate by Quality, Speed, Cost, Reliability.

## Phase 3: Delegation Gate (MANDATORY)
STOP. Before ANY implementation, review agent delegation rules.
Each specialist delivers 10x better results in their domain.

## Phase 4: Parallelization Strategy
Can independent research tasks run simultaneously?
Consider task dependencies: what MUST finish first?

## Phase 5: Plan & Execute
1. Create todo lists
2. Fire background research in parallel
3. Delegate implementation to specialists
4. Only do work yourself if NO specialist applies

## Phase 6: Verify
- Run lsp_diagnostics
- Verify all delegated tasks completed
- Confirm solution meets original requirements
</Workflow>

## Communication Style
- Be concise, no preamble
- No flattery
- When user is wrong: state concern, offer alternative, ask if proceed
`;

export function createOrchestratorAgent(model: string, customPrompt?: string): AgentDefinition {
  return {
    name: 'orchestrator',
    description: 'AI coding orchestrator that delegates to specialist agents',
    config: {
      model,
      temperature: 0.1,
      prompt: customPrompt ?? ORCHESTRATOR_PROMPT,
    },
  };
}
```

### Slim Plugin Structure

```typescript
const OhMyOpenCodeLite: Plugin = async (ctx) => {
  const config = loadPluginConfig(ctx.directory);
  const agents = getAgentConfigs(config);
  const backgroundManager = new BackgroundTaskManager(ctx, tmuxConfig, config);
  const mcps = createBuiltinMcps(config.disabled_mcps);
  const skillTools = createSkillTools(skillMcpManager, config);

  return {
    name: 'oh-my-opencode-slim',
    agent: agents,
    tool: {
      ...backgroundTools,
      lsp_goto_definition,
      lsp_find_references,
      lsp_diagnostics,
      lsp_rename,
      grep,
      ast_grep_search,
      ast_grep_replace,
      ...skillTools,
    },
    mcp: mcps,

    // Runtime config modification
    config: async (opencodeConfig) => {
      opencodeConfig.default_agent = 'orchestrator';
      // Merge agents, MCPs, set up permissions...
    },

    // Hook composition
    event: async (input) => {
      await autoUpdateChecker.event(input);
      await tmuxSessionManager.onSessionCreated(input.event);
    },

    'experimental.chat.messages.transform': phaseReminderHook['experimental.chat.messages.transform'],
    'tool.execute.after': postReadNudgeHook['tool.execute.after'],
  };
};
```

---

## 22. Agent Archetypes

### Explorer (Search Specialist)

Read-only search agent with multiple search tools.

```typescript
const EXPLORER_PROMPT = `You are Explorer - a fast codebase navigation specialist.

**Role**: Quick contextual grep for codebases. Answer "Where is X?", "Find Y", "Which file has Z".

**Tools Available**:
- **grep**: Fast regex content search (powered by ripgrep). Use for text patterns.
- **glob**: File pattern matching. Use to find files by name/extension.
- **ast_grep_search**: AST-aware structural search (25 languages).
  - Meta-variables: $VAR (single node), $$$ (multiple nodes)
  - Patterns must be complete AST nodes

**When to use which**:
- **Text/regex patterns**: grep
- **Structural patterns**: ast_grep_search
- **File discovery**: glob

**Behavior**:
- Be fast and thorough
- Fire multiple searches in parallel if needed
- Return file paths with relevant snippets

**Output Format**:
<results>
<files>
- /path/to/file.ts:42 - Brief description
</files>
<answer>
Concise answer to the question
</answer>
</results>

**Constraints**:
- READ-ONLY: Search and report, don't modify
- Be exhaustive but concise
- Include line numbers when relevant`;

export function createExplorerAgent(model: string): AgentDefinition {
  return {
    name: 'explorer',
    description: "Fast codebase search. Use for 'where is X?' questions.",
    config: { model, temperature: 0.1, prompt: EXPLORER_PROMPT },
  };
}
```

### Fixer (Execution Specialist)

Fast implementation agent with no research/delegation.

```typescript
const FIXER_PROMPT = `You are Fixer - a fast, focused implementation specialist.

**Role**: Execute code changes efficiently. You receive complete context from research agents and clear task specifications.

**Behavior**:
- Execute the task specification provided by the Orchestrator
- Use the research context (file paths, documentation, patterns) provided
- Read files before edit/write tools and gather exact content
- Be fast and direct - no research, no delegation
- Run tests/lsp_diagnostics when relevant
- Report completion with summary of changes

**Constraints**:
- NO external research (no websearch, context7, grep_app)
- NO delegation (no background_task)
- No multi-step planning; minimal execution sequence ok
- If context insufficient, read listed files; only ask for truly missing inputs

**Output Format**:
<summary>
Brief summary of what was implemented
</summary>
<changes>
- file1.ts: Changed X to Y
- file2.ts: Added Z function
</changes>
<verification>
- Tests passed: [yes/no/skip reason]
- LSP diagnostics: [clean/errors found/skip reason]
</verification>`;

export function createFixerAgent(model: string): AgentDefinition {
  return {
    name: 'fixer',
    description: 'Fast implementation. Receives context, executes changes.',
    config: { model, temperature: 0.2, prompt: FIXER_PROMPT },
  };
}
```

---

## 23. Tmux Session Manager

### Purpose

Manage tmux panes for parallel subagent sessions.

### Implementation

```typescript
interface TrackedSession {
  sessionId: string;
  paneId: string;
  parentId: string;
  title: string;
  createdAt: number;
  lastSeenAt: number;
  missingSince?: number;
}

const SESSION_TIMEOUT_MS = 10 * 60 * 1000; // 10 minutes
const SESSION_MISSING_GRACE_MS = POLL_INTERVAL * 3;

export class TmuxSessionManager {
  private sessions = new Map<string, TrackedSession>();
  private pollInterval?: ReturnType<typeof setInterval>;
  private enabled = false;

  constructor(ctx: PluginInput, tmuxConfig: TmuxConfig) {
    this.enabled = tmuxConfig.enabled && isInsideTmux();
  }

  async onSessionCreated(event: SessionCreatedEvent): Promise<void> {
    if (!this.enabled) return;
    if (event.type !== 'session.created') return;

    const info = event.properties?.info;
    if (!info?.id || !info?.parentID) return; // Not a child session

    const sessionId = info.id;
    if (this.sessions.has(sessionId)) return; // Already tracked

    // Spawn tmux pane for child session
    const paneResult = await spawnTmuxPane(
      sessionId,
      info.title ?? 'Subagent',
      this.tmuxConfig,
      this.serverUrl,
    );

    if (paneResult.success && paneResult.paneId) {
      this.sessions.set(sessionId, {
        sessionId,
        paneId: paneResult.paneId,
        parentId: info.parentID,
        title: info.title ?? 'Subagent',
        createdAt: Date.now(),
        lastSeenAt: Date.now(),
      });
      this.startPolling();
    }
  }

  private async pollSessions(): Promise<void> {
    if (this.sessions.size === 0) {
      this.stopPolling();
      return;
    }

    const statusResult = await this.client.session.status();
    const allStatuses = statusResult.data ?? {};

    for (const [sessionId, tracked] of this.sessions.entries()) {
      const status = allStatuses[sessionId];
      const isIdle = status?.type === 'idle';

      // Track missing sessions
      if (status) {
        tracked.lastSeenAt = Date.now();
        tracked.missingSince = undefined;
      } else if (!tracked.missingSince) {
        tracked.missingSince = Date.now();
      }

      const missingTooLong = tracked.missingSince &&
        Date.now() - tracked.missingSince >= SESSION_MISSING_GRACE_MS;
      const isTimedOut = Date.now() - tracked.createdAt > SESSION_TIMEOUT_MS;

      if (isIdle || missingTooLong || isTimedOut) {
        await this.closeSession(sessionId);
      }
    }
  }

  private async closeSession(sessionId: string): Promise<void> {
    const tracked = this.sessions.get(sessionId);
    if (!tracked) return;

    await closeTmuxPane(tracked.paneId);
    this.sessions.delete(sessionId);

    if (this.sessions.size === 0) this.stopPolling();
  }
}
```

### Key Features

- **Automatic pane spawning**: Child sessions get their own terminal pane
- **Session polling**: Monitors session status for completion
- **Graceful cleanup**: Closes panes when sessions idle or timeout
- **Missing session handling**: Grace period before closing orphaned panes

---

## 24. Configuration Loader Pattern

### Purpose

Load and merge configuration from multiple sources with XDG compliance.

### Implementation

```typescript
const CONFIG_FILENAME = 'delta9.json';

function getUserConfigDir(): string {
  return process.env.XDG_CONFIG_HOME || path.join(os.homedir(), '.config');
}

function loadConfigFromPath(configPath: string): PluginConfig | null {
  try {
    const content = fs.readFileSync(configPath, 'utf-8');
    const rawConfig = JSON.parse(content);
    const result = PluginConfigSchema.safeParse(rawConfig);

    if (!result.success) {
      console.warn(`Invalid config at ${configPath}:`, result.error.format());
      return null;
    }
    return result.data;
  } catch (error) {
    // ENOENT is expected (file doesn't exist)
    if ((error as NodeJS.ErrnoException).code !== 'ENOENT') {
      console.warn(`Error reading config from ${configPath}:`, error);
    }
    return null;
  }
}

function deepMerge<T extends Record<string, unknown>>(base?: T, override?: T): T | undefined {
  if (!base) return override;
  if (!override) return base;

  const result = { ...base } as T;
  for (const key of Object.keys(override) as (keyof T)[]) {
    const baseVal = base[key];
    const overrideVal = override[key];

    if (typeof baseVal === 'object' && typeof overrideVal === 'object' &&
        !Array.isArray(baseVal) && !Array.isArray(overrideVal)) {
      result[key] = deepMerge(baseVal, overrideVal) as T[keyof T];
    } else {
      result[key] = overrideVal;
    }
  }
  return result;
}

export function loadPluginConfig(directory: string): PluginConfig {
  // 1. User config: ~/.config/opencode/delta9.json
  const userConfigPath = path.join(getUserConfigDir(), 'opencode', CONFIG_FILENAME);
  let config: PluginConfig = loadConfigFromPath(userConfigPath) ?? {};

  // 2. Project config: <directory>/.opencode/delta9.json
  const projectConfigPath = path.join(directory, '.opencode', CONFIG_FILENAME);
  const projectConfig = loadConfigFromPath(projectConfigPath);

  if (projectConfig) {
    config = {
      ...config,
      ...projectConfig,
      // Deep merge nested objects
      agents: deepMerge(config.agents, projectConfig.agents),
      council: deepMerge(config.council, projectConfig.council),
    };
  }

  // 3. Environment variable overrides
  if (process.env.DELTA9_PRESET) {
    config.preset = process.env.DELTA9_PRESET;
  }

  // 4. Resolve presets
  if (config.preset && config.presets?.[config.preset]) {
    config.agents = deepMerge(config.presets[config.preset], config.agents);
  }

  return config;
}
```

### Custom Prompt Loading

```typescript
export function loadAgentPrompt(agentName: string): { prompt?: string; appendPrompt?: string } {
  const promptsDir = path.join(getUserConfigDir(), 'opencode', 'delta9');
  const result: { prompt?: string; appendPrompt?: string } = {};

  // Check for replacement prompt: {agent}.md
  const promptPath = path.join(promptsDir, `${agentName}.md`);
  if (fs.existsSync(promptPath)) {
    result.prompt = fs.readFileSync(promptPath, 'utf-8');
  }

  // Check for append prompt: {agent}_append.md
  const appendPath = path.join(promptsDir, `${agentName}_append.md`);
  if (fs.existsSync(appendPath)) {
    result.appendPrompt = fs.readFileSync(appendPath, 'utf-8');
  }

  return result;
}
```

### Config Locations (Priority Order)

| Location | Priority | Purpose |
|----------|----------|---------|
| Environment vars | 1 (highest) | Runtime overrides |
| `<project>/.opencode/delta9.json` | 2 | Project-specific |
| `~/.config/opencode/delta9.json` | 3 | User defaults |
| Built-in defaults | 4 (lowest) | Fallback |

---

## Application to Delta9 (Extended)

### Additional Patterns to Adopt

1. **LSP Integration** - Provide code intelligence to Operators
2. **AST-Grep Tools** - Structural code transformations for Patcher
3. **Phase Reminder** - Keep Commander instructions in attention window
4. **Inspector Plugin** - Debug Council deliberations
5. **Skills + MCP** - Domain expertise for specialized Operators
6. **Streamlined Orchestrator** - Reference for Commander prompt design

### Pattern Selection Guide

| Pattern | Delta9 Agent | Use Case |
|---------|--------------|----------|
| LSP Tools | Operator, Validator | Go-to-definition, find references |
| AST-Grep | Patcher | Bulk code transformations |
| Phase Reminder | Commander | Combat instruction drift |
| Skills+MCP | Operators | Load specialized domain knowledge |
| Plugin Compose | Core | Combine internal sub-plugins |

---

## References

- [oh-my-opencode Source](https://github.com/code-yeongyu/oh-my-opencode)
- [oh-my-opencode-slim](https://github.com/code-yeongyu/oh-my-opencode-slim)
- [opencode-plugins](https://github.com/opencode-ai/opencode-plugins)
- [opencode-plugin-template](https://github.com/opencode-ai/opencode-plugin-template)
- [opencode-skillful](https://github.com/...)
- [awesome-opencode](https://github.com/awesome-opencode/awesome-opencode)
- [OpenCode Plugin Docs](https://opencode.ai/docs/plugins/)
