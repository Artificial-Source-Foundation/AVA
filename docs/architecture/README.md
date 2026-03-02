# Architecture

> AVA system design — desktop-first AI coding app with a virtual dev team

---

## Overview

AVA is a monorepo with three layers:

```
AVA/
├── src/                    # Desktop app (Tauri + SolidJS) ← PRIMARY
├── src-tauri/              # Rust backend for Tauri
├── packages/
│   ├── core/               # Shared business logic (~54,200 lines)
│   ├── core-v2/            # Minimal core (~28 files, 407 tests)
│   ├── extensions/         # Built-in extensions (25+ modules, 319 tests)
│   ├── platform-node/      # Node.js implementations (fs, shell, PTY)
│   └── platform-tauri/     # Tauri implementations (fs, shell)
└── cli/                    # CLI interface (secondary)
```

### Desktop App (`src/`)

SolidJS + TypeScript + Tailwind CSS v4. IDE-inspired layout:
- Activity Bar (left) — Sessions, Explorer (2 icons)
- Main Area (center) — Chat with Team Lead, code viewer
- Right Panel — Agent activity on demand (320px, closeable)
- Bottom Panel — Memory panel (resizable, Ctrl+M toggle)
- Settings — Modal overlay (Ctrl+,)

### Core Package (`packages/core/`)

All business logic lives here. Platform-agnostic — works in both Tauri and Node.js.

**Agent System** (the dev team):
| Module | Lines | Purpose |
|--------|-------|---------|
| `agent/` | ~2,500 | Agent loop, planning, recovery, subagents, doom loop detection |
| `commander/` | ~2,400 | Team Lead → Senior Leads → Junior Devs delegation |
| `validator/` | ~1,000 | QA pipeline (syntax, types, lint, test, self-review) |

**Tools** (35 total — 6 core + 29 extended):
| Module | Lines | Purpose |
|--------|-------|---------|
| `tools/` (core-v2) | ~2,500 | read_file, write_file, edit, glob, grep, bash |
| `tools-extended/` (extensions) | ~3,000 | create_file, delete_file, apply_patch, multiedit, ls, batch, codesearch, repo_map, question, todoread, todowrite, task, websearch, webfetch, attempt_completion, plan_enter, plan_exit |
| `commander/` (extensions) | ~1,500 | delegate_coder, delegate_tester, delegate_reviewer, delegate_researcher, delegate_debugger |
| `memory/` (extensions) | ~500 | memory_read, memory_write, memory_list, memory_delete |
| `lsp/` (extensions) | ~800 | lsp_diagnostics, lsp_hover, lsp_definition |

**Intelligence**:
| Module | Lines | Purpose |
|--------|-------|---------|
| `codebase/` | ~1,800 | Repo map, symbols, PageRank, dependency graph |
| `context/` | ~1,450 | Token tracking, compaction, compression strategies |
| `lsp/` | ~400 | Language server (TS, Python, Go, Rust, Java) |

**Extensibility** (plugin system):
| Module | Lines | Purpose |
|--------|-------|---------|
| `extensions/` | ~600 | Install, enable, disable, reload plugins |
| `custom-commands/` | ~350 | TOML custom commands |
| `hooks/` | ~1,100 | PreToolUse, PostToolUse, Task lifecycle hooks |
| `skills/` | ~350 | Auto-invoked knowledge modules |
| `mcp/` | ~950 | MCP protocol client + server registry |

**Safety**:
| Module | Lines | Purpose |
|--------|-------|---------|
| `permissions/` | ~1,700 | Risk assessment, auto-approval, path-aware checks |
| `policy/` | ~800 | Priority rules, wildcards, regex matching |
| `permissions/trusted-folders` | ~400 | Per-folder security levels |

**Infrastructure**:
| Module | Lines | Purpose |
|--------|-------|---------|
| `llm/` | ~2,600 | 14 provider clients (Anthropic, OpenAI, Google, OpenRouter, DeepSeek, Mistral, Groq, xAI, Cohere, Together, Ollama, GLM, Kimi, Copilot) |
| `config/` | ~1,150 | Settings, credentials, Zod validation |
| `session/` | ~800 | State, checkpoints, forking, resume |
| `auth/` | ~500 | OAuth + PKCE |
| `bus/` | ~400 | Message bus (pub/sub, tool confirmation) |

---

## The Dev Team Flow

```
User types request
    │
    ▼
Team Lead (AgentExecutor + Commander)
    │ Analyzes task, creates plan
    │
    ├─→ Senior Frontend Lead (Worker)
    │   │ Receives frontend scope
    │   ├─→ Jr. Dev: Component file (SubWorker)
    │   └─→ Jr. Dev: Styling file (SubWorker)
    │   └─→ Auto-reports to Team Lead ✓
    │
    ├─→ Senior Backend Lead (Worker)
    │   │ Receives backend scope
    │   ├─→ Jr. Dev: API routes (SubWorker)
    │   └─→ Jr. Dev: Database (SubWorker)
    │   └─→ Auto-reports to Team Lead ✓
    │
    └─→ Validator (QA)
        │ Runs after all workers complete
        ├─→ Syntax check
        ├─→ Type check
        ├─→ Lint
        ├─→ Test
        └─→ Self-review (LLM code review)
            └─→ Results to Team Lead ✓

Team Lead summarizes and presents to user
```

**User can intervene at any point** — click into any agent's chat, fix issues, send results back up.

---

## Database

SQLite via `tauri-plugin-sql`:
```sql
sessions: id, name, created_at, updated_at
messages: id, session_id, role, content, tokens_used, cost, model, created_at
agents: id, session_id, type, status, model, created_at, completed_at, task_description, result
file_operations: id, session_id, path, operation, diff, created_at
terminal_executions: id, session_id, command, output, exit_code, cwd, created_at
memory_items: id, session_id, type, key, value, created_at
```

---

## Key Design Decisions

| Decision | Choice | Why |
|----------|--------|-----|
| Desktop framework | Tauri 2.0 | ~5MB binary, 30MB RAM, Rust security |
| Frontend | SolidJS | Fine-grained reactivity for streaming |
| LLM calls | Frontend-first | Simpler SSE, faster iteration |
| Tools | Platform-agnostic | Single implementation for Tauri + Node |
| Agents | Workers as tools | Gemini CLI pattern — simple, unified |
| Edits | 8 fuzzy strategies | Handles LLM whitespace/indent errors |
| Shell | Process groups | Clean SIGKILL escalation |
| Plugins | Skills + Commands | Auto-invoked + manual, like Claude Code |
