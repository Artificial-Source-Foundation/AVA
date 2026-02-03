# Epic 2: File Tools

> ✅ Completed: 2025-01-30

---

## Goal

Implement file reading, writing, and shell execution tools for LLM function calling.

---

## Sprints Completed

| Sprint | What | Lines |
|--------|------|-------|
| 2.1 | File Reading Tools | ~1015 |
| 2.2 | File Writing Tools | ~305 |
| 2.3 | Bash Execution | ~195 |

**Total:** ~1515 lines

---

## What Was Built

### File Reading (2.1)
- **glob**: Pattern matching (*, **, {a,b}) with mtime sorting
- **read_file**: Line numbers, pagination, 2000 line limit
- **grep**: Regex content search with file filtering

### File Writing (2.2)
- **create_file**: New file (fails if exists)
- **write_file**: Create or overwrite
- **delete_file**: Remove files (fails on directories)

### Bash Execution (2.3)
- **bash**: Shell command execution with timeout
- Process spawn with kill() capability
- Output truncation (2000 lines OR 50KB)

### Foundation
- Tool registry with auto-registration
- ToolError class and error types
- Binary file detection
- Path resolution utilities
- Glob pattern matching
- Anthropic tool_use streaming integration

---

## Tool Limits

| Limit | Value | Purpose |
|-------|-------|---------|
| MAX_RESULTS | 100 | Glob results |
| MAX_LINES | 2000 | File read |
| MAX_LINE_LENGTH | 2000 | Truncation |
| MAX_BYTES | 50KB | Content size |
| MAX_TOOL_CALLS | 10 | Per turn |
| TIMEOUT | 2 min | Bash execution |

---

## Key Decisions

- **No Zod**: Simple manual validation (added in Epic 6)
- **Native glob**: TypeScript pattern matching, not ripgrep
- **Tauri shell**: spawn() for kill capability
- **Protected limits**: Prevent LLM from exhausting resources

---

## Files Created

```
src/services/tools/
├── types.ts      # Tool interfaces
├── errors.ts     # ToolError class
├── registry.ts   # Registration, lookup, execution
├── utils.ts      # Binary detection, path resolution, glob matching
├── glob.ts       # File pattern matching
├── read.ts       # File reading with line numbers
├── grep.ts       # Content search
├── create.ts     # Create new files
├── write.ts      # Write/overwrite files
├── delete.ts     # Delete files
├── bash.ts       # Shell execution
└── index.ts      # Barrel export
```

---

## Tauri Permissions Added

```json
{
  "fs:allow-read-text-file": true,
  "fs:allow-read-dir": true,
  "fs:allow-stat": true,
  "fs:allow-write-text-file": true,
  "fs:allow-remove": true,
  "fs:allow-exists": true,
  "fs:allow-mkdir": true,
  "shell:allow-execute": true
}
```
