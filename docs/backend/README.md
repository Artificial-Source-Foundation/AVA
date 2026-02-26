# Backend — @ava/core

> The brain of AVA. ~237 source files, ~54,500 lines. Test baseline: ~3302 tests across ~162 files. Includes dual-stack: original core + core-v2 + extensions.

**Package:** `packages/core/` | **Entry:** `packages/core/src/index.ts` | **Exports:** 29 modules

---

## Architecture at a Glance

```
User (Desktop/CLI)
  → Agent System (autonomous loop)
    → Commander (hierarchical delegation)
      → Team Lead plans, delegates to Senior Leads
        → Senior Leads spawn Junior Devs
          → Junior Devs execute Tools → LLM → Results
    → Validator (QA verification)
  → Response to user
```

---

## Module Map (29 directories)

### Agent System (core loop)

| Module | Files | Lines | Purpose |
|--------|-------|-------|---------|
| `agent/` | 22 | 5,530 | Autonomous loop, planning, recovery, subagents, modes (plan + minimal), metrics, validation gate |
| `commander/` | 13 | 2,870 | Team Lead → Senior Leads → Junior Devs delegation, keyword auto-routing |
| `validator/` | 9 | 2,256 | QA pipeline (syntax, lint, build, test, self-review) — wired into agent loop completion gate |

### Tools (24 registered tools)

| Module | Files | Lines | Purpose |
|--------|-------|-------|---------|
| `tools/` | 43 | 12,123 | read, write, edit, bash, glob, grep, browser, websearch, sandbox, etc. |

### Intelligence

| Module | Files | Lines | Purpose |
|--------|-------|-------|---------|
| `codebase/` | 11 | 3,431 | Repo map, symbols, imports, PageRank, tree-sitter |
| `context/` | 12 | 2,206 | Token tracking, compaction, visibility metadata, auto-compaction |
| `lsp/` | 4 | 1,219 | Language Server Protocol (5 languages) |

### Extensibility

| Module | Files | Lines | Purpose |
|--------|-------|-------|---------|
| `extensions/` | 5 | 947 | Plugin system (manifest, storage, manager) |
| `custom-commands/` | 6 | 993 | TOML-based user commands (discovery, parsing, templating) |
| `hooks/` | 4 | 1,147 | Lifecycle hooks (PreToolUse, PostToolUse, etc.) |
| `mcp/` | 6 | 1,495 | Model Context Protocol client (OAuth, bridge, discovery) |
| `skills/` | 4 | 629 | Auto-invoked knowledge modules |
| `slash-commands/` | 4 | 854 | User-invocable `/commands` |

### Safety

| Module | Files | Lines | Purpose |
|--------|-------|-------|---------|
| `permissions/` | 13 | 3,924 | Risk assessment, auto-approval, security inspector pipeline, audit trail |
| `policy/` | 5 | 1,071 | Policy engine (tool approval rules, wildcards, regex) |

### Infrastructure

| Module | Files | Lines | Purpose |
|--------|-------|-------|---------|
| `llm/` | 19 | 2,596 | LLM client factory + 14 providers + utils (SSE, OpenAI-compat) |
| `config/` | 9 | 2,172 | Settings, credentials, schema, storage, migration, sandbox settings |
| `session/` | 6 | 2,024 | Session management, resume, forking, doom-loop detection |
| `auth/` | 8 | 1,107 | OAuth + PKCE (Anthropic, Copilot, Google, OpenAI) |
| `bus/` | 3 | 524 | Message bus (pub/sub event system) |
| `models/` | 3 | 674 | Model registry (~16 LLM models) |
| `scheduler/` | 3 | 337 | Background task scheduler |
| `question/` | 3 | 361 | LLM-to-user question system |

### Utility

| Module | Files | Lines | Purpose |
|--------|-------|-------|---------|
| `diff/` | 4 | 657 | Diff tracking, unified format |
| `focus-chain/` | 4 | 825 | Task progress tracking |
| `git/` | 5 | 969 | Git snapshots, auto-commit, version control utilities |
| `instructions/` | 3 | 321 | Project/directory instructions loader |
| `integrations/` | 2 | 351 | External integrations (Exa web search) |
| `logger/` | 4 | 460 | Structured logging — NDJSON file output + source-scoped console logger |
| `types/` | 2 | 151 | Shared type definitions |

### Top-Level Files

| File | Lines | Purpose |
|------|-------|---------|
| `index.ts` | ~86 | Main barrel export (all modules) |
| `platform.ts` | 226 | Platform abstraction (Node.js, Tauri, browser) |

---

## Key Classes & Entry Points

| Class | Module | What It Does |
|-------|--------|-------------|
| `AgentExecutor` | agent/loop.ts | Runs the autonomous agent loop |
| `AgentPlanner` | agent/planner.ts | Plans tasks and recovery strategies |
| `RecoveryManager` | agent/recovery.ts | Error classification + backoff + retry |
| `AgentEventEmitter` | agent/events.ts | Agent event system (on/off/emit) |
| `SubagentManager` | agent/subagent.ts | Spawns and manages child agents |
| `WorkerRegistry` | commander/registry.ts | Registry of available Senior Leads |
| `TaskScheduler` | commander/parallel/scheduler.ts | Parallel task execution |
| `ConflictDetector` | commander/parallel/conflict.ts | File access conflict detection |
| `ValidationPipeline` | validator/pipeline.ts | Multi-step QA checks |
| `LLMClient` | llm/client.ts | Provider-agnostic LLM client factory |
| `PermissionManager` | permissions/manager.ts | Tool permission risk assessment |
| `PolicyEngine` | policy/engine.ts | Rule-based tool approval |
| `SessionManager` | session/manager.ts | Session CRUD, resume, fork |
| `ContextTracker` | context/tracker.ts | Token counting + budget management |
---

## LLM Providers (14)

All in `llm/providers/`:

| Provider | File | Auth |
|----------|------|------|
| Anthropic | anthropic.ts | API key + OAuth |
| OpenAI | openai.ts | API key + OAuth |
| Google | google.ts | API key + OAuth |
| OpenRouter | openrouter.ts | API key |
| DeepSeek | deepseek.ts | API key |
| Groq | groq.ts | API key |
| Mistral | mistral.ts | API key |
| Cohere | cohere.ts | API key |
| Together | together.ts | API key |
| xAI (Grok) | xai.ts | API key |
| Ollama | ollama.ts | Local (no key) |
| GLM | glm.ts | API key |
| Kimi | kimi.ts | API key |
| Copilot | (via OpenAI) | OAuth device code |

---

## Registered Tools (24)

All in `tools/` and auto-registered in `tools/index.ts`:

| Tool Name | File | Category |
|-----------|------|----------|
| read_file | read.ts | File I/O |
| create_file | create.ts | File I/O |
| write_file | write.ts | File I/O |
| delete_file | delete.ts | File I/O |
| edit | edit.ts | File I/O |
| apply_patch | apply-patch/ | File I/O |
| multiedit | multiedit.ts | File I/O |
| glob | glob.ts | Search |
| grep | grep.ts | Search |
| ls | ls.ts | Search |
| codesearch | codesearch.ts | Search |
| bash | bash.ts | Shell |
| batch | batch.ts | Orchestration |
| task | task.ts | Orchestration |
| question | question.ts | User interaction |
| skill | skill.ts | Extensibility |
| todo_read | todo.ts | State |
| todo_write | todo.ts | State |
| websearch | websearch.ts | Web |
| webfetch | webfetch.ts | Web |
| browser | browser/ | Web |
| attempt_completion | completion.ts | Flow control |
| plan_enter | agent/modes/plan.ts | Flow control |
| plan_exit | agent/modes/plan.ts | Flow control |

---

## Related Docs

| File | What |
|------|------|
| [architecture-guide.md](./architecture-guide.md) | **Deep navigation guide** — where things are, why, how they work, data flows, patterns |
| [modules.md](./modules.md) | Detailed per-module file listing with descriptions |
| [test-coverage.md](./test-coverage.md) | Test coverage report — tested vs untested files |
| [backlog.md](./backlog.md) | What's missing, gaps, and future work |
| [gap-analysis.md](./gap-analysis.md) | Competitive analysis vs 8 codebases (15 gaps identified) |
| [changelog.md](./changelog.md) | Backend development history |

---

*Last updated: 2026-02-26 — ~3302 tests across ~162 files (dual-stack: core + core-v2 + extensions)*
