# ADR-001: TypeScript for File Operations

> Architecture Decision Record

**Status:** Accepted
**Date:** 2025-01-29
**Decision Makers:** Team

---

## Context

Estela needs file system access for Epic 2 (File Tools). We had to decide between:

1. **Rust/Tauri backend** - Native Rust code for file operations
2. **TypeScript via Tauri plugins** - TypeScript code using Tauri's fs plugin

---

## Research

We analyzed 6 major open-source AI coding agents:

| Project | File Ops Language | Backend | Stars |
|---------|-------------------|---------|-------|
| OpenCode | TypeScript | TypeScript | 70k+ |
| Gemini CLI | TypeScript | TypeScript | 50k+ |
| OpenHands | Python | Python | 45k+ |
| Aider | Python | Python | 25k+ |
| Goose | Rust | Rust | 15k+ |
| Plandex | Go | Go | 10k+ |

**Key Finding:** The most popular tools (OpenCode, Gemini CLI) use **TypeScript** for file operations, not native backends.

---

## Decision

**Use TypeScript via Tauri's fs plugin for file operations.**

```typescript
import { readTextFile, writeTextFile } from '@tauri-apps/plugin-fs'
```

---

## Rationale

### Why TypeScript

| Factor | TypeScript | Rust |
|--------|------------|------|
| **Development speed** | Fast iteration | Slower compile cycle |
| **Consistency** | Same as UI code | Context switching |
| **Debugging** | Browser devtools | Separate tooling |
| **Libraries** | Rich ecosystem (glob, diff) | Fewer options |
| **Contributors** | More devs know TS | Rust is niche |
| **Reference code** | OpenCode patterns | Limited examples |

### Why NOT Rust

| Concern | Reality |
|---------|---------|
| "Performance" | File I/O is not CPU-bound; network/disk is the bottleneck |
| "Security" | Tauri's fs plugin already handles permissions and sandboxing |
| "Native feel" | TypeScript runs in the same process, no IPC overhead |

### Industry Validation

- **OpenCode** (70k stars): TypeScript file operations
- **Gemini CLI** (50k stars): TypeScript file operations
- **Cursor**: TypeScript (Electron/VS Code)
- **Claude Code CLI**: TypeScript with subprocess

---

## Implementation

### File Structure

```
src/services/tools/
├── index.ts          # Tool registry
├── types.ts          # Tool interfaces
├── read.ts           # read_file, read_lines
├── write.ts          # write_file, create_file
├── edit.ts           # str_replace, patch
├── glob.ts           # glob, list_dir
├── grep.ts           # search, grep
└── bash.ts           # execute, timeout
```

### Tool Interface (from OpenCode pattern)

```typescript
interface Tool {
  name: string
  description: string
  parameters: JSONSchema
  execute: (params: unknown) => Promise<ToolResult>
}

interface ToolResult {
  success: boolean
  output?: string
  error?: string
}
```

### Tauri Permissions

Already configured in `src-tauri/capabilities/default.json`:
```json
{
  "permissions": [
    "fs:default",
    "fs:allow-read",
    "fs:allow-write"
  ]
}
```

---

## Consequences

### Positive
- Faster development velocity
- Single language codebase (TypeScript)
- Can directly port patterns from OpenCode
- Easier to onboard contributors

### Negative
- Can't use Rust-specific libraries (tree-sitter native bindings)
- Slightly less performance for CPU-intensive parsing

### Mitigations
- Use Tauri commands for performance-critical operations if needed later
- tree-sitter has WASM bindings for TypeScript

---

## References

- [OpenCode Tools](https://github.com/sst/opencode/tree/dev/packages/opencode/src/tool)
- [Gemini CLI](https://github.com/google-gemini/gemini-cli)
- [Tauri FS Plugin](https://v2.tauri.app/plugin/file-system/)
- [docs/reference-code/](../reference-code/) - Cloned repos
