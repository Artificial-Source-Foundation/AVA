# Agents

> Agent specifications and communication protocols

---

## Documents

| Document | Description |
|----------|-------------|
| [commander.md](./commander.md) | Commander agent - planning and orchestration |
| [operator.md](./operator.md) | Operator agents - code execution |
| [validator.md](./validator.md) | Validator agent - verification |
| [communication.md](./communication.md) | Inter-agent communication protocol |

---

## Agent Hierarchy

```
COMMANDER (Opus/Sonnet)
    │
    ├── Plans & decomposes tasks
    ├── Maintains project context
    ├── Delegates to Operators
    └── Validates completed work
         │
         ▼
    OPERATORS (Sonnet/Haiku)
         │
         ├── Execute specific file tasks
         ├── Run in parallel
         ├── Report summaries back
         └── Isolated contexts
              │
              ▼
         VALIDATOR (Haiku)
              │
              ├── Runs linter
              ├── Type checks
              └── Reports errors
```

---

## Agent Types

### Commander

- **Model**: Claude Opus 4.5 or Sonnet 4.5
- **Role**: Strategic planning, task decomposition, delegation
- **Never**: Writes code directly (except docs/markdown)
- **Tools**: Task delegation, project management, documentation

### Operator

- **Model**: Claude Sonnet 4.5 or Haiku 4.5
- **Role**: Execute specific file modifications
- **Scope**: Isolated to assigned files only
- **Tools**: str_replace, file_create, file_read, bash

### Validator

- **Model**: Claude Haiku 4.5 (fast, cheap)
- **Role**: Verify changes after operators complete
- **Tools**: lint, typecheck, test (read-only)
