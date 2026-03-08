# OpenCode

> AI coding assistant by SST (~115k GitHub stars)
> Analyzed: March 2026

---

## Architecture Summary

OpenCode is a **TypeScript/Bun-based** AI coding assistant with a TUI interface. Originally written in Go, it was rewritten in TypeScript for better developer velocity while maintaining performance via Bun.

**Key architectural decisions:**
- **Bun runtime** — Built-in SQLite, shell execution, fast startup
- **9-layer edit fuzzer** — Cascading replacement strategies
- **Tree-sitter bash parsing** — AST-based command analysis for safety
- **Rule-based permissions** — Glob patterns with allow/deny/ask
- **Multi-agent system** — build/plan/explore agents with different capabilities
- **Skill discovery** — Auto-discovers SKILL.md files
- **Shadow git snapshots** — Separate git repo for session tracking

### Project Structure

```
opencode/
├── packages/
│   ├── opencode/            # Core agent + TUI
│   │   ├── src/
│   │   │   ├── agent/       # Agent definitions
│   │   │   ├── lsp/         # LSP integration (30+ servers)
│   │   │   ├── mcp/         # MCP client with OAuth
│   │   │   ├── permission/  # Permission engine
│   │   │   ├── session/     # Session management
│   │   │   ├── skill/       # Skill discovery
│   │   │   ├── snapshot/    # Shadow git snapshots
│   │   │   ├── tool/        # Tool implementations
│   │   │   └── worktree/    # Git worktree isolation
│   │   └── package.json
│   └── ...
```

---

## Key Patterns

### 1. 9-Layer Edit Fuzzer

Cascading replacement strategies for reliable edits:

```
Simple → LineTrimmed → BlockAnchor → WhitespaceNormalized →
IndentationFlexible → EscapeNormalized → TrimmedBoundary →
ContextAware → MultiOccurrence
```

Achieves ~95% edit success rate. BlockAnchorReplacer uses Levenshtein distance for fuzzy matching.

### 2. Tree-Sitter Bash Parsing

AST-based command parsing before execution:

```typescript
const tree = parser.parse(params.command)
for (const node of tree.rootNode.descendantsOfType("command")) {
    // Extract paths from cd, rm, cp, mv, etc.
    // Check if external to project
}
```

Handles quotes, escapes, and redirects correctly — more secure than regex.

### 3. Rule-Based Permissions

Glob-pattern permissions:

```typescript
const rules = [
    { permission: "edit", pattern: "src/**", action: "allow" },
    { permission: "read", pattern: "*.env", action: "ask" },
    { permission: "bash", pattern: "rm -rf *", action: "deny" },
]
```

Last matching rule wins. Supports wildcards.

### 4. Multi-Agent System

6 built-in agents with different personalities:

| Agent | Mode | Tools |
|-------|------|-------|
| build | primary | All (with permissions) |
| plan | primary | No edit tools |
| explore | subagent | Read-only |
| compaction | hidden | None (summarization) |

Users can define custom agents in config.

### 5. Skill Discovery

Auto-discovers SKILL.md files:

```markdown
---
name: react-patterns
description: React best practices
---

# React Patterns
When editing React components...
```

Locations: `.opencode/skills/`, `.claude/skills/`, `~/.config/opencode/skills/`

### 6. Shadow Git Snapshots

Separate git repo at `$DATA_DIR/snapshot/$PROJECT_ID`:
- `Snapshot.track()` — git add + write-tree
- `Snapshot.patch(hash)` — git diff
- `Snapshot.revert()` — git checkout

No pollution of user's git history.

---

## What AVA Can Learn

### High Priority

1. **Tree-Sitter Bash Parsing** — More secure than regex for command analysis. Prevents permission bypasses.

2. **9-Layer Edit Fuzzer** — Add BlockAnchor with Levenshtein to AVA's 8 strategies.

3. **Shadow Git Snapshots** — Isolated repos are cleaner than ghost commits.

### Medium Priority

4. **Rule-Based Permissions** — More user-friendly than middleware approach.

5. **Skill Discovery** — Compatibility with `.claude/skills/` ecosystem.

6. **Multi-Agent System** — Different tool sets for different tasks improves speed and safety.

### Patterns to Avoid

- **Bun dependency** — Node.js ecosystem is larger, more stable
- **Namespace pattern** — ES modules are clearer
- **TUI-only** — Desktop UI is the future

---

## Comparison: OpenCode vs AVA

| Capability | OpenCode | AVA |
|------------|----------|-----|
| **Runtime** | Bun | Node.js + Tauri |
| **Startup** | <50ms | 200ms-2s |
| **Edit strategies** | 9-layer | 8 strategies |
| **Bash parsing** | Tree-sitter AST | Regex-based |
| **Permissions** | Rule-based (glob) | Middleware-based |
| **Agents** | Named agents | Praxis hierarchy |
| **Skills** | SKILL.md discovery | Built-in skills |
| **LSP** | Built-in (9 ops) | Via LSP module |
| **Snapshots** | Shadow git | Git checkpoints |
| **MCP** | OAuth-first | MCP client |
| **UI** | TUI | Desktop (Tauri) |

---

## File References

| File | Lines | Purpose |
|------|-------|---------|
| `packages/opencode/src/tool/edit.ts` | ~650 | 9-layer edit fuzzer |
| `packages/opencode/src/snapshot/index.ts` | 297 | Shadow git snapshots |
| `packages/opencode/src/worktree/index.ts` | 643 | Git worktree isolation |
| `packages/opencode/src/permission/next.ts` | 286 | Permission engine |
| `packages/opencode/src/session/processor.ts` | ~1,000 | Agent loop |

---

*Consolidated from: audits/opencode-audit.md, opencode/*.md, backend-analysis/opencode.md, backend-analysis/opencode-detailed.md*
