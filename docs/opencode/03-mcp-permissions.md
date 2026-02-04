# OpenCode MCP & Permissions Analysis

> Comprehensive analysis of OpenCode's MCP protocol implementation, permission system, and snapshot/rollback safety mechanisms.

---

## Table of Contents

1. [MCP Implementation](#mcp-implementation)
   - [Server Discovery and Connection](#server-discovery-and-connection)
   - [Transport Types](#transport-types)
   - [OAuth Authentication](#oauth-authentication)
   - [Tool Integration](#tool-integration)
2. [Permission System](#permission-system)
   - [Architecture Overview](#architecture-overview)
   - [Permission Request Flow](#permission-request-flow)
   - [Rule Configuration](#rule-configuration)
   - [Tool-Specific Permissions](#tool-specific-permissions)
3. [Snapshot System](#snapshot-system)
   - [Git-Based Snapshots](#git-based-snapshots)
   - [Rollback Mechanisms](#rollback-mechanisms)
   - [Session Revert Flow](#session-revert-flow)

---

## MCP Implementation

### Server Discovery and Connection

**Source:** `/packages/opencode/src/mcp/index.ts`

OpenCode discovers MCP servers through configuration. Servers are defined in the user's config file (`opencode.json` or `opencode.jsonc`) under the `mcp` key:

```typescript
// Config structure (from config.ts)
mcp: z.record(
  z.string(),          // Server name/key
  z.union([
    Mcp,               // Full MCP config (local or remote)
    z.object({
      enabled: z.boolean(),  // Simple enable/disable toggle
    }).strict(),
  ]),
)
```

**Connection Process:**

1. **State Initialization:** When the instance starts, `MCP.state()` initializes all configured MCP servers in parallel
2. **Configuration Check:** Each server config is validated via `isMcpConfigured()` to ensure it has a `type` field
3. **Disabled Check:** Servers with `enabled: false` are marked as "disabled" without attempting connection
4. **Connection Attempt:** `create()` function handles the actual connection based on server type
5. **Tool Discovery:** After successful connection, `listTools()` is called to fetch available tools

```typescript
// Simplified connection flow
const state = Instance.state(async () => {
  const cfg = await Config.get()
  const config = cfg.mcp ?? {}
  const clients: Record<string, MCPClient> = {}
  const status: Record<string, Status> = {}

  await Promise.all(
    Object.entries(config).map(async ([key, mcp]) => {
      if (mcp.enabled === false) {
        status[key] = { status: "disabled" }
        return
      }
      const result = await create(key, mcp)
      // ... handle result
    }),
  )
  return { status, clients }
})
```

**Status Types:**

```typescript
export const Status = z.discriminatedUnion("status", [
  z.object({ status: z.literal("connected") }),
  z.object({ status: z.literal("disabled") }),
  z.object({ status: z.literal("failed"), error: z.string() }),
  z.object({ status: z.literal("needs_auth") }),
  z.object({ status: z.literal("needs_client_registration"), error: z.string() }),
])
```

---

### Transport Types

OpenCode supports three MCP transport types:

#### 1. Local (Stdio) Transport

**Configuration:**
```typescript
export const McpLocal = z.object({
  type: z.literal("local"),
  command: z.string().array(),        // Command and arguments to run
  environment: z.record(z.string(), z.string()).optional(),  // Env vars
  enabled: z.boolean().optional(),
  timeout: z.number().int().positive().optional(),  // Default: 30s
})
```

**Implementation:**
```typescript
if (mcp.type === "local") {
  const [cmd, ...args] = mcp.command
  const cwd = Instance.directory
  const transport = new StdioClientTransport({
    stderr: "pipe",
    command: cmd,
    args,
    cwd,
    env: {
      ...process.env,
      // Special handling for opencode self-invocation
      ...(cmd === "opencode" ? { BUN_BE_BUN: "1" } : {}),
      ...mcp.environment,
    },
  })

  // Stderr logging
  transport.stderr?.on("data", (chunk: Buffer) => {
    log.info(`mcp stderr: ${chunk.toString()}`, { key })
  })

  // Connect with timeout
  await withTimeout(client.connect(transport), connectTimeout)
}
```

#### 2. Remote (StreamableHTTP) Transport

**Configuration:**
```typescript
export const McpRemote = z.object({
  type: z.literal("remote"),
  url: z.string(),                    // Server URL
  enabled: z.boolean().optional(),
  headers: z.record(z.string(), z.string()).optional(),  // Custom headers
  oauth: z.union([McpOAuth, z.literal(false)]).optional(),  // OAuth config
  timeout: z.number().int().positive().optional(),
})
```

**Implementation (with SSE fallback):**
```typescript
if (mcp.type === "remote") {
  const transports: Array<{ name: string; transport: TransportWithAuth }> = [
    {
      name: "StreamableHTTP",
      transport: new StreamableHTTPClientTransport(new URL(mcp.url), {
        authProvider,
        requestInit: mcp.headers ? { headers: mcp.headers } : undefined,
      }),
    },
    {
      name: "SSE",
      transport: new SSEClientTransport(new URL(mcp.url), {
        authProvider,
        requestInit: mcp.headers ? { headers: mcp.headers } : undefined,
      }),
    },
  ]

  // Try each transport in order
  for (const { name, transport } of transports) {
    try {
      await withTimeout(client.connect(transport), connectTimeout)
      // Success - break out
      break
    } catch (error) {
      // Handle UnauthorizedError for OAuth
      if (error instanceof UnauthorizedError) {
        // ... OAuth handling
      }
    }
  }
}
```

#### 3. SSE Transport (Fallback)

OpenCode automatically falls back to SSE transport if StreamableHTTP fails, providing broader compatibility with different MCP server implementations.

---

### OAuth Authentication

**Source:** `/packages/opencode/src/mcp/oauth-provider.ts`, `/packages/opencode/src/mcp/oauth-callback.ts`, `/packages/opencode/src/mcp/auth.ts`

#### OAuth Flow Overview

1. **Detection:** OAuth is enabled by default for remote servers unless `oauth: false`
2. **State Generation:** Cryptographically secure state parameter generated before flow
3. **Authorization URL:** Transport generates authorization URL via SDK
4. **Browser Redirect:** User authorizes in browser
5. **Callback Server:** Local server receives authorization code
6. **Token Exchange:** SDK handles code-for-token exchange
7. **Token Storage:** Tokens persisted to `~/.local/share/opencode/mcp-auth.json`

#### OAuth Provider Implementation

```typescript
// oauth-provider.ts
export class McpOAuthProvider implements OAuthClientProvider {
  private mcpName: string
  private serverUrl: string
  private config: McpOAuthConfig
  private callbacks: McpOAuthCallbacks

  // Redirect URL for OAuth callback
  get redirectUrl(): string {
    return `http://127.0.0.1:${OAUTH_CALLBACK_PORT}${OAUTH_CALLBACK_PATH}`
  }

  // Client metadata for dynamic registration
  get clientMetadata(): OAuthClientMetadata {
    return {
      redirect_uris: [this.redirectUrl],
      client_name: "OpenCode",
      client_uri: "https://opencode.ai",
      grant_types: ["authorization_code", "refresh_token"],
      response_types: ["code"],
      token_endpoint_auth_method: this.config.clientSecret
        ? "client_secret_post"
        : "none",
    }
  }

  // Retrieve stored or configured client info
  async clientInformation(): Promise<OAuthClientInformation | undefined> {
    // Check config first (pre-registered client)
    if (this.config.clientId) {
      return {
        client_id: this.config.clientId,
        client_secret: this.config.clientSecret,
      }
    }

    // Check stored client info (from dynamic registration)
    const entry = await McpAuth.getForUrl(this.mcpName, this.serverUrl)
    if (entry?.clientInfo) {
      // Check expiration
      if (entry.clientInfo.clientSecretExpiresAt &&
          entry.clientInfo.clientSecretExpiresAt < Date.now() / 1000) {
        return undefined  // Expired, trigger re-registration
      }
      return {
        client_id: entry.clientInfo.clientId,
        client_secret: entry.clientInfo.clientSecret,
      }
    }

    return undefined  // Will trigger dynamic registration
  }

  // Token management
  async tokens(): Promise<OAuthTokens | undefined>
  async saveTokens(tokens: OAuthTokens): Promise<void>

  // PKCE support
  async saveCodeVerifier(codeVerifier: string): Promise<void>
  async codeVerifier(): Promise<string>

  // State parameter (CSRF protection)
  async saveState(state: string): Promise<void>
  async state(): Promise<string>
}
```

#### OAuth Callback Server

```typescript
// oauth-callback.ts
export namespace McpOAuthCallback {
  const OAUTH_CALLBACK_PORT = 19876
  const OAUTH_CALLBACK_PATH = "/mcp/oauth/callback"
  const CALLBACK_TIMEOUT_MS = 5 * 60 * 1000  // 5 minutes

  export async function ensureRunning(): Promise<void> {
    if (server) return

    server = Bun.serve({
      port: OAUTH_CALLBACK_PORT,
      fetch(req) {
        const url = new URL(req.url)
        if (url.pathname !== OAUTH_CALLBACK_PATH) {
          return new Response("Not found", { status: 404 })
        }

        const code = url.searchParams.get("code")
        const state = url.searchParams.get("state")
        const error = url.searchParams.get("error")

        // Enforce state parameter (CSRF protection)
        if (!state) {
          return new Response(HTML_ERROR("Missing state"), { status: 400 })
        }

        // Validate state against pending auths
        if (!pendingAuths.has(state)) {
          return new Response(HTML_ERROR("Invalid state"), { status: 400 })
        }

        // Resolve pending auth promise
        const pending = pendingAuths.get(state)!
        pending.resolve(code)
        return new Response(HTML_SUCCESS)
      },
    })
  }

  export function waitForCallback(oauthState: string): Promise<string> {
    return new Promise((resolve, reject) => {
      const timeout = setTimeout(() => {
        pendingAuths.delete(oauthState)
        reject(new Error("OAuth callback timeout"))
      }, CALLBACK_TIMEOUT_MS)

      pendingAuths.set(oauthState, { resolve, reject, timeout })
    })
  }
}
```

#### Token Storage

```typescript
// auth.ts
export namespace McpAuth {
  const filepath = path.join(Global.Path.data, "mcp-auth.json")

  export const Entry = z.object({
    tokens: Tokens.optional(),          // Access/refresh tokens
    clientInfo: ClientInfo.optional(),  // Dynamic registration info
    codeVerifier: z.string().optional(), // PKCE verifier
    oauthState: z.string().optional(),   // CSRF state
    serverUrl: z.string().optional(),    // URL validation
  })

  // Tokens are validated against URL to prevent credential reuse
  export async function getForUrl(mcpName: string, serverUrl: string) {
    const entry = await get(mcpName)
    if (!entry?.serverUrl || entry.serverUrl !== serverUrl) {
      return undefined  // Credentials invalid for different URL
    }
    return entry
  }

  // File permissions: 0o600 (owner read/write only)
  export async function set(mcpName: string, entry: Entry, serverUrl?: string) {
    await Bun.write(file, JSON.stringify(data, null, 2), { mode: 0o600 })
  }
}
```

---

### Tool Integration

MCP tools are seamlessly integrated into OpenCode's tool registry:

```typescript
// From prompt.ts - resolveTools()
for (const [key, item] of Object.entries(await MCP.tools())) {
  const execute = item.execute
  if (!execute) continue

  // Wrap execute to add permission checks and plugin hooks
  item.execute = async (args, opts) => {
    const ctx = context(args, opts)

    // Plugin hook: before execution
    await Plugin.trigger("tool.execute.before", { tool: key, ... }, { args })

    // Permission check for MCP tools
    await ctx.ask({
      permission: key,  // MCP tool name as permission
      metadata: {},
      patterns: ["*"],
      always: ["*"],
    })

    // Execute the MCP tool
    const result = await execute(args, opts)

    // Plugin hook: after execution
    await Plugin.trigger("tool.execute.after", { tool: key, ... }, result)

    // Format output (handle text, images, resources)
    // ... output formatting ...

    return { title: "", metadata, output, attachments }
  }

  tools[key] = item
}
```

**Tool Name Formatting:**
```typescript
// Tools are named: {mcpServerName}_{toolName}
const sanitizedClientName = clientName.replace(/[^a-zA-Z0-9_-]/g, "_")
const sanitizedToolName = mcpTool.name.replace(/[^a-zA-Z0-9_-]/g, "_")
result[sanitizedClientName + "_" + sanitizedToolName] = await convertMcpTool(mcpTool, client, timeout)
```

**Dynamic Tool Updates:**
```typescript
// Tool list change notification handler
client.setNotificationHandler(ToolListChangedNotificationSchema, async () => {
  log.info("tools list changed notification received", { server: serverName })
  Bus.publish(ToolsChanged, { server: serverName })
})
```

---

## Permission System

### Architecture Overview

**Source:** `/packages/opencode/src/permission/index.ts`, `/packages/opencode/src/permission/next.ts`

OpenCode has two permission system implementations:

1. **Legacy System (`Permission`):** Simpler, type-based permissions with pattern matching
2. **Next-Gen System (`PermissionNext`):** More granular rules with wildcard patterns

#### Permission Actions

```typescript
export const Action = z.enum(["allow", "deny", "ask"])
```

- **allow:** Auto-approve without user interaction
- **deny:** Auto-reject without user interaction
- **ask:** Prompt user for approval

#### Permission Rule Structure

```typescript
export const Rule = z.object({
  permission: z.string(),  // Permission type (e.g., "bash", "edit", "external_directory")
  pattern: z.string(),     // Wildcard pattern (e.g., "git*", "/home/**")
  action: Action,
})

export type Ruleset = Rule[]
```

---

### Permission Request Flow

#### 1. Tool Initiates Permission Request

```typescript
// Example from bash.ts
await ctx.ask({
  permission: "bash",
  patterns: Array.from(patterns),      // Commands being run
  always: Array.from(always),          // Patterns to remember if "always" selected
  metadata: {},
})
```

#### 2. Context's ask() Function

```typescript
// From prompt.ts - Tool.Context
async ask(req) {
  await PermissionNext.ask({
    ...req,
    sessionID: input.session.id,
    tool: { messageID: input.processor.message.id, callID: options.toolCallId },
    ruleset: PermissionNext.merge(input.agent.permission, input.session.permission ?? []),
  })
}
```

#### 3. Permission Evaluation

```typescript
// From next.ts
export const ask = fn(
  Request.partial({ id: true }).extend({ ruleset: Ruleset }),
  async (input) => {
    const s = await state()

    for (const pattern of request.patterns ?? []) {
      const rule = evaluate(request.permission, pattern, ruleset, s.approved)

      if (rule.action === "deny") {
        throw new DeniedError(ruleset.filter(r => Wildcard.match(request.permission, r.permission)))
      }

      if (rule.action === "ask") {
        // Create pending permission request
        return new Promise<void>((resolve, reject) => {
          s.pending[id] = { info, resolve, reject }
          Bus.publish(Event.Asked, info)
        })
      }

      if (rule.action === "allow") continue
    }
  },
)
```

#### 4. Rule Evaluation with Wildcard Matching

```typescript
export function evaluate(permission: string, pattern: string, ...rulesets: Ruleset[]): Rule {
  const merged = merge(...rulesets)

  // Find last matching rule (later rules override earlier ones)
  const match = merged.findLast(
    (rule) => Wildcard.match(permission, rule.permission) &&
              Wildcard.match(pattern, rule.pattern),
  )

  // Default to "ask" if no matching rule
  return match ?? { action: "ask", permission, pattern: "*" }
}
```

#### 5. User Response Handling

```typescript
export const reply = fn(
  z.object({
    requestID: Identifier.schema("permission"),
    reply: Reply,  // "once" | "always" | "reject"
    message: z.string().optional(),
  }),
  async (input) => {
    const s = await state()
    const existing = s.pending[input.requestID]
    if (!existing) return

    delete s.pending[input.requestID]

    if (input.reply === "reject") {
      existing.reject(input.message
        ? new CorrectedError(input.message)  // Continue with guidance
        : new RejectedError())               // Halt execution

      // Reject all other pending permissions for this session
      for (const [id, pending] of Object.entries(s.pending)) {
        if (pending.info.sessionID === sessionID) {
          delete s.pending[id]
          pending.reject(new RejectedError())
        }
      }
      return
    }

    if (input.reply === "once") {
      existing.resolve()
      return
    }

    if (input.reply === "always") {
      // Store approval patterns for future requests
      for (const pattern of existing.info.always) {
        s.approved.push({
          permission: existing.info.permission,
          pattern,
          action: "allow",
        })
      }

      existing.resolve()

      // Auto-approve other pending permissions that now match
      for (const [id, pending] of Object.entries(s.pending)) {
        if (pending.info.sessionID !== sessionID) continue
        const ok = pending.info.patterns.every(
          (pattern) => evaluate(pending.info.permission, pattern, s.approved).action === "allow",
        )
        if (ok) {
          delete s.pending[id]
          pending.resolve()
        }
      }
    }
  },
)
```

---

### Rule Configuration

#### Config File Format

```typescript
// In opencode.json
{
  "permission": {
    // Simple format: action applies to all patterns
    "read": "allow",
    "edit": "ask",
    "bash": "ask",

    // Detailed format: pattern-specific rules
    "bash": {
      "git*": "allow",           // Auto-approve git commands
      "rm*": "deny",             // Block rm commands
      "*": "ask"                 // Ask for everything else
    },

    // External directory access
    "external_directory": {
      "~/Documents/**": "allow", // Allow access to Documents
      "/tmp/*": "allow",         // Allow temp files
      "*": "ask"                 // Ask for other external paths
    }
  }
}
```

#### Path Expansion

```typescript
// From next.ts
function expand(pattern: string): string {
  if (pattern.startsWith("~/")) return os.homedir() + pattern.slice(1)
  if (pattern === "~") return os.homedir()
  if (pattern.startsWith("$HOME/")) return os.homedir() + pattern.slice(5)
  if (pattern.startsWith("$HOME")) return os.homedir() + pattern.slice(5)
  return pattern
}
```

---

### Tool-Specific Permissions

#### Bash Tool Permissions

**Source:** `/packages/opencode/src/tool/bash.ts`

The bash tool extracts commands using tree-sitter parsing and requests permissions:

```typescript
// Parse command with tree-sitter
const tree = await parser().then((p) => p.parse(params.command))

const directories = new Set<string>()
const patterns = new Set<string>()
const always = new Set<string>()

for (const node of tree.rootNode.descendantsOfType("command")) {
  const command = []
  // ... extract command tokens ...

  // Check for directory access commands
  if (["cd", "rm", "cp", "mv", "mkdir", "touch", "chmod", "chown", "cat"].includes(command[0])) {
    for (const arg of command.slice(1)) {
      if (arg.startsWith("-")) continue
      const resolved = await realpath(arg)
      if (!Instance.containsPath(resolved)) {
        directories.add(resolved)  // External directory
      }
    }
  }

  // Build permission patterns
  if (command[0] !== "cd") {
    patterns.add(command.join(" "))
    always.add(BashArity.prefix(command).join(" ") + "*")
  }
}

// Request external_directory permission if needed
if (directories.size > 0) {
  await ctx.ask({
    permission: "external_directory",
    patterns: Array.from(directories),
    always: Array.from(directories).map((x) => path.dirname(x) + "*"),
    metadata: {},
  })
}

// Request bash permission
if (patterns.size > 0) {
  await ctx.ask({
    permission: "bash",
    patterns: Array.from(patterns),
    always: Array.from(always),
    metadata: {},
  })
}
```

#### Bash Arity for Command Prefixes

**Source:** `/packages/opencode/src/permission/arity.ts`

This module helps identify the "human-understandable command" from shell input:

```typescript
// Examples:
// "npm install" -> arity 2 -> ["npm", "install"]
// "npm run dev" -> arity 3 -> ["npm", "run", "dev"]
// "git checkout main" -> arity 2 -> ["git", "checkout"]
// "git remote add origin" -> arity 3 -> ["git", "remote", "add"]

const ARITY: Record<string, number> = {
  cat: 1,
  git: 2,
  "git config": 3,
  "git remote": 3,
  npm: 2,
  "npm run": 3,
  docker: 2,
  "docker compose": 3,
  kubectl: 2,
  // ... many more
}

export function prefix(tokens: string[]) {
  for (let len = tokens.length; len > 0; len--) {
    const prefix = tokens.slice(0, len).join(" ")
    const arity = ARITY[prefix]
    if (arity !== undefined) return tokens.slice(0, arity)
  }
  if (tokens.length === 0) return []
  return tokens.slice(0, 1)
}
```

#### Edit Tool Permissions

**Source:** `/packages/opencode/src/tool/edit.ts`

```typescript
await ctx.ask({
  permission: "edit",
  patterns: [path.relative(Instance.worktree, filePath)],
  always: ["*"],  // "Always" approves all edits
  metadata: {
    filepath: filePath,
    diff,  // Unified diff for review
  },
})
```

#### MCP Tool Permissions

All MCP tools require permission with the tool name as the permission type:

```typescript
await ctx.ask({
  permission: key,  // e.g., "context7_query-docs"
  metadata: {},
  patterns: ["*"],
  always: ["*"],
})
```

---

## Snapshot System

### Git-Based Snapshots

**Source:** `/packages/opencode/src/snapshot/index.ts`

OpenCode uses a separate git repository for tracking file changes during agent execution. This provides a safety net for rolling back unwanted changes.

#### Snapshot Directory

```typescript
function gitdir() {
  const project = Instance.project
  return path.join(Global.Path.data, "snapshot", project.id)
}
// Example: ~/.local/share/opencode/snapshot/proj_abc123
```

#### Initialization

```typescript
export async function track() {
  if (Instance.project.vcs !== "git") return
  const cfg = await Config.get()
  if (cfg.snapshot === false) return  // Can be disabled in config

  const git = gitdir()
  if (await fs.mkdir(git, { recursive: true })) {
    // Initialize new snapshot repo
    await $`git init`.env({ GIT_DIR: git, GIT_WORK_TREE: Instance.worktree })
    await $`git --git-dir ${git} config core.autocrlf false`
  }

  // Stage all files
  await $`git --git-dir ${git} --work-tree ${Instance.worktree} add .`

  // Write tree object (returns hash)
  const hash = await $`git --git-dir ${git} --work-tree ${Instance.worktree} write-tree`.text()

  return hash.trim()
}
```

#### Patch Tracking

```typescript
export const Patch = z.object({
  hash: z.string(),
  files: z.string().array(),
})

export async function patch(hash: string): Promise<Patch> {
  const git = gitdir()

  // Stage current state
  await $`git --git-dir ${git} --work-tree ${Instance.worktree} add .`

  // Get list of changed files since hash
  const result = await $`git --git-dir ${git} --work-tree ${Instance.worktree} diff --name-only ${hash} -- .`

  const files = result.text()
    .trim()
    .split("\n")
    .filter(Boolean)
    .map((x) => path.join(Instance.worktree, x))

  return { hash, files }
}
```

#### Full Diff Retrieval

```typescript
export async function diffFull(from: string, to: string): Promise<FileDiff[]> {
  const git = gitdir()
  const result: FileDiff[] = []

  // Get numstat (additions, deletions, filename)
  for await (const line of $`git --git-dir ${git} diff --numstat ${from} ${to} -- .`.lines()) {
    const [additions, deletions, file] = line.split("\t")
    const isBinaryFile = additions === "-" && deletions === "-"

    // Get before/after content
    const before = isBinaryFile ? "" : await $`git show ${from}:${file}`.text()
    const after = isBinaryFile ? "" : await $`git show ${to}:${file}`.text()

    result.push({
      file,
      before,
      after,
      additions: isBinaryFile ? 0 : parseInt(additions),
      deletions: isBinaryFile ? 0 : parseInt(deletions),
    })
  }

  return result
}
```

---

### Rollback Mechanisms

#### Restore to Snapshot

```typescript
export async function restore(snapshot: string) {
  log.info("restore", { commit: snapshot })
  const git = gitdir()

  // Read tree and checkout all files
  await $`git --git-dir ${git} --work-tree ${Instance.worktree} read-tree ${snapshot} &&
          git --git-dir ${git} --work-tree ${Instance.worktree} checkout-index -a -f`
}
```

#### Selective Revert

```typescript
export async function revert(patches: Patch[]) {
  const files = new Set<string>()
  const git = gitdir()

  for (const item of patches) {
    for (const file of item.files) {
      if (files.has(file)) continue  // Skip already reverted

      // Try to checkout file from snapshot
      const result = await $`git --git-dir ${git} checkout ${item.hash} -- ${file}`.nothrow()

      if (result.exitCode !== 0) {
        // Check if file existed in snapshot
        const checkTree = await $`git ls-tree ${item.hash} -- ${relativePath}`.nothrow()

        if (checkTree.exitCode === 0 && checkTree.text().trim()) {
          // File existed but checkout failed - keep current version
          log.info("file existed in snapshot but checkout failed, keeping", { file })
        } else {
          // File didn't exist in snapshot - delete it
          log.info("file did not exist in snapshot, deleting", { file })
          await fs.unlink(file).catch(() => {})
        }
      }

      files.add(file)
    }
  }
}
```

#### Cleanup Scheduling

```typescript
export function init() {
  Scheduler.register({
    id: "snapshot.cleanup",
    interval: hour,  // Every hour
    run: cleanup,
    scope: "instance",
  })
}

export async function cleanup() {
  if (Instance.project.vcs !== "git") return
  const cfg = await Config.get()
  if (cfg.snapshot === false) return

  // Prune objects older than 7 days
  await $`git --git-dir ${git} gc --prune=7.days`
}
```

---

### Session Revert Flow

**Source:** `/packages/opencode/src/session/revert.ts`

The session revert flow allows users to undo changes made during a conversation:

#### Revert Process

```typescript
export async function revert(input: RevertInput) {
  SessionPrompt.assertNotBusy(input.sessionID)
  const all = await Session.messages({ sessionID: input.sessionID })
  const session = await Session.get(input.sessionID)

  let revert: Session.Info["revert"]
  const patches: Snapshot.Patch[] = []

  // Find revert point and collect patches
  for (const msg of all) {
    for (const part of msg.parts) {
      if (revert) {
        // Collect patches after revert point
        if (part.type === "patch") {
          patches.push(part)
        }
        continue
      }

      // Find the revert point
      if ((msg.info.id === input.messageID && !input.partID) || part.id === input.partID) {
        revert = {
          messageID: msg.info.id,
          partID: input.partID,
        }
      }
    }
  }

  if (revert) {
    // Take snapshot of current state before reverting
    revert.snapshot = session.revert?.snapshot ?? (await Snapshot.track())

    // Revert files to their state before the patches
    await Snapshot.revert(patches)

    // Generate diff for display
    if (revert.snapshot) {
      revert.diff = await Snapshot.diff(revert.snapshot)
    }

    // Store revert info in session
    return Session.update(input.sessionID, (draft) => {
      draft.revert = revert
    })
  }

  return session
}
```

#### Unrevert (Restore Forward)

```typescript
export async function unrevert(input: { sessionID: string }) {
  SessionPrompt.assertNotBusy(input.sessionID)
  const session = await Session.get(input.sessionID)

  if (!session.revert) return session

  // Restore to the snapshot taken before revert
  if (session.revert.snapshot) {
    await Snapshot.restore(session.revert.snapshot)
  }

  // Clear revert state
  return Session.update(input.sessionID, (draft) => {
    draft.revert = undefined
  })
}
```

#### Cleanup on Continue

```typescript
export async function cleanup(session: Session.Info) {
  if (!session.revert) return

  const sessionID = session.id
  const msgs = await Session.messages({ sessionID })
  const messageID = session.revert.messageID

  // Split messages: preserve before revert point, remove after
  const [preserve, remove] = splitWhen(msgs, (x) => x.info.id === messageID)

  // Delete removed messages
  for (const msg of remove) {
    await Storage.remove(["message", sessionID, msg.info.id])
    await Bus.publish(MessageV2.Event.Removed, { sessionID, messageID: msg.info.id })
  }

  // Handle partial message revert
  if (session.revert.partID && last) {
    const [preserveParts, removeParts] = splitWhen(last.parts, (x) => x.id === partID)
    for (const part of removeParts) {
      await Storage.remove(["part", last.info.id, part.id])
      await Bus.publish(MessageV2.Event.PartRemoved, { sessionID, messageID: last.info.id, partID: part.id })
    }
  }

  // Clear revert state
  await Session.update(sessionID, (draft) => {
    draft.revert = undefined
  })
}
```

---

## Integration with Session Processing

**Source:** `/packages/opencode/src/session/processor.ts`

The processor integrates snapshots into the agent loop:

```typescript
export function create(input: { assistantMessage, sessionID, model, abort }) {
  let snapshot: string | undefined

  return {
    async process(streamInput: LLM.StreamInput) {
      for await (const value of stream.fullStream) {
        switch (value.type) {
          case "start-step":
            // Take snapshot at start of each step
            snapshot = await Snapshot.track()
            await Session.updatePart({
              type: "step-start",
              snapshot,
              // ...
            })
            break

          case "finish-step":
            // Record patch at end of step
            await Session.updatePart({
              type: "step-finish",
              snapshot: await Snapshot.track(),
              // ...
            })

            if (snapshot) {
              const patch = await Snapshot.patch(snapshot)
              if (patch.files.length) {
                await Session.updatePart({
                  type: "patch",
                  hash: patch.hash,
                  files: patch.files,
                })
              }
              snapshot = undefined
            }
            break

          case "tool-error":
            // Handle permission rejection
            if (value.error instanceof PermissionNext.RejectedError) {
              blocked = shouldBreak
            }
            break
        }
      }
    },
  }
}
```

---

## Summary

### Key Takeaways

1. **MCP Implementation:**
   - Supports local (stdio) and remote (StreamableHTTP/SSE) transports
   - Full OAuth support with PKCE, dynamic client registration, and secure token storage
   - Tools integrated seamlessly into the tool registry with automatic permission checks

2. **Permission System:**
   - Rule-based with wildcard pattern matching
   - Three actions: allow, deny, ask
   - Session-scoped approvals with "always" option for persistent approval
   - Tool-specific permission types (bash, edit, external_directory, etc.)
   - Bash tool uses tree-sitter parsing for command extraction

3. **Snapshot System:**
   - Separate git repository per project for change tracking
   - Tree-object based snapshots (not commits) for efficiency
   - Selective revert capability at file level
   - Automatic cleanup of old snapshots (7 day retention)
   - Integration with session revert flow for user-initiated rollback

4. **Safety Features:**
   - External directory access requires explicit permission
   - File modification tracking with full diff history
   - Non-destructive revert/unrevert operations
   - Permission rejection cascades to halt execution
