# OpenCode Auxiliary Packages Analysis

This document analyzes the auxiliary packages in OpenCode's monorepo architecture, focusing on the desktop app, SDK, plugin system, UI components, and frontend app.

---

## Package Overview

| Package | Purpose | Key Dependencies |
|---------|---------|------------------|
| `@opencode-ai/desktop` | Tauri v2 native desktop app | `@tauri-apps/*`, `solid-js` |
| `@opencode-ai/sdk` | JavaScript SDK for external integrations | Auto-generated from OpenAPI |
| `@opencode-ai/plugin` | Plugin system for extensibility | `@opencode-ai/sdk`, `zod` |
| `@opencode-ai/ui` | Shared SolidJS UI components | `@kobalte/core`, `solid-js`, `shiki` |
| `@opencode-ai/app` | Frontend application logic | `@opencode-ai/ui`, `@solidjs/router` |

---

## Desktop App (Tauri)

**Location:** `/packages/desktop/`

### Architecture

The desktop app is built with Tauri v2, providing a native wrapper around the web-based UI with enhanced capabilities.

```
packages/desktop/
├── src/                    # TypeScript frontend entry
│   ├── index.tsx           # Desktop-specific entry point
│   ├── menu.ts             # macOS native menu
│   ├── updater.ts          # Auto-update logic
│   ├── webview-zoom.ts     # Zoom controls
│   └── i18n/               # Desktop translations
├── src-tauri/              # Rust backend
│   ├── src/
│   │   ├── main.rs         # Entry point, display backend config
│   │   ├── lib.rs          # Core Tauri setup and commands
│   │   ├── cli.rs          # CLI sidecar management
│   │   ├── markdown.rs     # Native markdown parsing
│   │   └── job_object.rs   # Windows process management
│   ├── tauri.conf.json     # Tauri configuration
│   └── capabilities/       # Permission capabilities
└── scripts/                # Build scripts
```

### Core Features

#### 1. Sidecar Server Management

The desktop app spawns and manages the OpenCode CLI as a sidecar process:

```rust
// From lib.rs
fn spawn_sidecar(app: &AppHandle, hostname: &str, port: u32, password: &str) -> CommandChild {
    let (mut rx, child) = cli::create_command(
        app,
        format!("serve --hostname {hostname} --port {port}").as_str(),
    )
    .env("OPENCODE_SERVER_USERNAME", "opencode")
    .env("OPENCODE_SERVER_PASSWORD", password)
    .spawn()
    .expect("Failed to spawn opencode");
    // ... stdout/stderr logging
}
```

Key behaviors:
- Auto-detects free port using `TcpListener::bind("127.0.0.1:0")`
- Generates UUID password for authentication
- Supports custom server URLs from config
- Health checks via `/global/health` endpoint
- 30-second timeout for server startup

#### 2. CLI Installation and Sync

The app can install/update the CLI to the user's system:

```rust
// From cli.rs
const CLI_INSTALL_DIR: &str = ".opencode/bin";
const CLI_BINARY_NAME: &str = "opencode";

pub fn sync_cli(app: tauri::AppHandle) -> Result<(), String> {
    // Compare versions
    let cli_version = semver::Version::parse(&cli_version_str)?;
    let app_version = app.package_info().version.clone();

    if cli_version >= app_version {
        return Ok(());  // Already up to date
    }

    install_cli(app)?;  // Run install script
}
```

#### 3. Platform-Specific Adaptations

**Linux Wayland Handling:**
```rust
// From main.rs
fn configure_display_backend() -> Option<String> {
    // Prefer XWayland when available to avoid Wayland protocol errors
    if env::var_os("DISPLAY").is_some() {
        set_env_if_absent("WINIT_UNIX_BACKEND", "x11");
        set_env_if_absent("GDK_BACKEND", "x11");
    }
}
```

**Windows Proxy Bypass:**
```rust
// Ensure loopback never goes through proxy
upsert("NO_PROXY");  // Add 127.0.0.1, localhost, ::1
```

**macOS Title Bar:**
```rust
let window_builder = window_builder
    .title_bar_style(tauri::TitleBarStyle::Overlay)
    .hidden_title(true);
```

#### 4. Tauri Plugins Used

| Plugin | Purpose |
|--------|---------|
| `tauri-plugin-single-instance` | Prevent multiple app instances |
| `tauri-plugin-deep-link` | Handle `opencode://` URLs |
| `tauri-plugin-window-state` | Remember window position/size |
| `tauri-plugin-store` | Persistent key-value storage |
| `tauri-plugin-dialog` | Native file/folder dialogs |
| `tauri-plugin-shell` | Shell command execution |
| `tauri-plugin-process` | Process management |
| `tauri-plugin-opener` | Open URLs in browser |
| `tauri-plugin-clipboard-manager` | Clipboard access |
| `tauri-plugin-http` | HTTP requests |
| `tauri-plugin-notification` | System notifications |
| `tauri-plugin-updater` | Auto-updates |
| `tauri-plugin-os` | OS detection |
| `tauri-plugin-decorum` | Windows title bar overlay |

#### 5. Tauri Commands (IPC)

```rust
.invoke_handler(tauri::generate_handler![
    kill_sidecar,           // Stop the server process
    install_cli,            // Install CLI to system
    ensure_server_ready,    // Wait for server startup
    get_default_server_url, // Read from settings store
    set_default_server_url, // Save to settings store
    markdown::parse_markdown_command  // Native markdown parsing
])
```

#### 6. Desktop Platform Implementation

The desktop entry point creates a `Platform` object implementing:

```typescript
// From src/index.tsx
const createPlatform = (password: Accessor<string | null>): Platform => ({
  platform: "desktop",
  os: ostype(),  // macos, windows, linux
  version: pkg.version,

  // Native dialogs
  openDirectoryPickerDialog: (opts) => open({ directory: true, ... }),
  openFilePickerDialog: (opts) => open({ directory: false, ... }),
  saveFilePickerDialog: (opts) => save({ ... }),

  // Tauri storage with debounced writes
  storage: (name) => createStorage(name),  // Uses @tauri-apps/plugin-store

  // Auto-updates
  checkUpdate: async () => check().then(next => next.download()),
  update: async () => update.install(),

  // Authenticated fetch
  fetch: (input, init) => {
    if (pw) addHeader(input.headers, pw);  // Basic auth
    return tauriFetch(input);
  },

  // Native markdown (via Rust comrak crate)
  parseMarkdown: (markdown) => invoke("parse_markdown_command", { markdown }),
});
```

### Configuration

**tauri.conf.json:**
```json
{
  "productName": "OpenCode Dev",
  "identifier": "ai.opencode.desktop.dev",
  "app": {
    "windows": [{
      "label": "main",
      "titleBarStyle": "Overlay",
      "hiddenTitle": true,
      "trafficLightPosition": { "x": 12.0, "y": 18.0 }
    }],
    "withGlobalTauri": true,
    "macOSPrivateApi": true
  },
  "bundle": {
    "externalBin": ["sidecars/opencode-cli"]
  },
  "plugins": {
    "deep-link": {
      "desktop": { "schemes": ["opencode"] }
    }
  }
}
```

**Capabilities (default.json):**
```json
{
  "permissions": [
    "core:default",
    "opener:default",
    "deep-link:default",
    "shell:default",
    "updater:default",
    "dialog:default",
    "process:default",
    "store:default",
    "window-state:default",
    "os:default",
    "notification:default",
    { "identifier": "http:default", "allow": [{ "url": "http://*" }, { "url": "https://*" }] }
  ]
}
```

---

## JavaScript SDK

**Location:** `/packages/sdk/js/`

### Architecture

The SDK is auto-generated from an OpenAPI specification using `@hey-api/openapi-ts`.

```
packages/sdk/js/
├── src/
│   ├── index.ts        # Main entry, createOpencode()
│   ├── client.ts       # createOpencodeClient()
│   ├── server.ts       # createOpencodeServer(), createOpencodeTui()
│   ├── gen/            # Generated v1 SDK
│   │   ├── types.gen.ts    # TypeScript types
│   │   ├── sdk.gen.ts      # SDK methods
│   │   └── client/         # HTTP client
│   └── v2/             # Generated v2 SDK
│       └── gen/            # Same structure
├── example/
│   └── example.ts      # Usage examples
└── script/
    ├── build.ts        # Build script
    └── publish.ts      # Publish script
```

### API

#### Creating a Full Environment

```typescript
import { createOpencode } from "@opencode-ai/sdk"

// Spawns server process and creates client
const { client, server } = await createOpencode({
  hostname: "127.0.0.1",
  port: 4096,
  timeout: 5000,
  config: { logLevel: "debug" }
})

// Use the client
const session = await client.session.create()
await client.session.prompt({
  path: { id: session.data.id },
  body: {
    parts: [
      { type: "text", text: "Hello, world!" }
    ]
  }
})

// Cleanup
server.close()
```

#### Client-Only Usage

```typescript
import { createOpencodeClient } from "@opencode-ai/sdk/client"

const client = createOpencodeClient({
  baseUrl: "http://localhost:4096",
  directory: "/path/to/project",  // Sets x-opencode-directory header
})
```

#### Server Spawning

```typescript
import { createOpencodeServer } from "@opencode-ai/sdk/server"

const server = await createOpencodeServer({
  hostname: "127.0.0.1",
  port: 4096,
  timeout: 5000,
  signal: abortController.signal,
  config: {
    logLevel: "debug",
    // ... other config options
  }
})

console.log(server.url)  // "http://127.0.0.1:4096"
server.close()
```

#### TUI Spawning

```typescript
import { createOpencodeTui } from "@opencode-ai/sdk/server"

const tui = createOpencodeTui({
  project: "/path/to/project",
  model: "anthropic/claude-sonnet-4-20250514",
  session: "session-id",
  agent: "agent-name",
})

// TUI runs with inherited stdio
tui.close()
```

### Generated Types (Partial)

```typescript
// From types.gen.ts
export type UserMessage = {
  id: string
  sessionID: string
  role: "user"
  time: { created: number }
  summary?: { title?: string; body?: string; diffs: FileDiff[] }
  agent: string
  model: { providerID: string; modelID: string }
  system?: string
  tools?: { [key: string]: boolean }
}

export type AssistantMessage = {
  id: string
  sessionID: string
  role: "assistant"
  time: { created: number; completed?: number }
  error?: ProviderAuthError | UnknownError | ApiError
  modelID: string
  providerID: string
  cost: number
  tokens: {
    input: number
    output: number
    reasoning: number
    cache: { read: number; write: number }
  }
}

export type Part = TextPart | ReasoningPart | ToolCallPart | ...

export type Event = EventMessageUpdated | EventSessionStatus | ...
```

### SDK Exports

```typescript
// @opencode-ai/sdk
export { createOpencode } from "./index.js"
export { createOpencodeClient, OpencodeClient } from "./client.js"
export { createOpencodeServer, createOpencodeTui } from "./server.js"
export * from "./gen/types.gen.js"  // All generated types

// @opencode-ai/sdk/v2 - Version 2 API
// Same structure with updated endpoints
```

---

## Plugin System

**Location:** `/packages/plugin/`

### Architecture

Plugins are JavaScript/TypeScript modules that export a function returning hooks.

```
packages/plugin/
└── src/
    ├── index.ts    # Plugin types, hook definitions
    ├── tool.ts     # Tool definition helper
    ├── shell.ts    # Bun shell types
    └── example.ts  # Example plugin
```

### Plugin Structure

```typescript
// From index.ts
export type PluginInput = {
  client: ReturnType<typeof createOpencodeClient>  // SDK client
  project: Project                                  // Project info
  directory: string                                 // Working directory
  worktree: string                                  // Git worktree root
  serverUrl: URL                                    // Server URL
  $: BunShell                                       // Bun shell template
}

export type Plugin = (input: PluginInput) => Promise<Hooks>
```

### Hook System

Plugins can register hooks for various lifecycle events:

```typescript
export interface Hooks {
  // Event stream handler
  event?: (input: { event: Event }) => Promise<void>

  // Config modification
  config?: (input: Config) => Promise<void>

  // Custom tools
  tool?: { [key: string]: ToolDefinition }

  // Provider authentication
  auth?: AuthHook

  // Message interception
  "chat.message"?: (
    input: { sessionID, agent, model, messageID, variant },
    output: { message: UserMessage, parts: Part[] }
  ) => Promise<void>

  // LLM parameter modification
  "chat.params"?: (
    input: { sessionID, agent, model, provider, message },
    output: { temperature, topP, topK, options }
  ) => Promise<void>

  // Header injection
  "chat.headers"?: (
    input: { sessionID, agent, model, provider, message },
    output: { headers: Record<string, string> }
  ) => Promise<void>

  // Permission interception
  "permission.ask"?: (
    input: Permission,
    output: { status: "ask" | "deny" | "allow" }
  ) => Promise<void>

  // Command execution hooks
  "command.execute.before"?: (
    input: { command, sessionID, arguments },
    output: { parts: Part[] }
  ) => Promise<void>

  // Tool execution hooks
  "tool.execute.before"?: (
    input: { tool, sessionID, callID },
    output: { args: any }
  ) => Promise<void>

  "tool.execute.after"?: (
    input: { tool, sessionID, callID },
    output: { title, output, metadata }
  ) => Promise<void>

  // Experimental hooks
  "experimental.chat.messages.transform"?: ...
  "experimental.chat.system.transform"?: ...
  "experimental.session.compacting"?: ...
  "experimental.text.complete"?: ...
}
```

### Tool Definition

```typescript
// From tool.ts
import { z } from "zod"

export type ToolContext = {
  sessionID: string
  messageID: string
  agent: string
  directory: string   // Current project directory
  worktree: string    // Git worktree root
  abort: AbortSignal
  metadata(input: { title?: string; metadata?: object }): void
  ask(input: AskInput): Promise<void>  // Request permission
}

export function tool<Args extends z.ZodRawShape>(input: {
  description: string
  args: Args
  execute(args: z.infer<z.ZodObject<Args>>, context: ToolContext): Promise<string>
}) {
  return input
}
tool.schema = z  // Re-export zod for convenience
```

### Authentication Hooks

```typescript
export type AuthHook = {
  provider: string
  loader?: (auth, provider) => Promise<Record<string, any>>
  methods: (
    | {
        type: "oauth"
        label: string
        prompts?: Array<TextPrompt | SelectPrompt>
        authorize(inputs?): Promise<AuthOauthResult>
      }
    | {
        type: "api"
        label: string
        prompts?: Array<TextPrompt | SelectPrompt>
        authorize?(inputs?): Promise<{ type: "success"; key: string } | { type: "failed" }>
      }
  )[]
}

export type AuthOauthResult = {
  url: string
  instructions: string
} & (
  | { method: "auto"; callback(): Promise<TokenResult> }
  | { method: "code"; callback(code: string): Promise<TokenResult> }
)
```

### Example Plugin

```typescript
// From example.ts
import { Plugin } from "./index"
import { tool } from "./tool"

export const ExamplePlugin: Plugin = async (ctx) => {
  return {
    tool: {
      mytool: tool({
        description: "This is a custom tool",
        args: {
          foo: tool.schema.string().describe("foo"),
        },
        async execute(args) {
          return `Hello ${args.foo}!`
        },
      }),
    },
  }
}
```

### Bun Shell Types

The plugin system exposes Bun's shell API for scripting:

```typescript
// From shell.ts
export interface BunShell {
  (strings: TemplateStringsArray, ...expressions: ShellExpression[]): BunShellPromise
  braces(pattern: string): string[]    // Brace expansion
  escape(input: string): string        // Escape for shell
  env(newEnv?): BunShell               // Set environment
  cwd(newCwd?): BunShell               // Set working directory
  nothrow(): BunShell                  // Don't throw on errors
}

export interface BunShellPromise extends Promise<BunShellOutput> {
  stdin: WritableStream
  cwd(newCwd): this
  env(newEnv): this
  quiet(): this              // Buffer only, no echo
  lines(): AsyncIterable<string>
  text(encoding?): Promise<string>
  json(): Promise<any>
}
```

---

## Shared UI Components

**Location:** `/packages/ui/`

### Architecture

The UI package provides reusable SolidJS components with Tailwind CSS styling.

```
packages/ui/
└── src/
    ├── components/         # UI components
    │   ├── file-icons/     # File type icons
    │   └── provider-icons/ # Provider logos
    ├── context/            # React-style contexts
    ├── hooks/              # Reusable hooks
    ├── i18n/               # Internationalization
    ├── pierre/             # Diff viewer integration
    ├── styles/             # CSS and Tailwind
    ├── theme/              # Theme system
    └── assets/             # Fonts, audio files
```

### Components Catalog

| Component | Description | Size |
|-----------|-------------|------|
| `accordion` | Expandable sections | 2.6k |
| `avatar` | User/agent avatars | 1.2k |
| `basic-tool` | Tool call display | 4.3k |
| `button` | Standard buttons | 1.0k |
| `card` | Card containers | 0.6k |
| `checkbox` | Checkboxes | 1.7k |
| `code` | Syntax highlighted code | 13k |
| `collapsible` | Collapsible sections | 1.4k |
| `dialog` | Modal dialogs | 2.5k |
| `diff` | Code diff viewer | 17k |
| `diff-ssr` | Server-side diff | 9.6k |
| `dropdown-menu` | Dropdown menus | 9.5k |
| `file-icon` | File type icons | 14k |
| `hover-card` | Hover cards | 1.1k |
| `icon` | Icon system | 35k |
| `icon-button` | Icon buttons | 0.9k |
| `list` | Virtual lists | 11k |
| `logo` | OpenCode logos | 2.5k |
| `markdown` | Markdown renderer | 8.0k |
| `message-nav` | Message navigation | 3.3k |
| `message-part` | Message part renderer | 49k |
| `popover` | Popovers | 4.9k |
| `progress-circle` | Circular progress | 1.6k |
| `radio-group` | Radio buttons | 2.3k |
| `resize-handle` | Panel resize handles | 2.4k |
| `select` | Select dropdowns | 5.3k |
| `session-review` | Session review UI | 24k |
| `session-turn` | Conversation turn | 29k |
| `spinner` | Loading spinners | 1.4k |
| `switch` | Toggle switches | 1.1k |
| `tabs` | Tab navigation | 3.2k |
| `tag` | Tags/badges | 0.5k |
| `text-field` | Text inputs | 3.4k |
| `toast` | Toast notifications | 5.3k |
| `tooltip` | Tooltips | 2.6k |
| `typewriter` | Typewriter effect | 1.5k |

### Theme System

**Theme Types:**
```typescript
// From types.ts
export type HexColor = `#${string}`

export interface ThemeSeedColors {
  neutral: HexColor
  primary: HexColor
  success: HexColor
  warning: HexColor
  error: HexColor
  info: HexColor
  interactive: HexColor
  diffAdd: HexColor
  diffDelete: HexColor
}

export interface ThemeVariant {
  seeds: ThemeSeedColors
  overrides?: Record<string, ColorValue>
}

export interface DesktopTheme {
  $schema?: string
  name: string
  id: string
  light: ThemeVariant
  dark: ThemeVariant
}
```

**Theme Context:**
```typescript
// From context.tsx
export const { use: useTheme, provider: ThemeProvider } = createSimpleContext({
  name: "Theme",
  init: (props: { defaultTheme?: string }) => {
    const [store, setStore] = createStore({
      themes: DEFAULT_THEMES,
      themeId: "oc-1",
      colorScheme: "system" as ColorScheme,
      mode: getSystemMode(),
      previewThemeId: null,
      previewScheme: null,
    })

    return {
      themeId: () => store.themeId,
      colorScheme: () => store.colorScheme,
      mode: () => store.mode,
      themes: () => store.themes,
      setTheme,
      setColorScheme,
      registerTheme,
      previewTheme,
      previewColorScheme,
      commitPreview,
      cancelPreview,
    }
  },
})
```

Features:
- System color scheme detection
- Theme preview without committing
- CSS custom property generation
- LocalStorage persistence
- Dynamic theme registration

### Pierre Diff Viewer

Integration with `@pierre/diffs` library for code diffs:

```typescript
// From pierre/index.ts
export type DiffProps<T = {}> = FileDiffOptions<T> & {
  before: FileContents
  after: FileContents
  annotations?: DiffLineAnnotation<T>[]
  selectedLines?: SelectedLineRange | null
  commentedLines?: SelectedLineRange[]
  onRendered?: () => void
}

export function createDefaultOptions(style: "unified" | "split") {
  return {
    theme: "OpenCode",
    themeType: "system",
    diffStyle: style,
    diffIndicators: "bars",
    expansionLineCount: 20,
    lineDiffType: style === "split" ? "word-alt" : "none",
    // ... more options
  }
}
```

### Context Helpers

```typescript
// From context/helper.tsx
export function createSimpleContext<T, P>(options: {
  name: string
  init: (props: P) => T
}) {
  const Context = createContext<T>()

  const use = () => {
    const ctx = useContext(Context)
    if (!ctx) throw new Error(`use${options.name} must be used within ${options.name}Provider`)
    return ctx
  }

  const provider = (props: P & { children: JSX.Element }) => {
    const value = options.init(props)
    return <Context.Provider value={value}>{props.children}</Context.Provider>
  }

  return { use, provider }
}
```

### Hooks

**Auto-scroll:**
```typescript
// From hooks/create-auto-scroll.tsx
export function createAutoScroll(options: {
  container: () => HTMLElement | undefined
  active: () => boolean
  onScroll?: () => void
}) {
  // Automatically scroll to bottom when content changes
  // Pause when user scrolls up
}
```

**Filtered List:**
```typescript
// From hooks/use-filtered-list.tsx
export function useFilteredList<T>(options: {
  items: () => T[]
  filter: (item: T, query: string) => boolean
  query: () => string
}) {
  // Returns filtered items with fuzzy matching
}
```

### Package Exports

```typescript
// From package.json exports
{
  "./*": "./src/components/*.tsx",        // Individual components
  "./i18n/*": "./src/i18n/*.ts",          // Translations
  "./pierre": "./src/pierre/index.ts",    // Diff viewer
  "./hooks": "./src/hooks/index.ts",      // Hooks
  "./context": "./src/context/index.ts",  // Contexts
  "./context/*": "./src/context/*.tsx",
  "./styles": "./src/styles/index.css",   // Global styles
  "./theme": "./src/theme/index.ts",      // Theme system
  "./theme/context": "./src/theme/context.tsx",
  "./fonts/*": "./src/assets/fonts/*",
  "./audio/*": "./src/assets/audio/*"
}
```

---

## App Frontend

**Location:** `/packages/app/`

### Architecture

The app package contains the main frontend application built with SolidJS.

```
packages/app/
└── src/
    ├── app.tsx           # Main app component
    ├── entry.tsx         # Web entry point
    ├── index.ts          # Package exports
    ├── pages/            # Route pages
    │   ├── home.tsx      # Home/project list
    │   ├── session.tsx   # Chat session (116k!)
    │   ├── layout.tsx    # Main layout (106k!)
    │   └── error.tsx     # Error page
    ├── components/       # App-specific components
    │   ├── session/      # Session components
    │   ├── dialog-*.tsx  # Various dialogs
    │   ├── prompt-input.tsx  # Chat input (75k!)
    │   ├── terminal.tsx      # Terminal emulator
    │   └── settings-*.tsx    # Settings panels
    ├── context/          # State management
    ├── hooks/            # App hooks
    ├── utils/            # Utilities
    └── i18n/             # Translations
```

### Provider Architecture

The app uses a deeply nested provider pattern for state management:

```tsx
// From app.tsx
export function AppInterface(props: { defaultUrl?: string }) {
  return (
    <ServerProvider defaultUrl={defaultServerUrl()}>
      <ServerKey>
        <GlobalSDKProvider>
          <GlobalSyncProvider>
            <Router root={...}>
              <SettingsProvider>
                <PermissionProvider>
                  <LayoutProvider>
                    <NotificationProvider>
                      <ModelsProvider>
                        <CommandProvider>
                          <HighlightsProvider>
                            <Layout>{/* Routes */}</Layout>
                          </HighlightsProvider>
                        </CommandProvider>
                      </ModelsProvider>
                    </NotificationProvider>
                  </LayoutProvider>
                </PermissionProvider>
              </SettingsProvider>
            </Router>
          </GlobalSyncProvider>
        </GlobalSDKProvider>
      </ServerKey>
    </ServerProvider>
  )
}
```

### Context Providers

| Provider | Purpose | File Size |
|----------|---------|-----------|
| `GlobalSDKProvider` | SDK client instance | 3.1k |
| `GlobalSyncProvider` | Real-time data sync | 33k |
| `ServerProvider` | Server connection | 6.4k |
| `SettingsProvider` | User settings | 6.7k |
| `PermissionProvider` | Permission management | 5.7k |
| `LayoutProvider` | Layout state | 22k |
| `NotificationProvider` | System notifications | 7.6k |
| `ModelsProvider` | Available models | 4.2k |
| `CommandProvider` | Command palette | 9.3k |
| `TerminalProvider` | Terminal state | 7.9k |
| `FileProvider` | File viewer state | 19k |
| `PromptProvider` | Prompt input state | 6.7k |
| `CommentsProvider` | Code comments | 4.3k |
| `HighlightsProvider` | Syntax highlighting | 6.7k |
| `LanguageProvider` | Internationalization | 6.7k |
| `LocalProvider` | Local project state | 7.0k |
| `SyncProvider` | Session sync | 10k |

### Platform Abstraction

```typescript
// From context/platform.tsx
export type Platform = {
  platform: "web" | "desktop"
  os?: "macos" | "windows" | "linux"
  version?: string

  // Navigation
  openLink(url: string): void
  back(): void
  forward(): void
  restart(): Promise<void>

  // Notifications
  notify(title: string, description?: string, href?: string): Promise<void>

  // File dialogs (Tauri only)
  openDirectoryPickerDialog?(opts?): Promise<string | string[] | null>
  openFilePickerDialog?(opts?): Promise<string | string[] | null>
  saveFilePickerDialog?(opts?): Promise<string | null>

  // Storage
  storage?: (name?: string) => SyncStorage | AsyncStorage

  // Updates (Tauri only)
  checkUpdate?(): Promise<{ updateAvailable: boolean; version?: string }>
  update?(): Promise<void>

  // Network
  fetch?: typeof fetch

  // Server URL management
  getDefaultServerUrl?(): Promise<string | null> | string | null
  setDefaultServerUrl?(url: string | null): Promise<void> | void

  // Native markdown (desktop only)
  parseMarkdown?(markdown: string): Promise<string>
}
```

### Global SDK Provider

```typescript
// From context/global-sdk.tsx
export const { use: useGlobalSDK, provider: GlobalSDKProvider } = createSimpleContext({
  name: "GlobalSDK",
  init: () => {
    const server = useServer()
    const platform = usePlatform()

    // Event stream client
    const eventSdk = createOpencodeClient({
      baseUrl: server.url,
      signal: abort.signal,
      fetch: platform.fetch,
    })

    // Event emitter with coalescing
    const emitter = createGlobalEmitter<{ [key: string]: Event }>()

    // Batch events for 16ms to reduce re-renders
    const flush = () => {
      batch(() => {
        for (const event of events) {
          emitter.emit(event.directory, event.payload)
        }
      })
    }

    // Main SDK client
    const sdk = createOpencodeClient({
      baseUrl: server.url,
      fetch: platform.fetch,
      throwOnError: true,
    })

    return { url: server.url, client: sdk, event: emitter }
  },
})
```

### Route Structure

```tsx
<Router>
  <Route path="/" component={Home} />
  <Route path="/:dir" component={DirectoryLayout}>
    <Route path="/" component={() => <Navigate href="session" />} />
    <Route path="/session/:id?" component={Session} />
  </Route>
</Router>
```

- `/` - Home page with project list
- `/:dir` - Project-specific routes (`:dir` is base64 encoded path)
- `/:dir/session/:id?` - Chat session (optional ID for existing sessions)

### Key Components

**Prompt Input (75k):**
- Multi-line input with auto-resize
- File attachment handling
- Model selection
- Agent selection
- Command palette integration
- Keyboard shortcuts

**Session Page (116k):**
- Message list with virtualization
- Turn-by-turn conversation display
- Tool call visualization
- Code diff viewer
- Permission approval UI
- Session controls (fork, export, etc.)

**Layout (106k):**
- Split panel layout
- File tree sidebar
- Terminal panel
- Settings modal
- Command palette
- Resize handles

### Package Exports

```typescript
// From index.ts
export { PlatformProvider, type Platform } from "./context/platform"
export { AppBaseProviders, AppInterface } from "./app"
```

### E2E Testing

The app includes Playwright E2E tests:

```typescript
// From playwright.config.ts
export default {
  testDir: "e2e",
  webServer: {
    command: "bun dev",
    url: "http://localhost:3000",
  },
}
```

Environment variables:
- `PLAYWRIGHT_SERVER_HOST` / `PLAYWRIGHT_SERVER_PORT` - Backend address
- `PLAYWRIGHT_PORT` - Vite dev server port
- `PLAYWRIGHT_BASE_URL` - Override base URL

---

## Cross-Package Integration

### Dependency Graph

```
@opencode-ai/desktop
    ├── @opencode-ai/app
    │   ├── @opencode-ai/ui
    │   │   └── @opencode-ai/sdk
    │   └── @opencode-ai/sdk
    └── @opencode-ai/ui

@opencode-ai/plugin
    └── @opencode-ai/sdk
```

### Data Flow

```
Desktop App (Tauri)
    │
    ├── Spawns CLI as sidecar
    │       │
    │       └── CLI serves HTTP API
    │
    └── Renders App Frontend
            │
            ├── SDK Client ──→ HTTP API ──→ CLI Backend
            │
            └── UI Components ──→ Theme System
```

### Platform Detection

```typescript
// Web platform
const webPlatform: Platform = {
  platform: "web",
  openLink: (url) => window.open(url, "_blank"),
  restart: () => window.location.reload(),
  // ... minimal implementation
}

// Desktop platform
const desktopPlatform: Platform = {
  platform: "desktop",
  os: ostype(),  // macos, windows, linux
  openLink: (url) => shellOpen(url),
  restart: async () => {
    await invoke("kill_sidecar")
    await relaunch()
  },
  openDirectoryPickerDialog: (opts) => open({ directory: true, ... }),
  storage: (name) => Store.load(name),
  checkUpdate: () => check(),
  update: () => update.install(),
  // ... full implementation
}
```

---

## Key Takeaways for AVA

### Desktop App Patterns

1. **Sidecar Management**: Spawn the core CLI as a sidecar process with health checks
2. **Platform Abstraction**: Create a `Platform` interface for web/desktop differences
3. **Deep Links**: Register URL schemes for external integration
4. **Auto-Update**: Use Tauri's updater plugin with proper sidecar cleanup
5. **CLI Sync**: Keep installed CLI in sync with app version

### SDK Patterns

1. **OpenAPI Generation**: Auto-generate SDK from API specification
2. **Multiple Versions**: Support v1/v2 API versions simultaneously
3. **Server Spawning**: SDK can spawn and manage server processes
4. **Custom Fetch**: Allow fetch override for authentication

### Plugin Patterns

1. **Hook System**: Define hooks for various lifecycle events
2. **Tool Definition**: Use Zod for type-safe tool arguments
3. **Shell Access**: Provide shell template tag for scripting
4. **Auth Hooks**: Support OAuth and API key authentication flows

### UI Patterns

1. **Theme System**: Seed colors with automatic palette generation
2. **Context Pattern**: Use `createSimpleContext` helper
3. **Diff Viewer**: Integrate Pierre for code diffs
4. **Virtual Lists**: Use virtualization for large lists

### App Patterns

1. **Deep Provider Nesting**: Organize state in domain-specific providers
2. **Event Coalescing**: Batch events to reduce re-renders
3. **Platform Abstraction**: Abstract platform differences at context level
4. **Route-Based Code Splitting**: Lazy load route components
