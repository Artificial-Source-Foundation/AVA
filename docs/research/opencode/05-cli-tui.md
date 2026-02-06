# OpenCode CLI & TUI Analysis

This document provides a comprehensive analysis of OpenCode's CLI architecture, Terminal UI (TUI) components, command system, PTY handling, and output formatting.

---

## CLI Architecture

### Entry Point and Command Framework

OpenCode uses **yargs** for CLI command parsing. The core pattern is defined in `/packages/opencode/src/cli/cmd/cmd.ts`:

```typescript
import type { CommandModule } from "yargs"

type WithDoubleDash<T> = T & { "--"?: string[] }

export function cmd<T, U>(input: CommandModule<T, WithDoubleDash<U>>) {
  return input
}
```

This wrapper enables support for `--` passthrough arguments while maintaining type safety.

### Bootstrap System

The bootstrap function (`/cli/bootstrap.ts`) provides a lifecycle wrapper that initializes the Instance context:

```typescript
export async function bootstrap<T>(directory: string, cb: () => Promise<T>) {
  return Instance.provide({
    directory,
    init: InstanceBootstrap,
    fn: async () => {
      try {
        const result = await cb()
        return result
      } finally {
        await Instance.dispose()
      }
    },
  })
}
```

This ensures proper initialization and cleanup of project state for every command execution.

### CLI Commands

#### Primary Commands

| Command | File | Purpose |
|---------|------|---------|
| `$0` (default) | `tui/thread.ts` | Launches the TUI (main entry point) |
| `run` | `run.ts` | Non-interactive execution with message |
| `serve` | `serve.ts` | Headless HTTP server |
| `web` | `web.ts` | Server + browser web interface |
| `attach` | `tui/attach.ts` | Connect to running server |
| `acp` | `acp.ts` | Agent Client Protocol server |

#### Session Management

| Command | File | Purpose |
|---------|------|---------|
| `session list` | `session.ts` | List sessions with pagination |
| `import` | `import.ts` | Import session from JSON/URL |
| `export` | `export.ts` | Export session to JSON |

#### Model & Agent Management

| Command | File | Purpose |
|---------|------|---------|
| `models` | `models.ts` | List available models |
| `agent create` | `agent.ts` | Interactive agent creation |
| `agent list` | `agent.ts` | List available agents |

#### Authentication

| Command | File | Purpose |
|---------|------|---------|
| `auth login` | `auth.ts` | Add provider credentials |
| `auth logout` | `auth.ts` | Remove credentials |
| `auth list` | `auth.ts` | List stored credentials |

#### MCP Management

| Command | File | Purpose |
|---------|------|---------|
| `mcp list` | `mcp.ts` | List MCP servers and status |
| `mcp add` | `mcp.ts` | Add local or remote MCP server |
| `mcp auth` | `mcp.ts` | OAuth authentication for MCP |
| `mcp logout` | `mcp.ts` | Remove OAuth credentials |
| `mcp debug` | `mcp.ts` | Debug OAuth connection |

#### GitHub Integration

| Command | File | Purpose |
|---------|------|---------|
| `github install` | `github.ts` | Install GitHub Action workflow |
| `github run` | `github.ts` | Run GitHub agent (Action context) |
| `pr` | `pr.ts` | Checkout PR and launch TUI |

#### Utility Commands

| Command | File | Purpose |
|---------|------|---------|
| `upgrade` | `upgrade.ts` | Upgrade to latest version |
| `uninstall` | `uninstall.ts` | Remove OpenCode and files |
| `stats` | `stats.ts` | Token usage and cost statistics |
| `generate` | `generate.ts` | Generate OpenAPI spec |

#### Debug Commands (`debug/`)

| Subcommand | File | Purpose |
|------------|------|---------|
| `config` | `config.ts` | Show resolved configuration |
| `agent` | `agent.ts` | Test agent with tools |
| `file` | `file.ts` | File operations debug |
| `lsp` | `lsp.ts` | Language server debug |
| `ripgrep` | `ripgrep.ts` | Grep operations debug |
| `scrap` | `scrap.ts` | Scratch testing |
| `skill` | `skill.ts` | Skill loading debug |
| `snapshot` | `snapshot.ts` | Git snapshot debug |
| `paths` | `index.ts` | Show global paths |
| `wait` | `index.ts` | Wait indefinitely |

### Network Configuration

The network module (`/cli/network.ts`) provides unified options for server commands:

```typescript
const options = {
  port: { type: "number", default: 0 },
  hostname: { type: "string", default: "127.0.0.1" },
  mdns: { type: "boolean", default: false },
  cors: { type: "string", array: true, default: [] }
}
```

Options can be set via CLI flags or global config (`server.port`, `server.hostname`, etc.).

### UI Utilities

The UI namespace (`/cli/ui.ts`) provides console output helpers:

```typescript
export namespace UI {
  export const Style = {
    TEXT_HIGHLIGHT: "\x1b[96m",       // Cyan
    TEXT_HIGHLIGHT_BOLD: "\x1b[96m\x1b[1m",
    TEXT_DIM: "\x1b[90m",             // Gray
    TEXT_WARNING: "\x1b[93m",         // Yellow
    TEXT_DANGER: "\x1b[91m",          // Red
    TEXT_SUCCESS: "\x1b[92m",         // Green
    TEXT_INFO: "\x1b[94m",            // Blue
    // ... bold variants
  }

  export function println(...message: string[])
  export function print(...message: string[])
  export function empty()
  export function logo(pad?: string)
  export function input(prompt: string): Promise<string>
  export function error(message: string)
}
```

The logo is rendered with ASCII art using custom glyphs and terminal colors.

### Error Formatting

The error module (`/cli/error.ts`) provides user-friendly error messages:

- `MCP.Failed` - MCP server connection failures
- `Provider.ModelNotFoundError` - Invalid model with suggestions
- `Provider.InitError` - Provider initialization failures
- `Config.JsonError` - Configuration parse errors
- `Config.InvalidError` - Configuration validation errors

---

## TUI Components

### Architecture Overview

The TUI is built with **@opentui/solid** (a SolidJS-based terminal UI framework) and follows a provider-based architecture.

### Entry Point (`tui/app.tsx`)

The `tui()` function initializes the entire TUI:

```typescript
export function tui(input: {
  url: string
  args: Args
  directory?: string
  fetch?: typeof fetch
  events?: EventSource
  onExit?: () => Promise<void>
})
```

### Provider Hierarchy

The TUI uses a deeply nested provider structure (from outermost to innermost):

1. **ArgsProvider** - CLI arguments
2. **ExitProvider** - Exit handling
3. **KVProvider** - Key-value persistent storage
4. **ToastProvider** - Toast notifications
5. **RouteProvider** - Navigation routing
6. **SDKProvider** - Backend API client
7. **SyncProvider** - Real-time data synchronization
8. **ThemeProvider** - Theme and appearance
9. **LocalProvider** - Local preferences (agent, model)
10. **KeybindProvider** - Keybinding configuration
11. **PromptStashProvider** - Prompt draft storage
12. **DialogProvider** - Modal dialogs
13. **CommandProvider** - Command palette
14. **FrecencyProvider** - Frecency-based suggestions
15. **PromptHistoryProvider** - Input history
16. **PromptRefProvider** - Prompt input ref

### Routes

Two main routes:

| Route | Component | Description |
|-------|-----------|-------------|
| `home` | `Home` | New session start screen |
| `session` | `Session` | Active chat session view |

### Context Providers

#### SDK Context (`context/sdk.ts`)

Manages the OpenCode SDK client for API communication:
- Session operations
- Message sending
- Event subscription
- Configuration retrieval

#### Sync Context (`context/sync.ts`)

Real-time synchronization of:
- Sessions list
- Messages and parts
- Provider/model availability
- MCP server status
- VCS (git) information

#### Local Context (`context/local.ts`)

Local UI state management:
- Current agent selection
- Current model selection
- Model variant (reasoning effort)
- Recent models list

#### Theme Context (`context/theme.ts`)

Theme management:
- Dark/light mode detection (via OSC 11 query)
- Theme selection (catppuccin variants, github, etc.)
- Color palette access

#### Keybind Context (`context/keybind.ts`)

Keybinding system:
- Configurable keybinds
- Leader key support
- Textarea action mappings

### Dialogs

| Dialog | Purpose |
|--------|---------|
| `DialogModel` | Model selection |
| `DialogAgent` | Agent selection |
| `DialogMcp` | MCP server management |
| `DialogStatus` | System status |
| `DialogThemeList` | Theme selection |
| `DialogHelp` | Help/keybindings |
| `DialogSessionList` | Session switcher |
| `DialogAlert` | Alert messages |
| `DialogProviderList` | Provider connection |

### Command Palette

The command palette registers actions via `command.register()`:

```typescript
command.register(() => [
  {
    title: "Switch session",
    value: "session.list",
    keybind: "session_list",
    category: "Session",
    slash: { name: "sessions", aliases: ["resume", "continue"] },
    onSelect: () => dialog.replace(() => <DialogSessionList />),
  },
  // ... more commands
])
```

Categories: Session, Agent, Provider, System

### TUI Worker Architecture

The TUI runs in a multi-threaded architecture:

1. **Main Thread** - Renders UI with @opentui/solid
2. **Worker Thread** (`tui/worker.ts`) - Runs backend server

Communication via RPC (`/util/rpc.ts`):

```typescript
export namespace Rpc {
  // Worker side
  export function listen(rpc: Definition)
  export function emit(event: string, data: unknown)

  // Client side
  export function client<T extends Definition>(target)
    // Returns { call, on }
}
```

Worker RPC methods:
- `fetch` - Proxy HTTP requests
- `server` - Start HTTP server
- `checkUpgrade` - Check for updates
- `reload` - Reload configuration
- `shutdown` - Clean shutdown

---

## Command System (Slash Commands)

### Command Definition (`/command/index.ts`)

Commands are defined with templates that support argument substitution:

```typescript
export namespace Command {
  export const Info = z.object({
    name: z.string(),
    description: z.string().optional(),
    agent: z.string().optional(),
    model: z.string().optional(),
    mcp: z.boolean().optional(),
    template: z.promise(z.string()).or(z.string()),
    subtask: z.boolean().optional(),
    hints: z.array(z.string()),  // $1, $2, $ARGUMENTS
  })
}
```

### Built-in Commands

| Name | Description | Template File |
|------|-------------|---------------|
| `init` | Create/update AGENTS.md | `template/initialize.txt` |
| `review` | Review changes | `template/review.txt` |

### Template Arguments

Templates support argument substitution:
- `$1`, `$2`, etc. - Numbered arguments
- `$ARGUMENTS` - All arguments as string

```typescript
export function hints(template: string): string[] {
  const result: string[] = []
  const numbered = template.match(/\$\d+/g)
  if (numbered) {
    for (const match of [...new Set(numbered)].sort())
      result.push(match)
  }
  if (template.includes("$ARGUMENTS"))
    result.push("$ARGUMENTS")
  return result
}
```

### Custom Commands

Defined in `opencode.json`:

```json
{
  "command": {
    "test": {
      "description": "Run tests",
      "template": "Run the test suite for $1",
      "agent": "test-runner",
      "subtask": true
    }
  }
}
```

### MCP Prompts as Commands

MCP servers can expose prompts that become available as commands:

```typescript
for (const [name, prompt] of Object.entries(await MCP.prompts())) {
  result[name] = {
    name,
    mcp: true,
    description: prompt.description,
    get template() {
      return MCP.getPrompt(prompt.client, prompt.name, ...)
    },
    hints: prompt.arguments?.map((_, i) => `$${i + 1}`) ?? [],
  }
}
```

### TUI Events

The TUI event system (`tui/event.ts`) enables communication:

```typescript
export const TuiEvent = {
  PromptAppend: BusEvent.define("tui.prompt.append",
    z.object({ text: z.string() })),

  CommandExecute: BusEvent.define("tui.command.execute",
    z.object({
      command: z.union([
        z.enum(["session.list", "session.new", "session.share", ...]),
        z.string(),
      ]),
    })),

  ToastShow: BusEvent.define("tui.toast.show",
    z.object({
      title: z.string().optional(),
      message: z.string(),
      variant: z.enum(["info", "success", "warning", "error"]),
      duration: z.number().default(5000),
    })),

  SessionSelect: BusEvent.define("tui.session.select",
    z.object({ sessionID: z.string().regex(/^ses/) })),
}
```

---

## PTY & Shell

### PTY Module (`/pty/index.ts`)

The PTY (pseudo-terminal) module manages interactive terminal sessions using `bun-pty`.

#### Session Management

```typescript
export namespace Pty {
  export const Info = z.object({
    id: Identifier.schema("pty"),
    title: z.string(),
    command: z.string(),
    args: z.array(z.string()),
    cwd: z.string(),
    status: z.enum(["running", "exited"]),
    pid: z.number(),
  })

  export async function create(input: CreateInput)
  export async function update(id: string, input: UpdateInput)
  export async function remove(id: string)
  export function resize(id: string, cols: number, rows: number)
  export function write(id: string, data: string)
  export function connect(id: string, ws: WSContext)
  export function list()
  export function get(id: string)
}
```

#### Session Creation

```typescript
export async function create(input: CreateInput) {
  const command = input.command || Shell.preferred()
  const args = input.args || []
  if (command.endsWith("sh")) {
    args.push("-l")  // Login shell
  }

  const env = {
    ...process.env,
    ...input.env,
    TERM: "xterm-256color",
    OPENCODE_TERMINAL: "1",
  }

  const ptyProcess = spawn(command, args, {
    name: "xterm-256color",
    cwd,
    env,
  })
  // ...
}
```

#### Buffer Management

PTY sessions buffer output when no subscribers are connected:

```typescript
const BUFFER_LIMIT = 1024 * 1024 * 2  // 2MB
const BUFFER_CHUNK = 64 * 1024        // 64KB

ptyProcess.onData((data) => {
  // Send to subscribers if connected
  for (const ws of session.subscribers) {
    ws.send(data)
  }
  // Buffer if no subscribers
  session.buffer += data
  if (session.buffer.length > BUFFER_LIMIT) {
    session.buffer = session.buffer.slice(-BUFFER_LIMIT)
  }
})
```

#### WebSocket Connection

```typescript
export function connect(id: string, ws: WSContext) {
  const session = state().get(id)
  session.subscribers.add(ws)

  // Send buffered content in chunks
  if (session.buffer) {
    for (let i = 0; i < buffer.length; i += BUFFER_CHUNK) {
      ws.send(buffer.slice(i, i + BUFFER_CHUNK))
    }
  }

  return {
    onMessage: (message) => session.process.write(String(message)),
    onClose: () => session.subscribers.delete(ws),
  }
}
```

### Shell Module (`/shell/shell.ts`)

Handles shell detection and process management:

```typescript
export namespace Shell {
  // Kill process tree (cross-platform)
  export async function killTree(proc: ChildProcess, opts?: { exited?: () => boolean })

  // Preferred shell (respects $SHELL)
  export const preferred: () => string

  // Acceptable shell (excludes fish, nu for compatibility)
  export const acceptable: () => string
}
```

#### Shell Detection

```typescript
const BLACKLIST = new Set(["fish", "nu"])

function fallback() {
  if (process.platform === "win32") {
    // Try Git Bash, fall back to cmd.exe
    if (Flag.OPENCODE_GIT_BASH_PATH) return Flag.OPENCODE_GIT_BASH_PATH
    const git = Bun.which("git")
    if (git) {
      const bash = path.join(git, "..", "..", "bin", "bash.exe")
      if (Bun.file(bash).size) return bash
    }
    return process.env.COMSPEC || "cmd.exe"
  }
  if (process.platform === "darwin") return "/bin/zsh"
  return Bun.which("bash") || "/bin/sh"
}

export const acceptable = lazy(() => {
  const s = process.env.SHELL
  if (s && !BLACKLIST.has(path.basename(s))) return s
  return fallback()
})
```

#### Process Termination

```typescript
export async function killTree(proc: ChildProcess, opts?: { exited?: () => boolean }) {
  if (process.platform === "win32") {
    // Use taskkill with /t for tree kill
    spawn("taskkill", ["/pid", String(pid), "/f", "/t"])
  } else {
    // SIGTERM the process group
    process.kill(-pid, "SIGTERM")
    await Bun.sleep(200)
    if (!opts?.exited?.()) {
      process.kill(-pid, "SIGKILL")
    }
  }
}
```

---

## Output Formatting

### Formatter Module (`/format/`)

The formatter module auto-formats files after edits based on detected tools.

#### Supported Formatters

| Name | Command | Extensions |
|------|---------|------------|
| `gofmt` | `gofmt -w $FILE` | `.go` |
| `mix` | `mix format $FILE` | `.ex`, `.exs`, `.eex`, `.heex` |
| `prettier` | `bun x prettier --write $FILE` | `.js`, `.ts`, `.json`, `.md`, etc. |
| `biome` | `bun x @biomejs/biome check --write $FILE` | `.js`, `.ts`, `.json`, etc. |
| `oxfmt` | `bun x oxfmt $FILE` | `.js`, `.ts` (experimental) |
| `zig` | `zig fmt $FILE` | `.zig`, `.zon` |
| `clang-format` | `clang-format -i $FILE` | `.c`, `.cpp`, `.h` |
| `ktlint` | `ktlint -F $FILE` | `.kt`, `.kts` |
| `ruff` | `ruff format $FILE` | `.py`, `.pyi` |
| `uvformat` | `uv format -- $FILE` | `.py`, `.pyi` |
| `rubocop` | `rubocop --autocorrect $FILE` | `.rb`, `.rake` |
| `standardrb` | `standardrb --fix $FILE` | `.rb`, `.rake` |
| `htmlbeautifier` | `htmlbeautifier $FILE` | `.erb` |
| `dart` | `dart format $FILE` | `.dart` |
| `ocamlformat` | `ocamlformat -i $FILE` | `.ml`, `.mli` |
| `terraform` | `terraform fmt $FILE` | `.tf`, `.tfvars` |
| `latexindent` | `latexindent -w -s $FILE` | `.tex` |
| `gleam` | `gleam format $FILE` | `.gleam` |
| `shfmt` | `shfmt -w $FILE` | `.sh`, `.bash` |
| `nixfmt` | `nixfmt $FILE` | `.nix` |
| `rustfmt` | `rustfmt $FILE` | `.rs` |
| `pint` | `./vendor/bin/pint $FILE` | `.php` |
| `air` | `air format $FILE` | `.R` |

#### Formatter Detection

Each formatter has an `enabled()` function that checks for availability:

```typescript
// Example: biome detection
export const biome: Info = {
  name: "biome",
  command: [BunProc.which(), "x", "@biomejs/biome", "check", "--write", "$FILE"],
  extensions: [...],
  async enabled() {
    const configs = ["biome.json", "biome.jsonc"]
    for (const config of configs) {
      const found = await Filesystem.findUp(config, Instance.directory, Instance.worktree)
      if (found.length > 0) return true
    }
    return false
  },
}
```

#### Auto-Format on Edit

Formatters run automatically when files are edited:

```typescript
export function init() {
  Bus.subscribe(File.Event.Edited, async (payload) => {
    const file = payload.properties.file
    const ext = path.extname(file)

    for (const item of await getFormatter(ext)) {
      const proc = Bun.spawn({
        cmd: item.command.map((x) => x.replace("$FILE", file)),
        cwd: Instance.directory,
        env: { ...process.env, ...item.environment },
        stdout: "ignore",
        stderr: "ignore",
      })
      await proc.exited
    }
  })
}
```

### TUI Utilities

#### Clipboard (`tui/util/clipboard.ts`)

Cross-platform clipboard operations:

```typescript
export namespace Clipboard {
  export interface Content {
    data: string
    mime: string  // "text/plain" or "image/png"
  }

  // Read text or image from clipboard
  export async function read(): Promise<Content | undefined>

  // Write text (uses OSC 52 for SSH support)
  export async function copy(text: string)
}
```

Platform support:
- **macOS**: `osascript` for images, `pbcopy` for text
- **Linux**: `wl-paste`/`wl-copy` (Wayland), `xclip`/`xsel` (X11)
- **Windows**: PowerShell clipboard APIs

OSC 52 support for terminal emulator clipboard (works over SSH):
```typescript
function writeOsc52(text: string): void {
  const base64 = Buffer.from(text).toString("base64")
  const osc52 = `\x1b]52;c;${base64}\x07`
  // Wrap for tmux/screen
  const passthrough = process.env["TMUX"] || process.env["STY"]
  const sequence = passthrough ? `\x1bPtmux;\x1b${osc52}\x1b\\` : osc52
  process.stdout.write(sequence)
}
```

#### Editor (`tui/util/editor.ts`)

Opens external editor for prompt editing:

```typescript
export namespace Editor {
  export async function open(opts: {
    value: string
    renderer: CliRenderer
  }): Promise<string | undefined> {
    const editor = process.env["VISUAL"] || process.env["EDITOR"]
    if (!editor) return

    const filepath = join(tmpdir(), `${Date.now()}.md`)
    await Bun.write(filepath, opts.value)

    opts.renderer.suspend()
    const proc = Bun.spawn({
      cmd: [...editor.split(" "), filepath],
      stdin: "inherit",
      stdout: "inherit",
      stderr: "inherit",
    })
    await proc.exited

    const content = await Bun.file(filepath).text()
    opts.renderer.resume()
    return content || undefined
  }
}
```

#### Transcript (`tui/util/transcript.ts`)

Formats session messages for export:

```typescript
export function formatTranscript(
  session: SessionInfo,
  messages: MessageWithParts[],
  options: TranscriptOptions,
): string

// Options
export type TranscriptOptions = {
  thinking: boolean      // Include reasoning
  toolDetails: boolean   // Include tool input/output
  assistantMetadata: boolean  // Include agent/model info
}
```

#### Terminal Colors (`tui/util/terminal.ts`)

Queries terminal colors via OSC sequences:

```typescript
export namespace Terminal {
  export async function colors(): Promise<{
    background: RGBA | null
    foreground: RGBA | null
    colors: RGBA[]  // Palette 0-15
  }>

  export async function getTerminalBackgroundColor(): Promise<"dark" | "light">
}
```

#### Spinner (`tui/ui/spinner.ts`)

Knight Rider-style animated spinner:

```typescript
export function createFrames(options: KnightRiderOptions = {}): string[]
export function createColors(options: KnightRiderOptions = {}): ColorGenerator

export interface KnightRiderOptions {
  width?: number           // Default: 8
  style?: "blocks" | "diamonds"
  holdStart?: number       // Pause frames at start
  holdEnd?: number         // Pause frames at end
  color?: ColorInput       // Single color for trail derivation
  colors?: ColorInput[]    // Custom color gradient
  enableFading?: boolean   // Fade inactive dots
  minAlpha?: number        // Minimum alpha during fade
}
```

### Keybindings (`/util/keybind.ts`)

Keybinding parsing and matching:

```typescript
export namespace Keybind {
  export type Info = {
    name: string
    ctrl: boolean
    meta: boolean
    shift: boolean
    super?: boolean
    leader: boolean  // <leader> prefix
  }

  // Parse keybind strings like "ctrl+shift+p" or "<leader>g"
  export function parse(key: string): Info[]

  // Match two keybinds
  export function match(a: Info | undefined, b: Info): boolean

  // Format for display
  export function toString(info: Info): string
}
```

#### Textarea Keybindings (`tui/component/textarea-keybindings.ts`)

Maps config keybinds to textarea actions:

```typescript
const TEXTAREA_ACTIONS = [
  "submit", "newline",
  "move-left", "move-right", "move-up", "move-down",
  "select-left", "select-right", "select-up", "select-down",
  "line-home", "line-end",
  "buffer-home", "buffer-end",
  "delete-line", "delete-to-line-end", "delete-to-line-start",
  "backspace", "delete",
  "undo", "redo",
  "word-forward", "word-backward",
  "delete-word-forward", "delete-word-backward",
  // ... more
]
```

---

## Summary

OpenCode's CLI and TUI architecture demonstrates several sophisticated patterns:

1. **Yargs-based CLI** with type-safe command wrappers
2. **Worker-based TUI** separating UI rendering from backend
3. **Provider hierarchy** for modular state management
4. **Template-based command system** with MCP integration
5. **Cross-platform PTY** with WebSocket multiplexing
6. **Auto-detection formatters** triggered on file edits
7. **OSC sequence support** for terminal introspection

Key technologies:
- **@opentui/solid** - SolidJS-based terminal UI
- **bun-pty** - Native PTY bindings for Bun
- **yargs** - CLI argument parsing
- **@clack/prompts** - Interactive CLI prompts
- **hono** - HTTP server framework
