# Gemini CLI Package Analysis

> Comprehensive analysis of `packages/cli/src/` from the Gemini CLI codebase

---

## Overview

The Gemini CLI `cli` package is a sophisticated React-based terminal UI (TUI) application built with **Ink** (React for CLI). It provides both interactive and non-interactive modes for interacting with Google's Gemini AI models.

### Key Architectural Patterns

1. **React + Ink TUI**: Full React component architecture rendered in terminal
2. **Command System**: Extensible slash commands with multiple loaders
3. **Tool Scheduling**: Event-driven and legacy tool execution schedulers
4. **Session Management**: Resume, list, delete previous conversations
5. **Extension System**: Install/manage extensions from GitHub, local, etc.
6. **MCP Integration**: Model Context Protocol for tool discovery
7. **Multiple Auth Methods**: API Key, Vertex AI, Google Login, ADC

---

## File-by-File Breakdown

### Entry Points

#### `/src/gemini.tsx` (~840 lines)
Main entry point for the CLI application.

**Key Exports:**
- `main()` - Primary entry point
- `startInteractiveUI()` - Launches Ink-based TUI
- `validateDnsResolutionOrder()` - DNS resolution checks
- `getNodeMemoryArgs()` - Memory allocation helpers
- `initializeOutputListenersAndFlush()` - Output stream setup

**Key Functions:**
- Handles startup flow: sandbox detection, authentication, IDE integration
- Routes to interactive vs non-interactive mode based on flags
- Uses Ink `render()` with multiple context providers:
  - `SettingsContext`
  - `ConfigContext`
  - `SessionContext`
  - `UIStateContext`

**Patterns:**
```typescript
// Context provider wrapping pattern
render(
  <SettingsContext.Provider value={settings}>
    <ConfigContext.Provider value={config}>
      <SessionContext.Provider value={session}>
        <AppContainer />
      </SessionContext.Provider>
    </ConfigContext.Provider>
  </SettingsContext.Provider>
);
```

---

#### `/src/nonInteractiveCli.ts` (~530 lines)
Handles headless/piped execution with JSON output.

**Key Exports:**
- `runNonInteractive()` - Main non-interactive handler

**Key Features:**
- JSON and stream-JSON output formats
- Ctrl+C cancellation handling
- Tool execution loop without TUI
- Stdin piping support

**Patterns:**
```typescript
// Non-interactive output format handling
if (config.getOutputFormat() === OutputFormat.JSON) {
  const formatter = new JsonFormatter();
  // Format tool responses as JSON
}
```

---

#### `/src/nonInteractiveCliCommands.ts` (~180 lines)
CLI subcommands for non-interactive use.

**Key Commands:**
- `gemini mcp` - MCP server management
- `gemini extensions` - Extension management
- `gemini hooks` - Hooks migration
- `gemini skills` - Skills management

---

### Configuration (`/src/config/`)

#### `/src/config/config.ts` (~821 lines)
CLI configuration and argument parsing using **yargs**.

**Key Exports:**
- `parseArguments()` - Parse CLI args
- `loadCliConfig()` - Load full config
- `CliArgs` interface

**CLI Arguments (key ones):**
- `--prompt, -p` - Single prompt (non-interactive)
- `--yolo` - Auto-approve all tool calls
- `--auto-edit` - Auto-approve edit tools only
- `--resume` - Resume previous session
- `--sandbox` - Enable sandboxed execution
- `--json` / `--stream-json` - Output formats
- `--model, -m` - Model selection

**ApprovalMode Enum:**
```typescript
enum ApprovalMode {
  DEFAULT = 'DEFAULT',
  AUTO_EDIT = 'AUTO_EDIT',
  YOLO = 'YOLO',
  PLAN = 'PLAN',
}
```

---

#### `/src/config/settings.ts` (~969 lines)
Multi-scope settings management.

**Key Exports:**
- `loadSettings()` - Load and merge settings
- `LoadedSettings` class
- `mergeSettings()` - Merge settings from multiple scopes

**Settings Scopes (precedence order):**
1. Schema Defaults
2. System Defaults
3. User Settings (`~/.gemini/settings.json`)
4. Workspace Settings (`.gemini/settings.json`)
5. System Overrides

**Settings Structure:**
```typescript
interface Settings {
  theme?: ThemeConfig;
  security?: SecuritySettings;
  mcp?: MCPSettings;
  mcpServers?: Record<string, MCPServerConfig>;
  tools?: ToolSettings;
  ui?: UISettings;
  // ... more
}
```

---

#### `/src/config/keyBindings.ts` (~479 lines)
Keybinding configuration system.

**Key Exports:**
- `Command` enum - 50+ commands
- `defaultKeyBindings` - Default key mappings
- `commandCategories` - Category groupings
- `commandDescriptions` - Human-readable descriptions

**Command Enum (sample):**
```typescript
enum Command {
  SUBMIT = 'submit',
  CANCEL = 'cancel',
  ACCEPT = 'accept',
  REJECT = 'reject',
  HISTORY_PREV = 'historyPrev',
  HISTORY_NEXT = 'historyNext',
  COMPLETE_FORWARD = 'completeForward',
  COMPLETE_BACKWARD = 'completeBackward',
  QUIT = 'quit',
  // ... 40+ more
}
```

**KeyBinding Interface:**
```typescript
interface KeyBinding {
  key: string;
  shift?: boolean;
  alt?: boolean;
  ctrl?: boolean;
  cmd?: boolean;
}
```

---

#### `/src/config/auth.ts` (~47 lines)
Authentication validation.

**Key Exports:**
- `validateAuthMethod()` - Validate auth configuration

**Auth Types:**
- `LOGIN_WITH_GOOGLE` - OAuth
- `USE_GEMINI` - API Key (requires `GEMINI_API_KEY`)
- `USE_VERTEX_AI` - Vertex AI (requires `GOOGLE_CLOUD_PROJECT` + `GOOGLE_CLOUD_LOCATION` or `GOOGLE_API_KEY`)
- `COMPUTE_ADC` - Application Default Credentials

---

#### `/src/config/sandboxConfig.ts` (~109 lines)
Sandbox execution configuration.

**Key Exports:**
- `loadSandboxConfig()` - Load sandbox settings

**Sandbox Commands:**
- `docker`
- `podman`
- `sandbox-exec` (macOS seatbelt)

**Pattern:**
```typescript
// Auto-detect sandbox command
if (os.platform() === 'darwin' && commandExists.sync('sandbox-exec')) {
  return 'sandbox-exec';
} else if (commandExists.sync('docker') && sandbox === true) {
  return 'docker';
}
```

---

#### `/src/config/trustedFolders.ts` (~267 lines)
Folder trust management for security.

**Key Exports:**
- `LoadedTrustedFolders` class
- `loadTrustedFolders()` - Load trust config
- `isFolderTrustEnabled()` - Check if enabled
- `isWorkspaceTrusted()` - Check workspace trust

**Trust Levels:**
```typescript
enum TrustLevel {
  TRUST_FOLDER = 'TRUST_FOLDER',
  TRUST_PARENT = 'TRUST_PARENT',
  DO_NOT_TRUST = 'DO_NOT_TRUST',
}
```

---

#### `/src/config/policy.ts` (~39 lines)
Policy engine configuration wrapper.

**Key Exports:**
- `createPolicyEngineConfig()` - Create policy config
- `createPolicyUpdater()` - Create policy updater

---

#### `/src/config/extension-manager.ts` (~400+ lines)
Extension system management.

**Key Exports:**
- `ExtensionManager` class (extends `ExtensionLoader`)

**Extension Install Types:**
- `local` - Local directory
- `link` - Symlinked directory
- `git` - Git repository
- `github-release` - GitHub releases

**Extension Capabilities:**
- MCP servers
- Skills
- Themes
- Hooks
- Custom commands (TOML)

---

### Services (`/src/services/`)

#### `/src/services/CommandService.ts` (~105 lines)
Command orchestration service.

**Key Exports:**
- `CommandService.create()` - Factory method
- `getCommands()` - Get all loaded commands

**Pattern:**
```typescript
// Parallel command loading from multiple sources
static async create(
  loaders: ICommandLoader[],
  signal: AbortSignal,
): Promise<CommandService> {
  const loadedCommands = await Promise.all(
    loaders.map((loader) => loader.loadCommands(signal)),
  );
  // Merge and deduplicate
}
```

---

#### `/src/services/BuiltinCommandLoader.ts` (~175 lines)
Loads 30+ built-in slash commands.

**Built-in Commands:**
- `/about` - Version info
- `/agents` - Agent management
- `/auth` - Authentication
- `/bug` - Report bugs
- `/chat` - Chat history
- `/clear` - Clear screen
- `/compress` - Compress context
- `/copy` - Copy to clipboard
- `/editor` - Editor settings
- `/extensions` - Extension management
- `/help` - Help
- `/history` - Input history
- `/logout` - Logout
- `/memory` - Memory commands
- `/model` - Model selection
- `/permissions` - Permission management
- `/privacy` - Privacy settings
- `/quit` - Exit
- `/restore` - Restore checkpoint
- `/rewind` - Rewind conversation
- `/save` - Save conversation
- `/session` - Session management
- `/settings` - Settings
- `/skills` - Skills management
- `/stats` - Statistics
- `/theme` - Theme settings
- `/tools` - Tool management
- `/vim` - Vim mode toggle

---

#### `/src/services/FileCommandLoader.ts` (~340 lines)
Custom command loader from TOML files.

**Key Exports:**
- `FileCommandLoader` class
- `TomlCommandDefSchema` - Zod schema for TOML validation

**TOML Command Features:**
- Argument substitution: `{{args}}`
- Shell injection: `$(command)`
- @-file injection: `@filename`
- Multi-command sequences

**Discovery Paths:**
- User: `~/.gemini/commands/`
- Project: `.gemini/commands/`
- Extensions: `extensions/*/commands/`

---

#### `/src/services/McpPromptLoader.ts` (~304 lines)
Loads slash commands from MCP server prompts.

**Key Exports:**
- `McpPromptLoader` class

**Features:**
- Converts MCP prompts to slash commands
- Argument parsing (named and positional)
- Auto-execute for prompts without arguments
- Help subcommand generation

---

### UI Components (`/src/ui/`)

#### `/src/ui/AppContainer.tsx` (~1000+ lines)
Main UI container orchestrating all state.

**Key Responsibilities:**
- History management
- Authentication state
- Theme management
- Settings management
- Session browser
- Dialog management
- Tool scheduling integration

**Hooks Used (~30+):**
- `useGeminiStream`
- `useHistoryManager`
- `useSlashCommandProcessor`
- `useKeypress`
- `useFolderTrust`
- `useSessionBrowser`
- `useThemeCommand`
- `useModelCommand`
- etc.

---

#### `/src/ui/types.ts` (~460 lines)
UI type definitions.

**HistoryItem Types:**
```typescript
type HistoryItem =
  | HistoryItemUser
  | HistoryItemGemini
  | HistoryItemInfo
  | HistoryItemError
  | HistoryItemWarning
  | HistoryItemToolGroup
  | HistoryItemHelp
  | HistoryItemStats
  | HistoryItemAbout
  | HistoryItemModel
  | HistoryItemCompression
  | HistoryItemQuit;
```

**Enums:**
```typescript
enum StreamingState {
  Idle = 'idle',
  Responding = 'responding',
  WaitingForConfirmation = 'waitingForConfirmation',
}

enum ToolCallStatus {
  Pending = 'Pending',
  Confirming = 'Confirming',
  Executing = 'Executing',
  Completed = 'Completed',
  Failed = 'Failed',
  Canceled = 'Canceled',
}

enum MessageType {
  USER = 'user',
  GEMINI = 'gemini',
  INFO = 'info',
  ERROR = 'error',
  WARNING = 'warning',
  // ... more
}
```

---

#### `/src/ui/commands/types.ts` (~221 lines)
Command type definitions.

**Key Interfaces:**
```typescript
interface CommandContext {
  services: {
    config: Config | null;
    settings: LoadedSettings;
    git: GitService | undefined;
    logger: Logger;
  };
  ui: {
    addItem: (item: HistoryItemWithoutId, timestamp?: number) => string;
    clear: () => void;
    loadHistory: (history: HistoryItem[], postLoadInput?: string) => void;
    // ... more
  };
  session: {
    stats: SessionStats;
    sessionShellAllowlist: Set<string>;
  };
  invocation?: {
    raw: string;
    name: string;
    args: string;
  };
}

interface SlashCommand {
  name: string;
  description?: string;
  kind: CommandKind;
  action?: (context: CommandContext, args: string) => Promise<SlashCommandActionReturn>;
  completion?: (context: CommandContext, partial: string) => Promise<string[]>;
  subCommands?: SlashCommand[];
  autoExecute?: boolean;
  extensionId?: string;
}

enum CommandKind {
  BUILT_IN = 'built_in',
  FILE = 'file',
  MCP_PROMPT = 'mcp_prompt',
  AGENT = 'agent',
}
```

---

### Hooks (`/src/ui/hooks/`)

#### `/src/ui/hooks/useGeminiStream.ts` (~1605 lines)
Core hook managing Gemini API streaming.

**Key Exports:**
- `useGeminiStream()` hook

**Responsibilities:**
- User input processing
- Command routing (slash, @, shell)
- Gemini API streaming
- Tool call lifecycle management
- Error handling
- Loop detection
- Cancellation handling

**Key State:**
- `isResponding` - Currently processing
- `thought` - Thinking indicator
- `toolCalls` - Active tool calls
- `pendingHistoryItem` - Streaming content

**Event Handling:**
```typescript
// Stream event types handled
switch (event.type) {
  case ServerGeminiEventType.Thought:
  case ServerGeminiEventType.Content:
  case ServerGeminiEventType.ToolCallRequest:
  case ServerGeminiEventType.UserCancelled:
  case ServerGeminiEventType.Error:
  case ServerGeminiEventType.ChatCompressed:
  case ServerGeminiEventType.MaxSessionTurns:
  case ServerGeminiEventType.ContextWindowWillOverflow:
  case ServerGeminiEventType.Finished:
  case ServerGeminiEventType.Citation:
  case ServerGeminiEventType.ModelInfo:
  case ServerGeminiEventType.LoopDetected:
  // ... more
}
```

---

#### `/src/ui/hooks/useToolScheduler.ts` (~89 lines)
Facade for tool scheduling implementations.

**Key Exports:**
- `useToolScheduler()` - Switches between legacy and event-driven schedulers

**Tool Call States:**
```typescript
type TrackedToolCall =
  | TrackedScheduledToolCall
  | TrackedValidatingToolCall
  | TrackedWaitingToolCall
  | TrackedExecutingToolCall
  | TrackedCompletedToolCall
  | TrackedCancelledToolCall;
```

---

#### `/src/ui/hooks/slashCommandProcessor.ts` (~710 lines)
Slash command processing logic.

**Key Exports:**
- `useSlashCommandProcessor()` hook

**Features:**
- Command discovery from multiple loaders
- Context building for command execution
- Shell command allowlist management
- Confirmation dialogs
- Result type handling

**Result Types:**
```typescript
type SlashCommandActionReturn =
  | { type: 'tool'; toolName: string; toolArgs: Record<string, unknown> }
  | { type: 'message'; messageType: 'info' | 'error'; content: string }
  | { type: 'dialog'; dialog: DialogType; props?: unknown }
  | { type: 'quit'; messages: HistoryItem[] }
  | { type: 'submit_prompt'; content: string }
  | { type: 'confirm_shell_commands'; commandsToConfirm: string[]; originalInvocation: CommandInvocation }
  | { type: 'confirm_action'; prompt: React.ReactNode; originalInvocation: CommandInvocation }
  | { type: 'custom_dialog'; component: React.ReactNode }
  | { type: 'load_history'; history: HistoryItem[]; clientHistory: Content[] }
  | { type: 'logout' };
```

---

### Utilities (`/src/utils/`)

#### `/src/utils/cleanup.ts` (~113 lines)
Cleanup and exit handling.

**Key Exports:**
- `registerCleanup()` - Register async cleanup
- `registerSyncCleanup()` - Register sync cleanup
- `runExitCleanup()` - Run all cleanup handlers
- `cleanupCheckpoints()` - Clean checkpoint files

**Pattern:**
```typescript
// Cleanup registration
const cleanupFunctions: Array<(() => void) | (() => Promise<void>)> = [];

export function registerCleanup(fn: (() => void) | (() => Promise<void>)) {
  cleanupFunctions.push(fn);
}

export async function runExitCleanup() {
  await drainStdin();
  runSyncCleanup();
  for (const fn of cleanupFunctions) {
    await fn();
  }
  // Shutdown telemetry last
}
```

---

#### `/src/utils/errors.ts` (~250 lines)
Error handling utilities.

**Key Exports:**
- `handleError()` - Format-aware error handling
- `handleToolError()` - Tool-specific error handling
- `handleCancellationError()` - Cancellation handling
- `handleMaxTurnsExceededError()` - Turn limit handling

**Error Types:**
- `FatalToolExecutionError`
- `FatalCancellationError`
- `FatalTurnLimitedError`

---

#### `/src/utils/sessionUtils.ts` (~517 lines)
Session management utilities.

**Key Exports:**
- `SessionSelector` class
- `getSessionFiles()` - List sessions
- `SessionInfo` interface
- `RESUME_LATEST` constant

**Features:**
- Session resume by UUID or index
- Session listing with metadata
- Corruption handling
- Deduplication

---

#### `/src/utils/commands.ts`
Command parsing utilities.

**Key Exports:**
- `parseSlashCommand()` - Parse slash command with subcommands

---

### IDE Integration

#### `/src/zed-integration/zedIntegration.ts` (~300+ lines)
Zed IDE integration via ACP (Agent Client Protocol).

**Key Exports:**
- `runZedIntegration()` - Entry point for Zed
- `GeminiAgent` class
- `Session` class

**ACP Implementation:**
```typescript
// Agent interface for IDE
class GeminiAgent {
  async run(session: Session): Promise<void> {
    // Handle tool calls
    // Stream responses
  }
}
```

---

#### `/src/core/initializer.ts` (~68 lines)
App initialization orchestration.

**Key Exports:**
- `initializeApp()` - Initialize auth, theme, IDE connection
- `InitializationResult` interface

---

### Commands (`/src/commands/`)

#### `/src/commands/extensions.tsx`
Extension management command.

**Subcommands:**
- `install` - Install extension
- `uninstall` - Remove extension
- `list` - List extensions
- `update` - Update extensions
- `enable` - Enable extension
- `disable` - Disable extension
- `link` - Link local extension
- `new` - Create new extension
- `validate` - Validate extension
- `configure` - Configure extension

---

#### `/src/commands/mcp.ts`
MCP server management command.

**Subcommands:**
- `add` - Add MCP server
- `remove` - Remove MCP server
- `list` - List MCP servers
- `enable` - Enable MCP server
- `disable` - Disable MCP server

---

#### `/src/commands/skills.tsx`
Skills management command.

**Subcommands:**
- `list` - List skills
- `enable` - Enable skill
- `disable` - Disable skill
- `install` - Install skill
- `uninstall` - Remove skill

---

#### `/src/commands/hooks.tsx`
Hooks management command.

**Subcommands:**
- `migrate` - Migrate hooks to new format

---

### Auth (`/src/validateNonInterActiveAuth.ts`)
Non-interactive authentication validation.

**Key Exports:**
- `validateNonInteractiveAuth()` - Validate auth in headless mode

**Features:**
- Environment variable detection (`GEMINI_API_KEY`, `GOOGLE_GENAI_USE_VERTEXAI`, etc.)
- Enforced auth type validation
- Error formatting for JSON output

---

## CLI Patterns and Commands

### Command System Architecture

```
┌─────────────────────────────────────────────────────────────┐
│                    CommandService                           │
├─────────────────────────────────────────────────────────────┤
│  Aggregates commands from multiple loaders:                 │
│  ├── BuiltinCommandLoader (30+ commands)                   │
│  ├── FileCommandLoader (TOML custom commands)              │
│  └── McpPromptLoader (MCP server prompts)                  │
└─────────────────────────────────────────────────────────────┘
```

### Slash Command Flow

```
User Input: "/help topic"
     │
     ▼
┌─────────────────┐
│ isSlashCommand? │
└────────┬────────┘
         │ Yes
         ▼
┌─────────────────────┐
│ parseSlashCommand() │
│ - Find command      │
│ - Extract args      │
│ - Resolve subcommands│
└────────┬────────────┘
         │
         ▼
┌─────────────────────┐
│ command.action()    │
│ with CommandContext │
└────────┬────────────┘
         │
         ▼
┌─────────────────────────────┐
│ Handle ActionReturn:        │
│ - message → addItem()       │
│ - dialog → openDialog()     │
│ - submit_prompt → sendQuery │
│ - tool → scheduleTool()     │
│ - quit → exit()             │
└─────────────────────────────┘
```

### Tool Execution Flow

```
┌─────────────────────┐
│ Gemini Response     │
│ with ToolCallRequest│
└────────┬────────────┘
         │
         ▼
┌─────────────────────┐
│ scheduleToolCalls() │
└────────┬────────────┘
         │
         ▼
┌─────────────────────────────────┐
│ Tool States:                    │
│ scheduled → validating →        │
│ awaiting_approval → executing → │
│ success/error/cancelled         │
└────────┬────────────────────────┘
         │
         ▼
┌─────────────────────────────────┐
│ handleCompletedTools()          │
│ - Record interactions           │
│ - Submit responses to Gemini    │
│ - Continue conversation loop    │
└─────────────────────────────────┘
```

### Approval Modes

| Mode | Behavior |
|------|----------|
| `DEFAULT` | Prompt for each tool call |
| `AUTO_EDIT` | Auto-approve edit tools (replace, write_file) |
| `YOLO` | Auto-approve all tool calls |
| `PLAN` | Planning mode (special handling) |

### Extension System

```
Extension Structure:
├── manifest.json (or extension.json)
├── commands/
│   └── *.toml (custom commands)
├── mcp/
│   └── servers.json (MCP server configs)
├── skills/
│   └── *.md (skill definitions)
├── themes/
│   └── *.json (theme definitions)
└── hooks/
    └── hooks.json (lifecycle hooks)
```

---

## Key Takeaways for Estela

### Features to Consider

1. **Extension System**
   - Gemini CLI has a sophisticated extension system supporting GitHub, local, and linked extensions
   - Extensions can provide: commands, MCP servers, skills, themes, hooks
   - Estela could benefit from a similar plugin architecture

2. **Session Resume**
   - Full session resume with `--resume latest` or `--resume <id>`
   - Session browser UI for selection
   - Session metadata: summary, message count, timestamps

3. **Approval Modes**
   - `--yolo` for full automation
   - `--auto-edit` for edit-only automation
   - `--plan` for planning mode
   - Estela should consider similar granular approval controls

4. **Sandbox Execution**
   - Built-in support for Docker, Podman, sandbox-exec
   - Automatic detection on macOS (sandbox-exec)
   - Environment variable configuration

5. **TOML Custom Commands**
   - Simple TOML format for user-defined commands
   - Shell expansion with `$(command)`
   - Argument substitution with `{{args}}`

6. **MCP Integration**
   - MCP prompts become slash commands
   - MCP server enable/disable per-session
   - Discovery state tracking

7. **Trusted Folders**
   - Security feature for folder trust
   - IDE integration for workspace trust
   - Three trust levels: TRUST_FOLDER, TRUST_PARENT, DO_NOT_TRUST

8. **Multi-Scope Settings**
   - System defaults → User → Workspace → System overrides
   - Clear precedence chain
   - Settings validation

9. **Tool Scheduling Architecture**
   - Two implementations: legacy React-based and event-driven
   - Feature flag switching between them
   - Parallel and sequential execution support

10. **IDE Integration (ACP)**
    - Zed integration via Agent Client Protocol
    - Clean separation of agent and session
    - Streaming response support

### Architectural Patterns Worth Adopting

1. **Context Providers**
   - Settings, Config, Session, UIState as React contexts
   - Clean separation of concerns

2. **Command Loader Pattern**
   - Interface-based loaders for different command sources
   - Parallel loading with deduplication

3. **Event-Driven Tool Scheduling**
   - State machine for tool lifecycle
   - Clean handling of cancellation

4. **Format-Aware Error Handling**
   - Different error formatting for JSON, stream-JSON, text
   - Graceful degradation

5. **Cleanup Registration**
   - Centralized cleanup with sync and async handlers
   - Telemetry shutdown as final step

### Missing in Estela (Potential Gaps)

1. **Session Browser UI** - Interactive session selection
2. **Extension Marketplace** - GitHub-based extension discovery
3. **Sandbox Mode** - Docker/Podman isolation
4. **TOML Custom Commands** - User-defined shortcuts
5. **Trusted Folders** - Security boundary management
6. **Loop Detection** - Automatic detection of repetitive behavior
7. **Context Compression** - Token limit management with `/compress`
8. **Checkpoint/Restore** - Git-based state restoration
9. **IDE Protocol (ACP)** - Native IDE agent integration
10. **Multi-Model Support** - Model switching with `/model`
