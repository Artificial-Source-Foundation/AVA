# OpenCode Config & Project Analysis

> Deep analysis of OpenCode's configuration system, project detection, environment handling, and data persistence.

**Source files analyzed:**
- `/packages/opencode/src/config/config.ts`
- `/packages/opencode/src/config/markdown.ts`
- `/packages/opencode/src/project/project.ts`
- `/packages/opencode/src/project/instance.ts`
- `/packages/opencode/src/project/state.ts`
- `/packages/opencode/src/project/bootstrap.ts`
- `/packages/opencode/src/project/vcs.ts`
- `/packages/opencode/src/env/index.ts`
- `/packages/opencode/src/installation/index.ts`
- `/packages/opencode/src/storage/storage.ts`
- `/packages/opencode/src/global/index.ts`
- `/packages/opencode/src/flag/flag.ts`
- `/packages/opencode/src/session/instruction.ts`
- `/packages/opencode/src/skill/skill.ts`
- `/packages/opencode/src/plugin/index.ts`
- `/packages/opencode/src/auth/index.ts`
- `/packages/opencode/src/util/filesystem.ts`

---

## Table of Contents

1. [Configuration System Overview](#configuration-system-overview)
2. [Configuration File Formats](#configuration-file-formats)
3. [Configuration Loading & Merging](#configuration-loading--merging)
4. [Project Detection](#project-detection)
5. [Instance Management](#instance-management)
6. [Environment Variables](#environment-variables)
7. [Storage System](#storage-system)
8. [Global Paths](#global-paths)
9. [Instructions System](#instructions-system)
10. [Skills & Plugins](#skills--plugins)
11. [Authentication Storage](#authentication-storage)
12. [Key Takeaways for AVA](#key-takeaways-for-estela)

---

## Configuration System Overview

OpenCode uses a **layered configuration system** with multiple precedence levels. Configuration is loaded from various sources and merged together, with later sources overriding earlier ones.

### Precedence Order (Lowest to Highest)

1. **Remote/Well-known config** - Fetched from `{auth-url}/.well-known/opencode`
2. **Global user config** - `~/.config/opencode/opencode.json` or `opencode.jsonc`
3. **Custom config path** - Via `OPENCODE_CONFIG` env var
4. **Project config** - `opencode.json` or `opencode.jsonc` in project directories (upward search)
5. **Inline config** - Via `OPENCODE_CONFIG_CONTENT` env var
6. **Managed config** - Enterprise deployments (highest priority, admin-controlled)

### Managed Config Directories (Enterprise)

```typescript
// Platform-specific managed config directories
switch (process.platform) {
  case "darwin":
    return "/Library/Application Support/opencode"
  case "win32":
    return path.join(process.env.ProgramData || "C:\\ProgramData", "opencode")
  default:
    return "/etc/opencode"
}
```

---

## Configuration File Formats

### Primary Config Files

**Supported formats:**
- `opencode.json` - Standard JSON
- `opencode.jsonc` - JSON with Comments (preferred)
- `config.json` - Legacy format (global only)

### Config Schema (Zod-based validation)

```typescript
export const Info = z.object({
  $schema: z.string().optional(),
  theme: z.string().optional(),
  keybinds: Keybinds.optional(),
  logLevel: Log.Level.optional(),
  tui: TUI.optional(),
  server: Server.optional(),
  command: z.record(z.string(), Command).optional(),
  skills: Skills.optional(),
  watcher: z.object({ ignore: z.array(z.string()).optional() }).optional(),
  plugin: z.string().array().optional(),
  snapshot: z.boolean().optional(),
  share: z.enum(["manual", "auto", "disabled"]).optional(),
  autoupdate: z.union([z.boolean(), z.literal("notify")]).optional(),
  disabled_providers: z.array(z.string()).optional(),
  enabled_providers: z.array(z.string()).optional(),
  model: z.string().optional(),           // format: "provider/model"
  small_model: z.string().optional(),     // for title generation
  default_agent: z.string().optional(),
  username: z.string().optional(),
  mode: z.object({...}).optional(),       // @deprecated - use agent
  agent: z.object({...}).optional(),
  provider: z.record(z.string(), Provider).optional(),
  mcp: z.record(z.string(), Mcp).optional(),
  formatter: z.union([z.literal(false), z.record(...)]).optional(),
  lsp: z.union([z.literal(false), z.record(...)]).optional(),
  instructions: z.array(z.string()).optional(),
  permission: Permission.optional(),
  compaction: z.object({
    auto: z.boolean().optional(),
    prune: z.boolean().optional(),
  }).optional(),
  experimental: z.object({...}).optional(),
})
```

### Variable Substitution

Config files support **variable substitution**:

```json
{
  "provider": {
    "anthropic": {
      "options": {
        "apiKey": "{env:ANTHROPIC_API_KEY}"
      }
    }
  },
  "agent": {
    "custom": {
      "prompt": "{file:./prompts/custom.md}"
    }
  }
}
```

**Supported substitutions:**
- `{env:VAR_NAME}` - Environment variable
- `{file:path}` - File content (relative to config file, supports `~/`)

### Command/Agent/Mode Markdown Format

Commands, agents, and modes can be defined as markdown files with YAML frontmatter:

```markdown
---
description: "Run tests"
agent: "build"
model: "anthropic/claude-3-5-sonnet-20241022"
---

Run the test suite with coverage.
```

**Glob patterns for loading:**
- Commands: `{command,commands}/**/*.md`
- Agents: `{agent,agents}/**/*.md`
- Modes: `{mode,modes}/*.md`

---

## Configuration Loading & Merging

### Loading Process

```typescript
export const state = Instance.state(async () => {
  let result: Info = {}

  // 1. Load remote/well-known config (from auth providers)
  for (const [key, value] of Object.entries(auth)) {
    if (value.type === "wellknown") {
      const response = await fetch(`${key}/.well-known/opencode`)
      result = mergeConfigConcatArrays(result, await load(response.json()))
    }
  }

  // 2. Global user config
  result = mergeConfigConcatArrays(result, await global())

  // 3. Custom config path (OPENCODE_CONFIG)
  if (Flag.OPENCODE_CONFIG) {
    result = mergeConfigConcatArrays(result, await loadFile(Flag.OPENCODE_CONFIG))
  }

  // 4. Project config (upward search)
  if (!Flag.OPENCODE_DISABLE_PROJECT_CONFIG) {
    for (const file of ["opencode.jsonc", "opencode.json"]) {
      const found = await Filesystem.findUp(file, Instance.directory, Instance.worktree)
      for (const resolved of found.toReversed()) {
        result = mergeConfigConcatArrays(result, await loadFile(resolved))
      }
    }
  }

  // 5. Inline config (OPENCODE_CONFIG_CONTENT)
  if (Flag.OPENCODE_CONFIG_CONTENT) {
    result = mergeConfigConcatArrays(result, JSON.parse(Flag.OPENCODE_CONFIG_CONTENT))
  }

  // 6. Load from .opencode/ directories
  // 7. Load managed config (enterprise) - highest priority
})
```

### Merging Strategy

```typescript
// Custom merge that concatenates arrays instead of replacing
function mergeConfigConcatArrays(target: Info, source: Info): Info {
  const merged = mergeDeep(target, source)
  if (target.plugin && source.plugin) {
    merged.plugin = Array.from(new Set([...target.plugin, ...source.plugin]))
  }
  if (target.instructions && source.instructions) {
    merged.instructions = Array.from(new Set([...target.instructions, ...source.instructions]))
  }
  return merged
}
```

### Configuration Directories

The system scans these directories for commands, agents, modes, plugins, and skills:

```typescript
const directories = [
  Global.Path.config,                              // ~/.config/opencode/
  ...Filesystem.up({                               // .opencode/ dirs upward
    targets: [".opencode"],
    start: Instance.directory,
    stop: Instance.worktree,
  }),
  ...Filesystem.up({                               // ~/.opencode/
    targets: [".opencode"],
    start: Global.Path.home,
    stop: Global.Path.home,
  }),
  Flag.OPENCODE_CONFIG_DIR,                        // Custom dir (if set)
]
```

For each directory, it:
1. Installs dependencies (`npm install` with `@opencode-ai/plugin`)
2. Loads config files
3. Loads commands (`{command,commands}/**/*.md`)
4. Loads agents (`{agent,agents}/**/*.md`)
5. Loads modes (`{mode,modes}/*.md`)
6. Loads plugins (`{plugin,plugins}/*.{ts,js}`)

---

## Project Detection

### Project Identification

Projects are identified by their **Git root commit hash**:

```typescript
export async function fromDirectory(directory: string) {
  // Find .git directory
  const matches = Filesystem.up({ targets: [".git"], start: directory })
  const git = await matches.next().then((x) => x.value)

  if (git) {
    let sandbox = path.dirname(git)

    // Try to read cached ID
    let id = await Bun.file(path.join(git, "opencode")).text().catch(() => undefined)

    // Generate ID from root commit if not cached
    if (!id) {
      const roots = await $`git rev-list --max-parents=0 --all`
        .quiet().nothrow().cwd(sandbox).text()
        .then((x) => x.split("\n").filter(Boolean).map((x) => x.trim()).toSorted())

      id = roots[0]

      // Cache the ID in .git/opencode
      if (id) {
        await Bun.file(path.join(git, "opencode")).write(id)
      }
    }

    // Get worktree (for git worktrees support)
    const worktree = await $`git rev-parse --git-common-dir`
      .quiet().nothrow().cwd(sandbox).text()
      .then((x) => {
        const dirname = path.dirname(x.trim())
        return dirname === "." ? sandbox : dirname
      })

    return { id, sandbox, worktree, vcs: "git" }
  }

  // No git repo - use "global" project
  return { id: "global", worktree: "/", sandbox: "/", vcs: undefined }
}
```

### Project Info Schema

```typescript
export const Info = z.object({
  id: z.string(),                    // Git root commit hash or "global"
  worktree: z.string(),              // Git worktree root
  vcs: z.literal("git").optional(),
  name: z.string().optional(),       // User-defined name
  icon: z.object({
    url: z.string().optional(),      // Data URL or path
    override: z.string().optional(), // Emoji override
    color: z.string().optional(),
  }).optional(),
  commands: z.object({
    start: z.string().optional(),    // Startup script for new worktrees
  }).optional(),
  time: z.object({
    created: z.number(),
    updated: z.number(),
    initialized: z.number().optional(),
  }),
  sandboxes: z.array(z.string()),    // Additional worktree paths
})
```

### Sandboxes (Worktrees)

OpenCode supports multiple git worktrees per project:

```typescript
// Add a sandbox (worktree)
export async function addSandbox(projectID: string, directory: string) {
  await Storage.update<Info>(["project", projectID], (draft) => {
    const sandboxes = draft.sandboxes ?? []
    if (!sandboxes.includes(directory)) sandboxes.push(directory)
    draft.sandboxes = sandboxes
  })
}

// List valid sandboxes (filter out deleted directories)
export async function sandboxes(projectID: string) {
  const project = await Storage.read<Info>(["project", projectID])
  const valid: string[] = []
  for (const dir of project.sandboxes) {
    const stat = await fs.stat(dir).catch(() => undefined)
    if (stat?.isDirectory()) valid.push(dir)
  }
  return valid
}
```

---

## Instance Management

### Instance Context

The `Instance` namespace manages per-project state using async context:

```typescript
interface Context {
  directory: string    // Current working directory
  worktree: string     // Git worktree root (or sandbox root)
  project: Project.Info
}

export const Instance = {
  async provide<R>(input: { directory: string; init?: () => Promise<any>; fn: () => R }): Promise<R> {
    let existing = cache.get(input.directory)
    if (!existing) {
      existing = iife(async () => {
        const { project, sandbox } = await Project.fromDirectory(input.directory)
        const ctx = { directory: input.directory, worktree: sandbox, project }
        await context.provide(ctx, async () => {
          await input.init?.()
        })
        return ctx
      })
      cache.set(input.directory, existing)
    }
    const ctx = await existing
    return context.provide(ctx, input.fn)
  },

  get directory() { return context.use().directory },
  get worktree() { return context.use().worktree },
  get project() { return context.use().project },

  // Check if path is within project boundaries
  containsPath(filepath: string) {
    if (Filesystem.contains(Instance.directory, filepath)) return true
    if (Instance.worktree === "/") return false  // Non-git projects
    return Filesystem.contains(Instance.worktree, filepath)
  },

  // Create instance-scoped state
  state<S>(init: () => S, dispose?: (state: Awaited<S>) => Promise<void>): () => S {
    return State.create(() => Instance.directory, init, dispose)
  },
}
```

### State Management

The `State` namespace provides scoped state management with disposal:

```typescript
export namespace State {
  interface Entry {
    state: any
    dispose?: (state: any) => Promise<void>
  }

  const recordsByKey = new Map<string, Map<any, Entry>>()

  export function create<S>(root: () => string, init: () => S, dispose?: (state: Awaited<S>) => Promise<void>) {
    return () => {
      const key = root()
      let entries = recordsByKey.get(key)
      if (!entries) {
        entries = new Map()
        recordsByKey.set(key, entries)
      }
      const exists = entries.get(init)
      if (exists) return exists.state as S

      const state = init()
      entries.set(init, { state, dispose })
      return state
    }
  }

  export async function dispose(key: string) {
    const entries = recordsByKey.get(key)
    if (!entries) return

    const tasks: Promise<void>[] = []
    for (const [init, entry] of entries) {
      if (!entry.dispose) continue
      const task = Promise.resolve(entry.state)
        .then((state) => entry.dispose!(state))
      tasks.push(task)
    }
    await Promise.all(tasks)
    entries.clear()
    recordsByKey.delete(key)
  }
}
```

### Bootstrap Process

```typescript
export async function InstanceBootstrap() {
  Log.Default.info("bootstrapping", { directory: Instance.directory })

  await Plugin.init()      // Load plugins
  Share.init()             // Sharing service
  ShareNext.init()         // Next-gen sharing
  Format.init()            // Code formatting
  await LSP.init()         // Language servers
  FileWatcher.init()       // File watching
  File.init()              // File service
  Vcs.init()               // Version control
  Snapshot.init()          // Git snapshots
  Truncate.init()          // Output truncation

  // Track project initialization
  Bus.subscribe(Command.Event.Executed, async (payload) => {
    if (payload.properties.name === Command.Default.INIT) {
      await Project.setInitialized(Instance.project.id)
    }
  })
}
```

---

## Environment Variables

### Flag System

```typescript
export namespace Flag {
  // Configuration paths
  export const OPENCODE_CONFIG = process.env["OPENCODE_CONFIG"]
  export const OPENCODE_CONFIG_CONTENT = process.env["OPENCODE_CONFIG_CONTENT"]
  export declare const OPENCODE_CONFIG_DIR: string | undefined
  export declare const OPENCODE_DISABLE_PROJECT_CONFIG: boolean

  // Feature toggles
  export const OPENCODE_DISABLE_AUTOUPDATE = truthy("OPENCODE_DISABLE_AUTOUPDATE")
  export const OPENCODE_DISABLE_PRUNE = truthy("OPENCODE_DISABLE_PRUNE")
  export const OPENCODE_DISABLE_AUTOCOMPACT = truthy("OPENCODE_DISABLE_AUTOCOMPACT")
  export const OPENCODE_DISABLE_TERMINAL_TITLE = truthy("OPENCODE_DISABLE_TERMINAL_TITLE")
  export const OPENCODE_DISABLE_DEFAULT_PLUGINS = truthy("OPENCODE_DISABLE_DEFAULT_PLUGINS")
  export const OPENCODE_DISABLE_LSP_DOWNLOAD = truthy("OPENCODE_DISABLE_LSP_DOWNLOAD")
  export const OPENCODE_DISABLE_MODELS_FETCH = truthy("OPENCODE_DISABLE_MODELS_FETCH")
  export const OPENCODE_DISABLE_CLAUDE_CODE = truthy("OPENCODE_DISABLE_CLAUDE_CODE")
  export const OPENCODE_DISABLE_CLAUDE_CODE_PROMPT = ...
  export const OPENCODE_DISABLE_CLAUDE_CODE_SKILLS = ...
  export const OPENCODE_DISABLE_FILETIME_CHECK = truthy("OPENCODE_DISABLE_FILETIME_CHECK")

  // Permissions override (JSON)
  export const OPENCODE_PERMISSION = process.env["OPENCODE_PERMISSION"]

  // Server settings
  export const OPENCODE_SERVER_PASSWORD = process.env["OPENCODE_SERVER_PASSWORD"]
  export const OPENCODE_SERVER_USERNAME = process.env["OPENCODE_SERVER_USERNAME"]
  export const OPENCODE_CLIENT = process.env["OPENCODE_CLIENT"] ?? "cli"

  // Experimental features
  export const OPENCODE_EXPERIMENTAL = truthy("OPENCODE_EXPERIMENTAL")
  export const OPENCODE_EXPERIMENTAL_FILEWATCHER = ...
  export const OPENCODE_EXPERIMENTAL_ICON_DISCOVERY = ...
  export const OPENCODE_EXPERIMENTAL_BASH_DEFAULT_TIMEOUT_MS = ...
  export const OPENCODE_EXPERIMENTAL_OUTPUT_TOKEN_MAX = ...
  export const OPENCODE_EXPERIMENTAL_PLAN_MODE = ...
  export const OPENCODE_ENABLE_EXA = ...
  export const OPENCODE_ENABLE_EXPERIMENTAL_MODELS = ...

  // Path overrides
  export const OPENCODE_GIT_BASH_PATH = process.env["OPENCODE_GIT_BASH_PATH"]
  export const OPENCODE_MODELS_URL = process.env["OPENCODE_MODELS_URL"]
  export const OPENCODE_FAKE_VCS = process.env["OPENCODE_FAKE_VCS"]
}
```

### Dynamic Getters

Some flags are evaluated at access time for runtime configurability:

```typescript
// Dynamic getter - evaluated at access time
Object.defineProperty(Flag, "OPENCODE_DISABLE_PROJECT_CONFIG", {
  get() {
    return truthy("OPENCODE_DISABLE_PROJECT_CONFIG")
  },
  enumerable: true,
  configurable: false,
})
```

### Env Namespace

Simple wrapper around `process.env` with instance scoping:

```typescript
export namespace Env {
  const state = Instance.state(() => {
    return process.env as Record<string, string | undefined>
  })

  export function get(key: string) { return state()[key] }
  export function all() { return state() }
  export function set(key: string, value: string) { state()[key] = value }
  export function remove(key: string) { delete state()[key] }
}
```

---

## Storage System

### Storage Paths

OpenCode uses XDG Base Directory Specification:

```typescript
import { xdgData, xdgCache, xdgConfig, xdgState } from "xdg-basedir"

const app = "opencode"

export namespace Global {
  export const Path = {
    get home() {
      return process.env.OPENCODE_TEST_HOME || os.homedir()
    },
    data: path.join(xdgData!, app),       // ~/.local/share/opencode/
    bin: path.join(data, "bin"),           // ~/.local/share/opencode/bin/
    log: path.join(data, "log"),           // ~/.local/share/opencode/log/
    cache: path.join(xdgCache!, app),      // ~/.cache/opencode/
    config: path.join(xdgConfig!, app),    // ~/.config/opencode/
    state: path.join(xdgState!, app),      // ~/.local/state/opencode/
  }
}

// Directories are created on module load
await Promise.all([
  fs.mkdir(Global.Path.data, { recursive: true }),
  fs.mkdir(Global.Path.config, { recursive: true }),
  fs.mkdir(Global.Path.state, { recursive: true }),
  fs.mkdir(Global.Path.log, { recursive: true }),
  fs.mkdir(Global.Path.bin, { recursive: true }),
])
```

### Cache Versioning

```typescript
const CACHE_VERSION = "21"

const version = await Bun.file(path.join(Global.Path.cache, "version"))
  .text().catch(() => "0")

if (version !== CACHE_VERSION) {
  // Clear entire cache on version change
  const contents = await fs.readdir(Global.Path.cache)
  await Promise.all(
    contents.map((item) => fs.rm(path.join(Global.Path.cache, item), {
      recursive: true, force: true,
    }))
  )
  await Bun.file(path.join(Global.Path.cache, "version")).write(CACHE_VERSION)
}
```

### Storage API

JSON file-based storage with locking:

```typescript
export namespace Storage {
  // Storage root: ~/.local/share/opencode/storage/
  const state = lazy(async () => {
    const dir = path.join(Global.Path.data, "storage")
    // Run migrations...
    return { dir }
  })

  export async function read<T>(key: string[]) {
    const dir = await state().then((x) => x.dir)
    const target = path.join(dir, ...key) + ".json"
    using _ = await Lock.read(target)
    return await Bun.file(target).json() as T
  }

  export async function write<T>(key: string[], content: T) {
    const dir = await state().then((x) => x.dir)
    const target = path.join(dir, ...key) + ".json"
    using _ = await Lock.write(target)
    await Bun.write(target, JSON.stringify(content, null, 2))
  }

  export async function update<T>(key: string[], fn: (draft: T) => void) {
    const dir = await state().then((x) => x.dir)
    const target = path.join(dir, ...key) + ".json"
    using _ = await Lock.write(target)
    const content = await Bun.file(target).json()
    fn(content)
    await Bun.write(target, JSON.stringify(content, null, 2))
    return content as T
  }

  export async function remove(key: string[]) {
    const dir = await state().then((x) => x.dir)
    const target = path.join(dir, ...key) + ".json"
    await fs.unlink(target).catch(() => {})
  }

  export async function list(prefix: string[]) {
    const dir = await state().then((x) => x.dir)
    const results = await Array.fromAsync(
      glob.scan({ cwd: path.join(dir, ...prefix), onlyFiles: true })
    )
    return results.map((x) => [...prefix, ...x.slice(0, -5).split(path.sep)])
  }
}
```

### Storage Structure

```
~/.local/share/opencode/storage/
├── migration                      # Migration version number
├── project/
│   ├── {git-root-commit}.json    # Project info
│   └── global.json               # Global project (no git)
├── session/
│   └── {projectID}/
│       └── {sessionID}.json      # Session info
├── message/
│   └── {sessionID}/
│       └── {messageID}.json      # Message data
├── part/
│   └── {messageID}/
│       └── {partID}.json         # Message parts
└── session_diff/
    └── {sessionID}.json          # Session diffs
```

---

## Instructions System

### Instruction Files

OpenCode loads project instructions from multiple sources:

```typescript
const FILES = [
  "AGENTS.md",
  "CLAUDE.md",
  "CONTEXT.md",  // deprecated
]

// Global instruction files
function globalFiles() {
  const files = [path.join(Global.Path.config, "AGENTS.md")]
  if (!Flag.OPENCODE_DISABLE_CLAUDE_CODE_PROMPT) {
    files.push(path.join(os.homedir(), ".claude", "CLAUDE.md"))
  }
  if (Flag.OPENCODE_CONFIG_DIR) {
    files.push(path.join(Flag.OPENCODE_CONFIG_DIR, "AGENTS.md"))
  }
  return files
}
```

### Instruction Loading

```typescript
export async function systemPaths() {
  const config = await Config.get()
  const paths = new Set<string>()

  // 1. Project-level instruction files (upward search)
  if (!Flag.OPENCODE_DISABLE_PROJECT_CONFIG) {
    for (const file of FILES) {
      const matches = await Filesystem.findUp(file, Instance.directory, Instance.worktree)
      if (matches.length > 0) {
        matches.forEach((p) => paths.add(path.resolve(p)))
        break  // Stop at first found type
      }
    }
  }

  // 2. Global instruction files
  for (const file of globalFiles()) {
    if (await Bun.file(file).exists()) {
      paths.add(path.resolve(file))
      break
    }
  }

  // 3. Additional instructions from config
  if (config.instructions) {
    for (let instruction of config.instructions) {
      // Skip URLs (handled separately)
      if (instruction.startsWith("https://") || instruction.startsWith("http://")) continue

      // Handle ~/paths
      if (instruction.startsWith("~/")) {
        instruction = path.join(os.homedir(), instruction.slice(2))
      }

      // Glob patterns for relative paths
      const matches = path.isAbsolute(instruction)
        ? await glob.scan(...)
        : await resolveRelative(instruction)
      matches.forEach((p) => paths.add(path.resolve(p)))
    }
  }

  return paths
}
```

### Directory-Specific Instructions

When reading a file, OpenCode loads instructions from parent directories:

```typescript
export async function resolve(messages: MessageV2.WithParts[], filepath: string, messageID: string) {
  const system = await systemPaths()
  const already = loaded(messages)  // Instructions already in conversation
  const results: { filepath: string; content: string }[] = []

  let current = path.dirname(path.resolve(filepath))
  const root = path.resolve(Instance.directory)

  // Walk up from file to project root
  while (current.startsWith(root)) {
    const found = await find(current)  // Find AGENTS.md/CLAUDE.md/CONTEXT.md
    if (found && !system.has(found) && !already.has(found) && !isClaimed(messageID, found)) {
      claim(messageID, found)
      const content = await Bun.file(found).text()
      if (content) {
        results.push({ filepath: found, content: "Instructions from: " + found + "\n" + content })
      }
    }
    if (current === root) break
    current = path.dirname(current)
  }

  return results
}
```

---

## Skills & Plugins

### Skill System

Skills are defined as markdown files with YAML frontmatter:

```typescript
export const OPENCODE_SKILL_GLOB = new Bun.Glob("{skill,skills}/**/SKILL.md")
export const CLAUDE_SKILL_GLOB = new Bun.Glob("skills/**/SKILL.md")

export const Info = z.object({
  name: z.string(),
  description: z.string(),
  location: z.string(),
})
```

**Skill Search Order:**
1. `.claude/skills/` directories (project level, upward search)
2. `~/.claude/skills/` (global Claude skills)
3. `.opencode/skill/` directories (from Config.directories)
4. Additional paths from `config.skills.paths`

### Plugin System

Plugins extend OpenCode functionality:

```typescript
// Built-in plugins
const BUILTIN = ["opencode-anthropic-auth@0.0.13", "@gitlab/opencode-gitlab-auth@1.3.2"]

// Internal plugins (directly imported)
const INTERNAL_PLUGINS: PluginInstance[] = [CodexAuthPlugin, CopilotAuthPlugin]

// Plugin loading from config directories
const PLUGIN_GLOB = new Bun.Glob("{plugin,plugins}/*.{ts,js}")
```

**Plugin Input:**

```typescript
const input: PluginInput = {
  client,               // OpenCode API client
  project: Instance.project,
  worktree: Instance.worktree,
  directory: Instance.directory,
  serverUrl: Server.url(),
  $: Bun.$,            // Bun shell
}
```

---

## Authentication Storage

```typescript
// Storage path: ~/.local/share/opencode/auth.json
const filepath = path.join(Global.Path.data, "auth.json")

// Auth types
export const Info = z.discriminatedUnion("type", [
  // OAuth tokens
  z.object({
    type: z.literal("oauth"),
    refresh: z.string(),
    access: z.string(),
    expires: z.number(),
    accountId: z.string().optional(),
    enterpriseUrl: z.string().optional(),
  }),
  // API keys
  z.object({
    type: z.literal("api"),
    key: z.string(),
  }),
  // Well-known discovery
  z.object({
    type: z.literal("wellknown"),
    key: z.string(),
    token: z.string(),
  }),
])

export async function all(): Promise<Record<string, Info>> {
  const file = Bun.file(filepath)
  const data = await file.json().catch(() => ({}))
  // Validate each entry with Zod
  return Object.entries(data).reduce((acc, [key, value]) => {
    const parsed = Info.safeParse(value)
    if (parsed.success) acc[key] = parsed.data
    return acc
  }, {})
}

export async function set(key: string, info: Info) {
  const data = await all()
  await Bun.write(file, JSON.stringify({ ...data, [key]: info }, null, 2), { mode: 0o600 })
}
```

---

## Key Takeaways for AVA

### Configuration Best Practices

1. **Layered Configuration**: Implement precedence-based merging (global < project < inline)
2. **JSONC Support**: Use `jsonc-parser` for JSON with comments
3. **Variable Substitution**: Support `{env:VAR}` and `{file:path}` in config
4. **Zod Validation**: Define strict schemas with helpful error messages
5. **Backwards Compatibility**: Handle deprecated fields gracefully (e.g., `mode` -> `agent`)

### Project Detection

1. **Git-Based ID**: Use root commit hash as stable project identifier
2. **ID Caching**: Store ID in `.git/opencode` for fast lookup
3. **Worktree Support**: Handle git worktrees properly
4. **Global Fallback**: Use "global" project for non-git directories

### Storage Design

1. **XDG Compliance**: Follow XDG Base Directory Specification
2. **Cache Versioning**: Clear cache on version bumps
3. **File Locking**: Use read/write locks for concurrent access
4. **Migration System**: Support storage schema migrations

### Instance Management

1. **Async Context**: Use AsyncLocalStorage for request-scoped state
2. **Lazy Initialization**: Cache instances per directory
3. **Disposal Hooks**: Support cleanup on instance disposal
4. **Path Containment**: Check if paths are within project boundaries

### Instructions/Skills

1. **Multiple File Names**: Support AGENTS.md, CLAUDE.md for compatibility
2. **Hierarchical Loading**: Load instructions from parent directories
3. **URL Instructions**: Support remote instructions via HTTP
4. **Deduplication**: Track already-loaded instructions per message

### Environment Variables

1. **Truthy Parsing**: Handle "true", "1" as boolean true
2. **Dynamic Getters**: Evaluate flags at access time for runtime changes
3. **Namespaced Flags**: Use consistent `OPENCODE_` prefix
4. **Feature Flags**: Separate stable, experimental, and disable flags
