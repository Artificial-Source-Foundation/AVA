# Components

> Module breakdown of packages/core/

---

## Agent System

### Agent Loop (`agent/`)
The autonomous execution loop. Receives a goal, iterates tool calls until done or limit hit.
- `loop.ts` — AgentExecutor with doom loop detection (3x repeated calls)
- `recovery.ts` — Error recovery with retry
- `planner.ts` — Goal decomposition
- `subagent.ts` — SubagentManager for spawning Junior Devs
- `modes/plan.ts` — Plan mode (read-only tool restrictions)
- `prompts/` — System prompts with model-specific variants (Claude XML, GPT native, Gemini)

### Commander (`commander/`)
Hierarchical delegation — Team Lead → Senior Leads → Junior Devs.
- `registry.ts` — WorkerRegistry with phone book (LLM-based routing)
- `executor.ts` — Worker execution with recursion prevention
- `tool-wrapper.ts` — Creates delegate_* tools for each worker type
- `workers/definitions.ts` — 5 built-in worker types (coder, tester, reviewer, researcher, documenter)
- `parallel/` — Batch execution, DAG scheduler, conflict detection, semaphore

### Validator (`validator/`)
QA verification pipeline, runs after worker completion.
- `syntax.ts`, `typescript.ts` — Language validators
- `lint.ts`, `test.ts`, `build.ts` — Tool validators
- `self-review.ts` — LLM-based code review

---

## Tools (22)

All tools use `defineTool()` with Zod schema validation.

| Category | Tools |
|----------|-------|
| File ops | read_file, create_file, write_file, delete_file, edit, apply_patch, multiedit, glob, grep, ls |
| Execution | bash (PTY, requires_approval), batch |
| Agents | task (spawn subagents), attempt_completion |
| Planning | plan_enter, plan_exit |
| User interaction | question |
| Session | todoread, todowrite |
| Web | websearch (Tavily/Exa), webfetch (HTML→Markdown) |
| Browser | browser (Puppeteer: click, type, scroll, screenshot) |
| Intelligence | codesearch, skill |

---

## Intelligence

### Codebase Understanding (`codebase/`)
- File indexer with symbol extraction
- Dependency graph with cycle detection
- PageRank-based file ranking
- Repo map generation with token budgets

### Context Management (`context/`)
- Token tracking per message
- Auto-compaction when approaching limit
- Strategies: sliding-window, hierarchical, tool-truncation, split-point, verified-summarize

### Memory (`memory/`)
- Episodic: session recordings and summaries
- Semantic: learned facts via vector similarity
- Procedural: recognized patterns
- Consolidation: decay, merge, promote

### LSP (`lsp/`)
CLI-based diagnostics for TypeScript, Python, Go, Rust, Java.

---

## Extensibility

### Extensions (`extensions/`)
Plugin install, enable, disable, reload. Manifest-based.

### Custom Commands (`commands/`)
TOML-defined commands with parameters.

### Hooks (`hooks/`)
Lifecycle hooks: PreToolUse, PostToolUse, TaskStart, TaskComplete, TaskCancel.
Discovery from `~/.estela/hooks/` and `.estela/hooks/`.

### Skills (`skills/`)
Auto-invoked knowledge modules matching file glob patterns.

### MCP (`mcp/`)
MCP protocol client with multi-server registry and tool bridge.

---

## Safety

### Permissions (`permissions/`)
13 built-in rules, risk assessment, auto-approval with path-aware checks, yolo mode.

### Policy Engine (`policy/`)
Priority-based rules with wildcards and regex matching.

### Trusted Folders (`trust/`)
Per-folder security levels.

---

## Infrastructure

### LLM Providers (`llm/`)
12+ providers: Anthropic, OpenAI, Google, Mistral, Groq, DeepSeek, xAI, Cohere, Together, Ollama, OpenRouter, GLM, Kimi.

### Config (`config/`)
Settings management with Zod validation, credentials storage, export/import.

### Session (`session/`)
State management with checkpoints, forking, file-based persistence, resume by ID.

### Auth (`auth/`)
OAuth + PKCE flow for provider authentication.

### Message Bus (`bus/`)
Pub/sub with correlation IDs and tool confirmation flow.
