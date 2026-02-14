# Epic 7: Platform Features

> PTY allocation, MCP integration

---

## Goal

Add advanced platform features: PTY for interactive commands, and MCP (Model Context Protocol) for extensibility via external tools.

---

## Reference Implementations

| Feature | Source | Stars |
|---------|--------|-------|
| PTY allocation | Gemini CLI | 50k+ |
| node-pty integration | OpenCode | 70k+ |
| MCP client | Claude Code | - |
| MCP tool bridge | Cursor | - |

---

## Sprints

| # | Sprint | Tasks | Est. Lines |
|---|--------|-------|------------|
| 7.1 | PTY Foundation | Interface, Node implementation | ~400 |
| 7.2 | PTY Integration | Tool wrapper, interactive mode detection | ~200 |
| 7.3 | MCP Client | Protocol implementation, server discovery | ~500 |
| 7.4 | MCP Tool Bridge | Expose MCP tools to LLM | ~300 |

**Total:** ~1400 lines

---

## Sprint 7.1: PTY Foundation

### Files to Create

```
packages/core/src/platform.ts        # Add IPTY interface
packages/platform-node/src/pty.ts    # Node-pty implementation
```

### Dependencies

```bash
pnpm add node-pty -w --filter @ava/platform-node
```

### Interface

```typescript
// packages/core/src/platform.ts

export interface PTYOptions {
  cols?: number
  rows?: number
  cwd?: string
  env?: Record<string, string>
}

export interface PTYProcess {
  pid: number

  // Unified data stream (stdout + stderr combined, like real terminal)
  onData: (callback: (data: string) => void) => void

  // Write to stdin
  write(data: string): void

  // Resize terminal
  resize(cols: number, rows: number): void

  // Kill process
  kill(signal?: string): void

  // Wait for exit
  wait(): Promise<{ exitCode: number }>
}

export interface IPTY {
  spawn(command: string, args: string[], options?: PTYOptions): PTYProcess
  isSupported(): boolean
}
```

### Node Implementation

```typescript
// packages/platform-node/src/pty.ts
import * as pty from 'node-pty'
import type { IPTY, PTYProcess, PTYOptions } from '@ava/core'

export class NodePTY implements IPTY {
  isSupported(): boolean {
    try {
      require.resolve('node-pty')
      return true
    } catch {
      return false
    }
  }

  spawn(command: string, args: string[], options?: PTYOptions): PTYProcess {
    const shell = process.platform === 'win32' ? 'powershell.exe' : 'bash'

    const ptyProcess = pty.spawn(shell, ['-c', `${command} ${args.join(' ')}`], {
      name: 'xterm-256color',
      cols: options?.cols ?? 80,
      rows: options?.rows ?? 24,
      cwd: options?.cwd ?? process.cwd(),
      env: options?.env ? { ...process.env, ...options.env } : process.env,
    })

    return {
      pid: ptyProcess.pid,

      onData(callback) {
        ptyProcess.onData(callback)
      },

      write(data) {
        ptyProcess.write(data)
      },

      resize(cols, rows) {
        ptyProcess.resize(cols, rows)
      },

      kill(signal = 'SIGTERM') {
        ptyProcess.kill(signal)
      },

      wait() {
        return new Promise((resolve) => {
          ptyProcess.onExit(({ exitCode }) => {
            resolve({ exitCode })
          })
        })
      },
    }
  }
}
```

---

## Sprint 7.2: PTY Integration

### Interactive Command Detection

```typescript
// packages/core/src/tools/bash.ts

const INTERACTIVE_COMMANDS = new Set([
  'ssh', 'vim', 'nvim', 'nano', 'less', 'more', 'top', 'htop',
  'python', 'node', 'irb', 'psql', 'mysql', 'redis-cli',
  'git rebase -i', 'git add -i', 'git commit --amend',
])

function isInteractive(command: string): boolean {
  const cmd = command.trim().split(/\s+/)[0]
  return INTERACTIVE_COMMANDS.has(cmd) ||
         command.includes(' -i') ||  // Interactive flags
         command.includes('--interactive')
}

export async function execute(input: BashInput, ctx: ToolContext): Promise<ToolResult> {
  const platform = getPlatform()

  if (isInteractive(input.command) && platform.pty?.isSupported()) {
    return executeWithPTY(input, ctx)
  }

  return executeWithShell(input, ctx)
}

async function executeWithPTY(input: BashInput, ctx: ToolContext): Promise<ToolResult> {
  const pty = getPlatform().pty!

  const proc = pty.spawn('bash', ['-c', input.command], {
    cwd: input.workdir ?? ctx.workingDirectory,
    cols: 120,
    rows: 40,
  })

  let output = ''
  proc.onData((data) => {
    output += data
    // Could stream to UI here
  })

  // Handle abort
  ctx.signal?.addEventListener('abort', () => proc.kill())

  const { exitCode } = await proc.wait()

  return {
    success: exitCode === 0,
    output: formatOutput(output, exitCode),
  }
}
```

---

## Sprint 7.3: MCP Client

### Files to Create

```
packages/core/src/mcp/
├── types.ts          # MCP protocol types
├── client.ts         # MCP client implementation
├── discovery.ts      # Find installed MCP servers
├── transport.ts      # stdio/SSE transport
└── index.ts
```

### MCP Protocol Types

```typescript
// types.ts
export interface MCPServer {
  name: string
  version: string
  transport: 'stdio' | 'sse'
  command?: string      // For stdio
  args?: string[]
  url?: string          // For SSE
}

export interface MCPTool {
  name: string
  description: string
  inputSchema: JSONSchema
}

export interface MCPResource {
  uri: string
  name: string
  mimeType?: string
}

export interface MCPRequest {
  jsonrpc: '2.0'
  id: number
  method: string
  params?: unknown
}

export interface MCPResponse {
  jsonrpc: '2.0'
  id: number
  result?: unknown
  error?: { code: number; message: string }
}
```

### MCP Client

```typescript
// client.ts
export class MCPClient {
  private servers = new Map<string, MCPConnection>()
  private tools = new Map<string, { server: string; tool: MCPTool }>()

  async connect(server: MCPServer): Promise<void> {
    const transport = server.transport === 'stdio'
      ? new StdioTransport(server.command!, server.args)
      : new SSETransport(server.url!)

    const connection = new MCPConnection(server.name, transport)
    await connection.initialize()

    this.servers.set(server.name, connection)

    // Discover tools
    const tools = await connection.listTools()
    for (const tool of tools) {
      this.tools.set(`${server.name}:${tool.name}`, { server: server.name, tool })
    }
  }

  async callTool(name: string, input: unknown): Promise<unknown> {
    const entry = this.tools.get(name)
    if (!entry) throw new Error(`Unknown MCP tool: ${name}`)

    const connection = this.servers.get(entry.server)!
    return connection.callTool(entry.tool.name, input)
  }

  getTools(): MCPTool[] {
    return [...this.tools.values()].map(e => e.tool)
  }

  async disconnect(serverName: string): Promise<void> {
    const connection = this.servers.get(serverName)
    if (connection) {
      await connection.close()
      this.servers.delete(serverName)

      // Remove tools from this server
      for (const [name, entry] of this.tools) {
        if (entry.server === serverName) {
          this.tools.delete(name)
        }
      }
    }
  }
}
```

### Server Discovery

```typescript
// discovery.ts
import { readFile } from 'fs/promises'
import { join } from 'path'
import { homedir } from 'os'

export async function discoverServers(): Promise<MCPServer[]> {
  const servers: MCPServer[] = []

  // Check Claude Code config
  const claudeConfig = join(homedir(), '.claude', 'mcp.json')
  try {
    const config = JSON.parse(await readFile(claudeConfig, 'utf-8'))
    if (config.servers) {
      servers.push(...config.servers)
    }
  } catch {}

  // Check AVA config
  const estelaConfig = join(homedir(), '.estela', 'mcp.json')
  try {
    const config = JSON.parse(await readFile(estelaConfig, 'utf-8'))
    if (config.servers) {
      servers.push(...config.servers)
    }
  } catch {}

  return servers
}
```

---

## Sprint 7.4: MCP Tool Bridge

### Bridge MCP Tools to LLM

```typescript
// packages/core/src/mcp/bridge.ts
import { MCPClient } from './client.js'
import type { Tool, ToolDefinition } from '../tools/types.js'

export function createMCPToolBridge(client: MCPClient): Tool[] {
  return client.getTools().map(mcpTool => ({
    definition: {
      name: `mcp_${mcpTool.name}`,
      description: `[MCP] ${mcpTool.description}`,
      input_schema: mcpTool.inputSchema,
    },
    execute: async (input, ctx) => {
      try {
        const result = await client.callTool(mcpTool.name, input)
        return {
          success: true,
          output: typeof result === 'string' ? result : JSON.stringify(result, null, 2),
        }
      } catch (err) {
        return {
          success: false,
          output: err instanceof Error ? err.message : 'MCP tool failed',
        }
      }
    },
  }))
}

// Integration with tool registry
export async function registerMCPTools(client: MCPClient): Promise<void> {
  const tools = createMCPToolBridge(client)
  for (const tool of tools) {
    registerTool(tool)
  }
}
```

---

## Directory Ownership

This epic owns:
- `packages/core/src/mcp/` (new)
- `packages/platform-node/src/pty.ts` (new)
- Modify `packages/core/src/platform.ts` (add IPTY)

---

## Dependencies

- Epic 3 complete (ACP + Core)
- Epic 4.1 (Permission System) - MCP tools may need permissions
- Epic 6.1 (Tool.define) - cleaner MCP tool registration

---

## Acceptance Criteria

- [ ] PTY works for interactive commands (ssh, vim)
- [ ] PTY falls back to regular shell when not supported
- [ ] MCP client connects to stdio and SSE servers
- [ ] MCP server discovery finds Claude Code and AVA configs
- [ ] MCP tools appear in LLM tool list with `mcp_` prefix
- [ ] MCP tool calls work through the tool execution loop
