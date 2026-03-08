# Plandex

> AI coding engine for large projects (~8k GitHub stars)
> Analyzed: March 2026

---

## Architecture Summary

Plandex is an **AI coding engine** designed for working with large projects. It's built in **Go** with a focus on batch operations and project-wide changes.

**Key architectural decisions:**
- **Batch operations** — Execute multiple changes at once
- **Project-wide context** — Understanding of large codebases
- **Plan-then-execute** — Two-phase approach
- **Go implementation** — Performance and concurrency

### Project Structure

```
plandex/
├── cmd/                     # CLI entry points
├── pkg/                     # Core packages
│   ├── context/             # Context management
│   ├── plan/                # Planning engine
│   └── exec/                # Execution engine
└── ...
```

---

## Key Patterns

### 1. Plan-Then-Execute

Two-phase approach:
1. **Plan phase** — Analyze codebase, create execution plan
2. **Execute phase** — Apply planned changes

This reduces errors and allows review before changes.

### 2. Batch Operations

Multiple changes in one operation:
- Rename across files
- Refactor patterns
- Update imports

### 3. Project-Wide Context

Understanding of large codebases:
- Cross-file analysis
- Dependency tracking
- Symbol resolution

### 4. Go Concurrency

Leverages Go's goroutines:
- Parallel analysis
- Concurrent file operations
- Non-blocking UI

---

## What AVA Can Learn

### High Priority

1. **Plan-Then-Execute** — Two-phase approach improves reliability for complex tasks.

2. **Batch Operations** — Multi-file changes should be atomic.

### Medium Priority

3. **Project-Wide Analysis** — Better understanding of large codebases.

---

## Comparison: Plandex vs AVA

| Capability | Plandex | AVA |
|------------|---------|-----|
| **Approach** | Plan-then-execute | Iterative |
| **Language** | Go | TypeScript/Rust |
| **Batch** | Yes | Limited |
| **Context** | Project-wide | File + symbols |

---

*Consolidated from: audits/plandex-audit.md, backend-analysis/plandex.md*
