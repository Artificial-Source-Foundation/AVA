# OpenCode vs AVA Feature Comparison

> Comprehensive comparison based on thorough OpenCode codebase analysis

---

## Executive Summary

After analyzing ~178KB of documentation covering every aspect of OpenCode's codebase, this document identifies:
- **Critical missing features** in AVA
- **AVA advantages** over OpenCode
- **Architecture differences** worth considering
- **Recommended action items** prioritized by impact

---

## Tools Comparison

### OpenCode Tools (20 total)

| Tool | Description | AVA Equivalent |
|------|-------------|-------------------|
| `read` | Read files with line numbers | `read_file` ✅ |
| `write` | Overwrite files | `write_file` ✅ |
| `edit` | Search-replace with 9 fuzzy strategies | `edit` ✅ (needs fuzzy) |
| `glob` | Find files by pattern | `glob` ✅ |
| `grep` | Search content (ripgrep) | `grep` ✅ |
| `list` | Directory tree | `ls` ✅ |
| `bash` | Execute shell (tree-sitter parsing) | `bash` ✅ |
| `task` | Spawn subagents | `task` ✅ |
| `question` | Ask user questions | `question` ✅ |
| `todoread` | Read session todos | `todoread` ✅ |
| `todowrite` | Update session todos | `todowrite` ✅ |
| `websearch` | Web search (Exa API) | `websearch` ✅ |
| `webfetch` | Fetch web pages | `webfetch` ✅ |
| **`batch`** | **Parallel tool execution (25 max)** | ❌ **MISSING** |
| **`skill`** | **Load skill instructions** | ❌ **MISSING** |
| **`codesearch`** | **API/library docs search (Exa)** | ❌ **MISSING** |
| **`apply_patch`** | **Unified diff for GPT models** | ❌ **MISSING** |
| **`multiedit`** | **Multiple edits in one call** | ❌ **MISSING** |
| `plan_enter` | Enter plan mode | `plan_enter` ✅ |
| `plan_exit` | Exit plan mode | `plan_exit` ✅ |
| `lsp` | LSP operations (experimental) | ⚠️ In progress |
| `invalid` | Error handling for malformed calls | ⚠️ Implicit |

### AVA-Only Tools

| Tool | Description | OpenCode Equivalent |
|------|-------------|---------------------|
| `browser` | Puppeteer automation | ❌ None |
| `attempt_completion` | Finish task with summary | ❌ None (implicit) |
| `create_file` | Create new file | Combined in `write` |
| `delete_file` | Delete file | ❌ None (uses bash) |

---

## Critical Missing Features

### 1. Batch Tool (HIGH PRIORITY)

OpenCode's `batch` tool allows parallel execution of up to 25 tools in a single call:

```typescript
// OpenCode batch tool
{
  tool_calls: [
    { tool: "read", parameters: { filePath: "file1.ts" } },
    { tool: "read", parameters: { filePath: "file2.ts" } },
    { tool: "glob", parameters: { pattern: "**/*.md" } },
  ]
}
```

**Benefits:**
- Reduces API round trips significantly
- Better token efficiency (single response)
- Faster multi-file operations
- Recursive batch calls blocked for safety

**Recommendation:** Implement in `packages/core/src/tools/batch.ts`

---

### 2. Fuzzy Edit Strategies (HIGH PRIORITY)

OpenCode's `edit` tool has **9 fuzzy matching strategies**:

| Strategy | Description |
|----------|-------------|
| `SimpleReplacer` | Exact match |
| `LineTrimmedReplacer` | Trim-based matching |
| `BlockAnchorReplacer` | First/last line anchors |
| `WhitespaceNormalizedReplacer` | Collapse whitespace |
| `IndentationFlexibleReplacer` | Ignore indentation |
| `EscapeNormalizedReplacer` | Handle escapes |
| `TrimmedBoundaryReplacer` | Trim boundaries |
| `ContextAwareReplacer` | Context-based blocks |
| `MultiOccurrenceReplacer` | All occurrences |

**Why it matters:** LLM-generated edits often have minor whitespace/indentation differences. Fuzzy matching prevents edit failures.

**Recommendation:** Port fuzzy strategies to `packages/core/src/tools/edit.ts`

---

### 3. Skill System (MEDIUM PRIORITY)

OpenCode's skill system loads reusable knowledge modules:

```markdown
---
name: typescript-patterns
description: Advanced TypeScript patterns
---

# TypeScript Patterns
[Content...]
```

**Discovery locations:**
- `.claude/skills/**/SKILL.md`
- `.opencode/skill/**/SKILL.md`
- Custom paths from config

**Recommendation:** Implement skill loading in `packages/core/src/skills/`

---

### 4. Code Search (Exa API) (MEDIUM PRIORITY)

Searches API documentation and code examples:

```typescript
{
  query: "React useState hook examples",
  tokensNum: 5000  // 1000-50000
}
```

**Benefit:** Direct access to up-to-date library documentation without web browsing.

---

### 5. Apply Patch / Unified Diff (MEDIUM PRIORITY)

GPT models work better with unified diff format:

```
*** Begin Patch
*** Update File: path/to/file.txt
@@ context line @@
-old line
+new line
*** End Patch
```

**Supports:** Add/Update/Delete/Move operations with chunk-based fuzzy matching.

---

### 6. Multi-Edit Tool (LOW PRIORITY)

Combines multiple edits to a single file:

```typescript
{
  filePath: "file.ts",
  edits: [
    { oldString: "foo", newString: "bar" },
    { oldString: "baz", newString: "qux" },
  ]
}
```

---

## Provider Comparison

### OpenCode: 21 Bundled Providers

| Provider | Features |
|----------|----------|
| Anthropic | Prompt caching, beta headers |
| OpenAI | Responses API, Chat API |
| Google AI/Vertex | Cross-region inference |
| Amazon Bedrock | Credential chain, region prefixing |
| Azure OpenAI | Responses API support |
| GitHub Copilot | OAuth, model API switching |
| OpenRouter | Custom headers |
| xAI, Mistral, Groq, DeepInfra, Cerebras | API key |
| Cohere, TogetherAI, Perplexity | API key |
| GitLab | OAuth |
| Cloudflare AI Gateway | OpenAI-compatible |
| SAP AI Core | Service key |

### Key Provider Features

| Feature | OpenCode | AVA |
|---------|----------|--------|
| Prompt caching (Anthropic) | ✅ Ephemeral | ⚠️ Verify |
| Responses API (OpenAI) | ✅ Custom SDK | ⚠️ Verify |
| Model variants (reasoning effort) | ✅ Per-model | ⚠️ Verify |
| SDK caching by config hash | ✅ | ⚠️ Verify |
| Fuzzy model matching | ✅ Suggestions | ⚠️ Verify |
| Provider-defined tools | ✅ 6 tools | ⚠️ Verify |

### Provider-Defined Tools (OpenAI)

| Tool | Description |
|------|-------------|
| `openai.web_search` | Built-in web search |
| `openai.code_interpreter` | Python sandbox |
| `openai.file_search` | Vector store search |
| `openai.image_generation` | Generate images |
| `openai.local_shell` | Execute shell |
| `openai.web_search_preview` | Search with context |

---

## MCP Comparison

### Transport Types

| Transport | OpenCode | AVA |
|-----------|----------|--------|
| Local (stdio) | ✅ | ✅ |
| HTTP (streamable) | ✅ | ⚠️ Verify |
| SSE | ✅ | ⚠️ Verify |
| OAuth support | ✅ Dynamic registration | ⚠️ Verify |

### MCP Features

| Feature | OpenCode | AVA |
|---------|----------|--------|
| Tool integration | ✅ Automatic | ⚠️ Verify |
| Resource reading | ✅ | ⚠️ Verify |
| Prompt templates | ✅ | ⚠️ Verify |
| Status tracking | ✅ 5 states | ⚠️ Verify |
| Output truncation | ✅ 50KB limit | ⚠️ Verify |

---

## Permission System Comparison

### OpenCode Permission Model

```typescript
// Pattern-based rules
{
  permission: [
    { rule: "bash", action: "ask" },
    { rule: "edit.*.env*", action: "deny" },
    { rule: "external_directory.*", action: "ask" },
  ]
}
```

**Features:**
- Pattern matching with wildcards
- Actions: `allow`, `deny`, `ask`
- Doom loop detection (3 identical calls)
- External directory protection
- Per-agent permission overrides
- Session-level permission merging

### AVA Permission Model

Location: `packages/core/src/permissions/`

**Verify:**
- Pattern-based rules
- Doom loop detection
- External directory protection

---

## Session Management Comparison

### OpenCode Session Model

**12 Part Types (Discriminated Unions):**
1. `TextPart` - LLM text output
2. `ReasoningPart` - Model thinking
3. `ToolPart` - Tool invocation (state machine)
4. `FilePart` - File attachments
5. `StepStartPart` - Step beginning with snapshot
6. `StepFinishPart` - Step completion
7. `SnapshotPart` - Git snapshot reference
8. `PatchPart` - File changes tracking
9. `AgentPart` - Agent invocation marker
10. `SubtaskPart` - Subagent task
11. `RetryPart` - API retry tracking
12. `CompactionPart` - Compaction marker

**Tool State Machine:**
```
pending → running → completed | error
```

**Compaction:**
- Auto-triggers at context overflow
- Prunes old tool outputs (keeps last 40k tokens)
- Protected tools: `["skill"]`

### AVA Session Model

Location: `packages/core/src/session/`

**Verify:**
- Part types coverage
- Tool state machine
- Compaction strategy

---

## LSP Integration Comparison

### OpenCode: 30+ Language Servers

| Server | Languages | Auto-Download |
|--------|-----------|---------------|
| TypeScript | .ts, .tsx, .js, .jsx | No (uses project's) |
| gopls | .go | Yes |
| rust-analyzer | .rs | No |
| pyright | .py | Yes |
| clangd | .c, .cpp, .h | Yes |
| zls | .zig | Yes |
| elixir-ls | .ex, .exs | Yes |
| jdtls | .java | Yes |
| kotlin-ls | .kt | Yes |
| svelte | .svelte | Yes |
| astro | .astro | Yes |
| vue | .vue | Yes |
| lua-ls | .lua | Yes |
| bash | .sh, .bash | Yes |
| ... | +16 more | ... |

**Operations:**
- `hover`, `definition`, `references`
- `documentSymbol`, `workspaceSymbol`
- `prepareCallHierarchy`, `incomingCalls`, `outgoingCalls`

### AVA LSP

Location: `packages/core/src/lsp/`

**Verify:** Coverage of language servers and operations

---

## Agent System Comparison

### OpenCode Built-in Agents

| Agent | Mode | Purpose |
|-------|------|---------|
| `build` | primary | Default execution |
| `plan` | primary | Plan mode (read-only) |
| `general` | subagent | Multi-step tasks |
| `explore` | subagent | Fast exploration |
| `compaction` | hidden | Context summarization |
| `title` | hidden | Title generation |
| `summary` | hidden | Summary generation |

**Agent Configuration:**
- Custom prompts, models, temperatures
- Permission overrides
- Step limits
- Color/visibility

### AVA Agent System

Location: `packages/core/src/agent/`

Based on CLAUDE.md: Similar subagent architecture with `task` tool delegation.

---

## CLI/TUI Comparison

### OpenCode TUI Features

- Ink-based React terminal UI
- Route-based navigation
- Theme system (JSON themes)
- Command system (slash commands)
- Real-time streaming
- Permission dialogs

### AVA TUI

Location: `cli/` and `src/` (SolidJS Tauri frontend)

**Different approach:** Tauri native app vs terminal UI

---

## Git Snapshot System

### OpenCode Snapshot Features

| Feature | Description |
|---------|-------------|
| `Snapshot.track()` | Create snapshot |
| `Snapshot.patch()` | Get changes since snapshot |
| `Snapshot.revert()` | Rollback changes |
| `Snapshot.restore()` | Restore specific snapshot |
| Session revert | Undo to message/part |

### AVA Snapshot System

Location: `packages/core/src/git/`

**Verify:** Feature parity

---

## SDK Comparison

### OpenCode SDK

- TypeScript SDK (`@opencode-ai/sdk`)
- OpenAPI-generated types
- Event streaming
- Session management API
- Workspace operations

### AVA SDK

**Status:** Internal use in `packages/core/`

---

## Recommended Action Items

### Priority 1 (Critical)

1. **Implement Batch Tool**
   - File: `packages/core/src/tools/batch.ts`
   - Max 25 parallel calls
   - Disallow recursive batching
   - Track individual tool states

2. **Add Fuzzy Edit Strategies**
   - File: `packages/core/src/tools/edit.ts`
   - Port 9 strategies from OpenCode
   - Prioritize: `LineTrimmed`, `WhitespaceNormalized`, `IndentationFlexible`

### Priority 2 (High)

3. **Implement Skill System**
   - Directory: `packages/core/src/skills/`
   - Scan: `.estela/skills/`, `~/.estela/skills/`
   - Format: Markdown with YAML frontmatter

4. **Add Doom Loop Detection**
   - File: `packages/core/src/session/`
   - Detect 3+ identical consecutive tool calls
   - Prompt user for confirmation

### Priority 3 (Medium)

5. **Add Code Search Tool**
   - File: `packages/core/src/tools/codesearch.ts`
   - Integrate Exa API
   - Parameter: query, tokensNum

6. **Implement Apply Patch Tool**
   - File: `packages/core/src/tools/apply-patch.ts`
   - Unified diff format
   - Chunk-based fuzzy matching

7. **Add Multi-Edit Tool**
   - File: `packages/core/src/tools/multiedit.ts`
   - Batch edits to single file

### Priority 4 (Low)

8. **Expand LSP Coverage**
   - Add auto-download for more servers
   - Add call hierarchy operations

9. **Add Provider-Defined Tools**
   - OpenAI web_search, code_interpreter
   - If using OpenAI Responses API

---

## Architecture Patterns to Consider

### 1. Discriminated Unions for Messages

OpenCode uses Zod discriminated unions for type-safe message parts:

```typescript
const Part = z.discriminatedUnion("type", [
  TextPart,
  ToolPart,
  ReasoningPart,
  // ...
])
```

**Benefit:** Runtime type safety, better TypeScript inference

### 2. Instance State Pattern

```typescript
const state = Instance.state(async () => {
  // Initialize state per-project
  return data
}, async (current) => {
  // Cleanup on instance change
})
```

**Benefit:** Proper cleanup, project isolation

### 3. Event Bus Pattern

All state changes publish events:
- `session.created/updated/deleted`
- `message.part.updated`
- `permission.asked/replied`

**Benefit:** Decoupled UI updates, easier testing

### 4. Tool Context Pattern

```typescript
const context: Tool.Context = {
  sessionID,
  messageID,
  abort,
  ask,  // Permission request
  metadata,  // Progress updates
  messages,  // Conversation history
}
```

**Benefit:** Standardized tool interface

---

## AVA Advantages

| Feature | AVA | OpenCode |
|---------|--------|----------|
| Browser Automation | ✅ Puppeteer tool | ❌ |
| Native Desktop App | ✅ Tauri + SolidJS | ❌ Terminal only |
| Delete File Tool | ✅ Explicit | Uses bash |
| Create File Tool | ✅ Explicit | Combined in write |
| Attempt Completion | ✅ Explicit | Implicit |

---

## Files Generated

This analysis generated 7 comprehensive documents:

1. `01-core-cli.md` (24KB) - Agent, session, tools
2. `02-providers.md` (18KB) - 21 providers, streaming
3. `03-mcp-permissions.md` (32KB) - MCP, permissions, snapshots
4. `04-config-project.md` (29KB) - Config, project, storage
5. `05-cli-tui.md` (24KB) - CLI, TUI, commands
6. `06-acp-lsp-ide.md` (19KB) - ACP, LSP, IDE, skills
7. `07-auxiliary-packages.md` (32KB) - Desktop, SDK, UI

**Total: ~178KB of analysis documentation**

---

## Conclusion

OpenCode has several mature features worth porting to AVA:

1. **Batch tool** - Significant performance improvement
2. **Fuzzy edit strategies** - Critical for reliability
3. **Skill system** - Extensibility pattern
4. **Doom loop detection** - Safety feature

AVA has unique advantages in browser automation and native desktop experience that OpenCode lacks.

**Recommended approach:** Prioritize batch tool and fuzzy edits first, as they directly improve daily usage reliability and performance.
