# Reference

> API documentation and quick references

---

## Documents

| Document | Description |
|----------|-------------|
| [types.md](./types.md) | TypeScript type definitions |
| [tools.md](./tools.md) | Tool API reference |
| [tauri-commands.md](./tauri-commands.md) | Rust Tauri command reference |

---

## Quick Type Reference

### Agent

```typescript
interface Agent {
  id: string;
  type: 'commander' | 'operator' | 'validator';
  model: string;
  contextWindow: Message[];
  status: 'idle' | 'thinking' | 'executing' | 'waiting';
  assignedFiles?: string[];
}
```

### Task

```typescript
interface Task {
  id: string;
  objective: string;
  outputFormat: string;
  tools: string[];
  sources: string[];
  boundaries: string;
  parentTaskId?: string;
}
```

### TaskResult

```typescript
interface TaskResult {
  taskId: string;
  agentId: string;
  summary: string;
  filesModified: string[];
  errors?: string[];
  tokensUsed: number;
}
```

---

## Tool Reference

### str_replace

```typescript
interface StrReplaceParams {
  path: string;
  old_str: string;   // Must be unique in file
  new_str: string;   // Replacement (empty to delete)
}
```

### file_create

```typescript
interface FileCreateParams {
  path: string;
  content: string;
}
```

### bash

```typescript
interface BashParams {
  command: string;
  cwd?: string;
  timeout?: number;
}
```
