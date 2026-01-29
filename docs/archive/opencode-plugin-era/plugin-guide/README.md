# OpenCode Plugin Development Guide

> Comprehensive patterns extracted from 41 production plugins for building world-class OpenCode/Claude Code plugins.

This guide synthesizes patterns from analyzing the reference plugin ecosystem. Any AI system can use this to build sophisticated plugins following battle-tested approaches.

---

## Navigation

| File | Topic |
|------|-------|
| [01-architecture.md](01-architecture.md) | Plugin entry points, lifecycle, ctx object |
| [02-configuration.md](02-configuration.md) | Hierarchical config, JSONC, Zod validation |
| [03-hooks.md](03-hooks.md) | 7 hook types, event handlers, interception |
| [04-tools.md](04-tools.md) | Tool definition, schemas, factory pattern |
| [05-memory.md](05-memory.md) | File-based, SQLite, vectors, logfmt |
| [06-skills.md](06-skills.md) | Discovery, SKILL.md format, model-aware rendering |
| [07-safety.md](07-safety.md) | Command interception, path validation, secrets |
| [08-background-tasks.md](08-background-tasks.md) | Concurrency, fire-and-forget, status tracking |
| [09-terminal.md](09-terminal.md) | Cross-platform spawning, tmux, IPC sockets |
| [10-dx.md](10-dx.md) | Logging, toasts, notifications |
| [11-patterns.md](11-patterns.md) | Idempotency, batching, lazy loading |
| [12-templates.md](12-templates.md) | File structure templates |
| [13-setup.md](13-setup.md) | Dependencies, package.json, tsconfig |
| [14-reference.md](14-reference.md) | Quick reference tables |

---

## Source Reference Plugins

Each pattern links to production code in `../REFERENCE_CODE/`:

| Pattern | Best Example |
|---------|-------------|
| Plugin architecture | `oh-my-opencode/src/index.ts` |
| Configuration | `oh-my-opencode/src/plugin-config.ts` |
| Background tasks | `background-agents/src/plugin/` |
| Memory persistence | `agent-memory/src/` |
| Vector search | `opencode-mem/src/` |
| Skills system | `opencode-skillful/src/` |
| Safety hooks | `safety-net/src/` |
| Terminal spawning | `worktree/src/lib/spawn/` |
| Session handoff | `handoff/src/` |
| Notifications | `opencode-notify/src/` |
| Token analysis | `tokenscope/plugin/` |

---

## Quick Start

1. **New plugin?** Start with [01-architecture.md](01-architecture.md)
2. **Need hooks?** See [03-hooks.md](03-hooks.md)
3. **Adding tools?** See [04-tools.md](04-tools.md)
4. **Setup help?** See [13-setup.md](13-setup.md)
