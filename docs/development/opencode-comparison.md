# Estela vs OpenCode: Practices Comparison

> Detailed comparison of patterns and practices between Estela and OpenCode

---

## Summary

| Metric | Estela | OpenCode |
|--------|--------|----------|
| Core LOC | ~13,750 | ~10,000 |
| Tool System | `defineTool()` with Zod | `Info<P,M>` interface + dynamic init |
| Agent Loop | Class-based `AgentExecutor` | Config-driven agents |
| Permissions | Class-based manager with events | Event bus with async ask/reply |
| MCP | Client with discovery | OAuth flow + StreamableHTTP/SSE/Stdio |
| Snapshots | Git utils | Separate git dir per project |

---

## Feature Comparison

### 1. Tool Definition System

| Aspect | Estela (Current) | OpenCode (Reference) | Should Adopt? |
|--------|------------------|---------------------|---------------|
| Schema | Zod with `defineTool()` | Zod with `Info<P,M>` interface | ✅ Similar |
| Init Pattern | Static config | Dynamic `init(ctx)` returns tools | **Yes** - lazy loading |
| Metadata Streaming | Not implemented | `ctx.metadata()` for live updates | **Yes** - critical for UX |
| Output Truncation | Basic | Multi-level (30KB metadata, 50KB files) | **Yes** |
| Validation Errors | `formatZodError()` | Custom `formatValidationError` per tool | Nice to have |
| Attachments | Not supported | `FilePart[]` attachments | **Yes** |

**OpenCode Pattern:**
```typescript
export interface Info<Parameters, Metadata> {
  id: string
  init: (ctx?: InitContext) => Promise<{
    description: string
    parameters: ZodSchema
    execute: (args, ctx) => Promise<{
      title: string
      metadata: Metadata        // Progressive updates
      output: string
      attachments?: FilePart[]  // Binary content
    }>
  }>
}
```

**Recommendation:**
- Add `ctx.metadata()` for streaming tool output (bash, long operations)
- Add attachment support for binary content (images, PDFs)
- Implement output truncation with configurable limits

---

### 2. Permission System

| Aspect | Estela (Current) | OpenCode (Reference) | Should Adopt? |
|--------|------------------|---------------------|---------------|
| Architecture | `PermissionManager` class | Function-based with Bus events | Similar |
| Rule Matching | Glob patterns (good) | Glob patterns (same) | ✅ Same |
| Actions | allow/deny/ask | allow/deny/ask | ✅ Same |
| Async Flow | Events via `emit()` | Event bus `Bus.publish()` | Similar |
| Storage | Export/import rules | Persistent + session | ✅ Same |
| Error Types | Single type | `RejectedError`, `CorrectedError`, `DeniedError` | **Yes** |
| User Correction | Not supported | Rejection with feedback message | **Yes** |

**OpenCode Pattern:**
```typescript
// User can reject with correction
class CorrectedError extends Error {
  constructor(public correction: string) {}
}

// Permission ask returns Promise that resolves on user reply
const ask = async (input) => {
  return new Promise((resolve, reject) => {
    s.pending[id] = { resolve, reject }
    Bus.publish(Event.Asked, info)  // UI picks up
  })
}
```

**Recommendation:**
- Add `CorrectedError` for rejections with feedback
- Ensure permission UI can capture user corrections

---

### 3. Agent System

| Aspect | Estela (Current) | OpenCode (Reference) | Should Adopt? |
|--------|------------------|---------------------|---------------|
| Architecture | `AgentExecutor` class | Config-driven data structures | **Consider** |
| Built-in Agents | Commander workers | build, plan, general, explore, compaction, title, summary | Expand |
| Custom Agents | Via Commander | User-defined via config | **Yes** |
| Temperature | Global config | Per-agent tuning | **Yes** |
| Agent Modes | Worker types | `"subagent"`, `"primary"`, `"all"` | Nice to have |
| Agent Generation | Not supported | LLM-powered `Agent.generate()` | **Yes** |

**OpenCode Pattern:**
```typescript
// Agents as pure config, not classes
const Agent = {
  build: {
    mode: "primary",
    temperature: 0.3,  // Deterministic
    permissions: PermissionNext.merge(defaults, agentSpecific),
    prompt: loadPrompt("build.txt")
  }
}
```

**Recommendation:**
- Add per-agent temperature/topP configuration
- Consider LLM-powered agent generation for custom agents

---

### 4. Session Management

| Aspect | Estela (Current) | OpenCode (Reference) | Should Adopt? |
|--------|------------------|---------------------|---------------|
| State | `SessionManager` with events | Storage keys + Bus events | ✅ Similar |
| Forking | Not supported | Full fork with message mapping | **Yes** |
| Sharing | Not implemented | Auto-share to endpoint | Nice to have |
| Compaction | Via ContextManager | Async message merging | ✅ Similar |
| Revert | Git snapshots | Snapshot hash + diff stored | ✅ Similar |
| Usage Tracking | Token counting | Detailed with cache metrics | **Enhance** |

**OpenCode Pattern:**
```typescript
// Session forking with ID mapping
async fork(sessionID, upToMessageID) {
  const idMap = new Map<string, string>()  // old → new
  for (const msg of msgs) {
    const newID = Identifier.ascending("message")
    idMap.set(msg.info.id, newID)
    // Clone with new ID, preserve parentID via mapping
  }
}
```

**Recommendation:**
- Implement session forking (branch conversations)
- Add cache hit metrics to usage tracking

---

### 5. MCP Integration

| Aspect | Estela (Current) | OpenCode (Reference) | Should Adopt? |
|--------|------------------|---------------------|---------------|
| Client | Basic MCP client | StreamableHTTP + SSE + Stdio | **Enhance** |
| Discovery | Config-based | Dynamic discovery + plugins | **Yes** |
| OAuth | Not supported | Full OAuth flow with browser redirect | **Yes** |
| Status Tracking | Connected/error | connected/disabled/failed/needs_auth/needs_client_registration | **Yes** |
| Timeouts | Basic | Per-server configurable | **Yes** |
| Tool Conversion | Basic mapping | `convertMcpTool()` with schema transform | ✅ Similar |

**OpenCode Pattern:**
```typescript
// Multiple transport fallback
const transports = [
  new StreamableHTTPClientTransport(url, { authProvider }),
  new SSEClientTransport(url, { authProvider }),  // Fallback
]

// OAuth flow
await MCP.startAuth(serverName)
await MCP.authenticate(serverName)  // Opens browser
await MCP.finishAuth(authCode)
```

**Recommendation:**
- Add OAuth support for remote MCP servers
- Implement granular status tracking
- Add per-server timeout configuration

---

### 6. Shell Execution

| Aspect | Estela (Current) | OpenCode (Reference) | Should Adopt? |
|--------|------------------|---------------------|---------------|
| Process Groups | `killProcessGroup()` | Tree kill with SIGTERM → SIGKILL | ✅ Same |
| Timeout | 2 min default | 2 min default + configurable | ✅ Same |
| Shell Selection | Platform detection | `SHELL` env + Git Bash support | **Enhance** |
| Command Parsing | Regex patterns | Tree-sitter AST parsing | **Yes** |
| PTY Support | Via platform | Dedicated PTY module with WebSocket | **Yes** |
| Output Streaming | Via callback | Ring buffer (2MB) + metadata | **Yes** |

**OpenCode Pattern:**
```typescript
// Tree-sitter command parsing for permission
import Parser from "tree-sitter"
import Bash from "tree-sitter-bash"

function parseCommands(command: string) {
  const parser = new Parser()
  parser.setLanguage(Bash)
  const tree = parser.parse(command)
  // Extract directories, commands for permission check
}

// PTY with WebSocket streaming
interface ActiveSession {
  info: Info
  process: IPty
  buffer: string  // 2MB ring buffer
  subscribers: Set<WSContext>
}
```

**Recommendation:**
- Add tree-sitter for bash command parsing (better permission detection)
- Consider PTY module for interactive sessions
- Implement ring buffer for disconnected clients

---

### 7. Snapshot System

| Aspect | Estela (Current) | OpenCode (Reference) | Should Adopt? |
|--------|------------------|---------------------|---------------|
| Storage | Project git repo | Separate git dir per project | **Consider** |
| Tracking | `git add` + commit | `git write-tree` (no commits) | **Consider** |
| Diff Format | Unified diff | `git diff --numstat` + content | ✅ Similar |
| Cleanup | Not implemented | `git gc --prune=7.days` hourly | **Yes** |
| Disable Option | Not supported | `cfg.snapshot === false` | Nice to have |

**OpenCode Pattern:**
```typescript
// Separate snapshot directory
const git = path.join(Global.Path.data, "snapshot", project.id)

// Use tree objects instead of commits
await exec("git add .")
const hash = await exec("git write-tree")  // Tree hash, not commit
```

**Recommendation:**
- Add snapshot cleanup (gc) to prevent repo bloat
- Consider separate snapshot directory to avoid polluting project git

---

### 8. Configuration System

| Aspect | Estela (Current) | OpenCode (Reference) | Should Adopt? |
|--------|------------------|---------------------|---------------|
| Layers | File + defaults | 6 layers (remote → managed) | **Consider** |
| Format | JSON | JSONC (comments supported) | **Yes** |
| Discovery | Single path | Walk up directory tree | **Yes** |
| Plugins | Not supported | Dynamic plugin discovery | Future |
| Migration | Basic | Legacy config auto-migration | ✅ Have |

**OpenCode Pattern:**
```
Config Layer Priority (lowest → highest):
1. Remote/.well-known
2. Global user (~/.opencode/opencode.jsonc)
3. Custom path (env var)
4. Project configs (walk up tree)
5. Inline (env var)
6. Managed /etc/opencode/ (enterprise)
```

**Recommendation:**
- Support JSONC for config files (allows comments)
- Implement directory walk-up for project config discovery

---

### 9. Read Tool Enhancements

| Aspect | Estela (Current) | OpenCode (Reference) | Should Adopt? |
|--------|------------------|---------------------|---------------|
| Line Limits | Basic offset/limit | 2000 lines default | ✅ Same |
| Binary Detection | Not implemented | Null bytes + printable % check | **Yes** |
| Image Handling | Via platform | Base64 embedding | ✅ Same |
| PDF Handling | Via platform | Base64 embedding | ✅ Same |
| Typo Suggestions | Not implemented | "Did you mean?" suggestions | **Yes** |
| Instruction Injection | Not implemented | Load `.opencode/instructions.md` | **Yes** |

**OpenCode Pattern:**
```typescript
// Typo suggestions
const suggestions = await findSimilarFiles(requestedPath)
if (suggestions.length > 0) {
  output += `\n\nDid you mean:\n${suggestions.join('\n')}`
}

// Instruction injection
const instructions = await loadInstructions(directory)
if (instructions.length > 0) {
  output += `\n\n<system-reminder>\n${instructions}\n</system-reminder>`
}
```

**Recommendation:**
- Add binary file detection
- Add typo suggestions for file not found
- Implement instruction file injection (`.estela/instructions.md`)

---

### 10. Edit Tool Enhancements

| Aspect | Estela (Current) | OpenCode (Reference) | Should Adopt? |
|--------|------------------|---------------------|---------------|
| Replace Mode | `replaceAll` option | `replaceAll` flag | ✅ Same |
| Line Endings | Not normalized | CRLF → LF normalization | **Yes** |
| LSP Diagnostics | Not integrated | Reports errors after edit | **Yes** |
| File Watcher | Not integrated | Emits events on edit | **Yes** |
| Snapshot Integration | Via DiffTracker | Direct snapshot calls | ✅ Same |

**OpenCode Pattern:**
```typescript
// After edit, check for LSP errors
const diagnostics = await LSP.diagnostics()
const errors = diagnostics[file].filter(d => d.severity === 1)
if (errors.length > 0) {
  output += "\n\nLSP errors detected:\n" + formatDiagnostics(errors)
}

// Emit file change events
await Bus.publish(File.Event.Edited, { file })
await Bus.publish(FileWatcher.Event.Updated, { file, event: "change" })
```

**Recommendation:**
- Add line ending normalization (CRLF → LF)
- Integrate LSP diagnostics after edits
- Emit file watcher events for UI updates

---

## Priority Matrix

### Must Implement (High Impact)

| Feature | Why | Effort |
|---------|-----|--------|
| Metadata streaming | Live bash output, better UX | Medium |
| Output truncation | Prevent context overflow | Low |
| Binary file detection | Avoid corrupted output | Low |
| LSP diagnostics on edit | Immediate error feedback | Medium |
| CorrectedError | Better permission UX | Low |

### Should Implement (Medium Impact)

| Feature | Why | Effort |
|---------|-----|--------|
| Session forking | Branch conversations | Medium |
| Tree-sitter bash parsing | Better permission detection | Medium |
| Instruction file injection | Project-specific context | Low |
| Typo suggestions | Better error messages | Low |
| Snapshot cleanup | Prevent disk bloat | Low |

### Nice to Have (Lower Priority)

| Feature | Why | Effort |
|---------|-----|--------|
| MCP OAuth | Remote authenticated servers | High |
| PTY module | Interactive sessions | High |
| JSONC config | Developer comments | Low |
| Config directory walk | Monorepo support | Low |
| Agent generation | Custom agents via LLM | Medium |

---

## Action Items

### Immediate (Next Sprint)

1. **Add `ctx.metadata()` to tool context** - Enable progressive output streaming
2. **Implement output truncation** - 30KB for metadata, 50KB for file content
3. **Add binary file detection** - Check null bytes before reading
4. **Add `CorrectedError`** - Permission rejection with user feedback

### Short-term (1-2 Sprints)

5. **Integrate LSP diagnostics** - Report errors after edit/write
6. **Add instruction injection** - Load `.estela/instructions.md`
7. **Implement session forking** - Branch from any message
8. **Add snapshot cleanup** - `git gc` on schedule

### Long-term (3+ Sprints)

9. **Tree-sitter bash parsing** - Better command analysis
10. **MCP OAuth flow** - Remote authenticated servers
11. **PTY module** - Full interactive terminal support

---

## Code Quality Comparison

| Aspect | Estela | OpenCode |
|--------|--------|----------|
| TypeScript Strict | Yes | Yes |
| Zod Validation | Everywhere | Everywhere |
| Error Types | Basic | Rich hierarchy |
| Event System | Class-based listeners | Global Bus |
| Async Patterns | Async/await | Async/await + lazy |
| Module Organization | Feature folders | Feature folders |
| Testing | Vitest | Not visible in ref |

Both codebases follow similar quality standards. OpenCode uses a global event bus pattern while Estela uses class-based event emitters - both are valid approaches.

---

*Generated: 2026-02-03*
