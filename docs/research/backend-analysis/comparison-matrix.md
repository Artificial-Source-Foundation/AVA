# AVA vs Competitors: Comprehensive Comparison Matrix

> Generated from deep backend analysis of 12 AI coding tools.
> Last updated: 2026-03-03

---

## Table of Contents

1. [Tool Comparison Matrix](#1-tool-comparison-matrix)
2. [Architecture Comparison](#2-architecture-comparison)
3. [Feature Gap Analysis](#3-feature-gap-analysis)
4. [What AVA Does Better](#4-what-ava-does-better)
5. [Recommended Backlog Items](#5-recommended-backlog-items)

---

## 1. Tool Comparison Matrix

Rows = tool categories. Columns = each project + AVA. Cell = tool name(s) or `---` if absent.

### 1.1 File Operations

| Tool Category | AVA | Goose | OpenCode | Cline | Aider | Codex CLI | Gemini CLI | Continue | OpenHands | SWE-agent | Zed | Pi Mono | Plandex |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| **Read file** | `read_file` | --- | `read_file` | `read_file` | (chat parse) | `read_file` | `read_file` | `readFile` | `read` | `open` | `read_file` | `read_file` | (server-side) |
| **Write file** | `write_file` | `write` | `write_file` | `write_to_file` | (edit formats) | `write_file` | `write_file` | `writeFile` | `write` | `create` | `create_file` | `write_file` | `plandex tell/build` |
| **Edit file** | `edit` (8 strategies) | `edit` | `edit_file` (9 strategies) | `replace_in_file` | 13 edit formats | `apply_patch` | `edit_file` | `editFile` | `edit` | `edit` (windowed) | `edit_file` (StreamingDiff) | `edit_file` | LLM-generated diffs |
| **Create file** | `create_file` | --- | --- | --- | (auto) | --- | --- | --- | --- | `create` | `create_file` | --- | (auto) |
| **Delete file** | `delete_file` | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
| **Apply patch/diff** | `apply_patch` | --- | `patch` | --- | `udiff`, `diff` | `apply_patch` | --- | `applyDiff` | --- | `apply_patch` | --- | --- | (auto) |
| **Multi-file edit** | `multiedit` | --- | --- | --- | (auto) | --- | --- | --- | --- | --- | --- | --- | concurrent build |
| **Directory listing** | `ls` | --- | `ls` | `list_files` | --- | `list_dir` | `list_dir` | `listDir` | --- | --- | `list_directory` | --- | --- |

### 1.2 Search & Navigation

| Tool Category | AVA | Goose | OpenCode | Cline | Aider | Codex CLI | Gemini CLI | Continue | OpenHands | SWE-agent | Zed | Pi Mono | Plandex |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| **Glob/find** | `glob` | --- | `glob` | `list_files` (recursive) | --- | `glob` | `glob` | `readFile` (glob) | `glob` | `find_file` | `read_file` (glob) | `glob` | --- |
| **Grep/search** | `grep` | --- | `grep` | `search_files` | (chat) | `grep` | `grep` | `grep` | --- | `search_dir` | `grep` | `grep` | --- |
| **Repo map** | `repo_map` (PageRank) | --- | --- | --- | `repo_map` (PageRank) | --- | --- | --- | --- | --- | --- | --- | --- |
| **Codebase search** | `codesearch` (Exa) | --- | --- | --- | --- | --- | `google_search` (grounding) | `code_search` | --- | --- | --- | --- | --- |
| **Web search** | `websearch` (DDG/Tavily/Exa) | --- | --- | `web_search` | --- | --- | `google_search` | --- | `web_search` (BrowserGym) | --- | --- | `web_search` | --- |
| **Web fetch** | `webfetch` | --- | --- | `web_fetch` | --- | --- | --- | --- | `browse_url` | --- | --- | `web_fetch` | --- |
| **Symbol search** | `lsp_workspace_symbols` | --- | --- | --- | (tree-sitter) | --- | --- | --- | --- | --- | `symbols` | --- | (tree-sitter) |
| **Full-text recall** | `recall` (FTS5) | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |

### 1.3 Shell & Execution

| Tool Category | AVA | Goose | OpenCode | Cline | Aider | Codex CLI | Gemini CLI | Continue | OpenHands | SWE-agent | Zed | Pi Mono | Plandex |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| **Shell command** | `bash` | `shell` | `bash` | `execute_command` | (run cmd) | `shell` | `run_shell_command` | `runTerminalCommand` | `bash` | `bash` (default) | `terminal` | `bash` | --- |
| **Background shell** | `bash_background` / `bash_output` / `bash_kill` | --- | --- | --- | --- | --- | --- | --- | --- | `bg_bash` | --- | --- | --- |
| **PTY/interactive** | `pty` | --- | --- | --- | --- | `container_exec` (PTY) | --- | --- | --- | --- | `terminal` | --- | --- |
| **Sandboxed exec** | `sandbox` (Docker) | --- | --- | --- | --- | OS-level (Seatbelt/bwrap) | --- | --- | Docker (EventStream) | SWE-ReX | --- | --- | --- |

### 1.4 Git & Version Control

| Tool Category | AVA | Goose | OpenCode | Cline | Aider | Codex CLI | Gemini CLI | Continue | OpenHands | SWE-agent | Zed | Pi Mono | Plandex |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| **Create branch** | `create_branch` | --- | --- | --- | (auto) | --- | --- | --- | --- | --- | --- | --- | (server-side) |
| **Switch branch** | `switch_branch` | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | `plandex checkout` |
| **Create PR** | `create_pr` (gh) | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
| **Read issue** | `read_issue` (gh) | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
| **Checkpoints/snapshots** | git snapshots | --- | separate git repo | shadow git | auto-commits | ghost snapshots | --- | --- | --- | --- | --- | --- | server-side git |
| **Worktrees** | git worktrees | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |

### 1.5 Agent & Planning

| Tool Category | AVA | Goose | OpenCode | Cline | Aider | Codex CLI | Gemini CLI | Continue | OpenHands | SWE-agent | Zed | Pi Mono | Plandex |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| **Subagent/delegate** | `delegate_*` (6 workers) | --- | `build`/`plan`/`explore` agents | `spawn_subagent` | --- | `spawn_agent` | --- | --- | `AgentDelegator` | --- | --- | --- | subtask system |
| **Plan mode** | `plan_enter`/`plan_exit` | --- | `plan` agent | `plan_mode_respond` | --- | --- | --- | --- | --- | --- | --- | --- | `plandex tell` |
| **Todo/task list** | `todoread`/`todowrite` | --- | --- | `checkpoints` | --- | --- | `memory_save/load` | --- | --- | --- | --- | --- | subtasks |
| **Question/ask user** | `question` | --- | --- | `ask_followup_question` | (chat) | --- | --- | --- | `question` | --- | --- | `ask_question` | --- |
| **Attempt completion** | `attempt_completion` | --- | --- | `attempt_completion` | --- | --- | --- | --- | --- | --- | --- | `attempt_completion` | --- |

### 1.6 Context & Memory

| Tool Category | AVA | Goose | OpenCode | Cline | Aider | Codex CLI | Gemini CLI | Continue | OpenHands | SWE-agent | Zed | Pi Mono | Plandex |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| **Persistent memory** | `memory_read/write/list/delete` | --- | --- | --- | --- | --- | `memory_save/load` | --- | --- | --- | --- | --- | --- |
| **Session recall** | `recall` (FTS5) | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
| **Context compaction** | token compaction | MOIM injection | --- | sliding window | --- | --- | 1M native | --- | 9 condenser strategies | history processors | --- | --- | (server-side) |
| **Skills/knowledge** | `load_skill` (auto-invoke) | recipes | skills | --- | --- | skills + .rules | skills | --- | microagents | --- | agent profiles | skills | --- |

### 1.7 Language Server & IDE Integration

| Tool Category | AVA | Goose | OpenCode | Cline | Aider | Codex CLI | Gemini CLI | Continue | OpenHands | SWE-agent | Zed | Pi Mono | Plandex |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| **LSP diagnostics** | `lsp_diagnostics` | --- | --- | (VS Code API) | --- | --- | --- | (VS Code API) | --- | --- | native | --- | --- |
| **LSP hover** | `lsp_hover` | --- | --- | (VS Code API) | --- | --- | --- | (VS Code API) | --- | --- | native | --- | --- |
| **LSP go-to-def** | `lsp_definition` | --- | --- | (VS Code API) | --- | --- | --- | (VS Code API) | --- | --- | native | --- | --- |
| **LSP references** | `lsp_references` | --- | --- | (VS Code API) | --- | --- | --- | (VS Code API) | --- | --- | native | --- | --- |
| **LSP rename** | `lsp_rename` | --- | --- | (VS Code API) | --- | --- | --- | (VS Code API) | --- | --- | native | --- | --- |
| **LSP completions** | `lsp_completions` | --- | --- | (VS Code API) | --- | --- | --- | (VS Code API) | --- | --- | native | --- | --- |
| **LSP code actions** | `lsp_code_actions` | --- | --- | (VS Code API) | --- | --- | --- | (VS Code API) | --- | --- | native | --- | --- |
| **LSP doc symbols** | `lsp_document_symbols` | --- | --- | (VS Code API) | --- | --- | --- | (VS Code API) | --- | --- | native | --- | --- |
| **LSP workspace symbols** | `lsp_workspace_symbols` | --- | --- | (VS Code API) | --- | --- | --- | (VS Code API) | --- | --- | native | --- | --- |

### 1.8 Protocols & Interop

| Tool Category | AVA | Goose | OpenCode | Cline | Aider | Codex CLI | Gemini CLI | Continue | OpenHands | SWE-agent | Zed | Pi Mono | Plandex |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| **MCP client** | full (stdio, SSE, HTTP) | MCP-native | MCP client | MCP client | --- | --- | MCP client | MCP client | --- | --- | MCP client+server | --- | --- |
| **ACP / A2A** | ACP REST server | --- | --- | --- | --- | --- | A2A protocol | --- | --- | --- | --- | RPC mode | --- |
| **Browser automation** | --- (Puppeteer via MCP) | --- | --- | Puppeteer | --- | --- | browser agent | --- | BrowserGym | --- | --- | --- | --- |

### 1.9 Tool Count Summary

| Project | Core Tools | Extended Tools | Total Tools |
|---|---|---|---|
| **AVA** | 7 | 48+ | **55+** |
| Goose | 4 | 0 (MCP-provided) | **4** (+ MCP) |
| OpenCode | ~10 | ~10 | **~20** |
| Cline | ~15 | ~10 | **~25** |
| Aider | 0 (chat-parse) | 0 | **0** (edit formats) |
| Codex CLI | ~10 | ~5 | **~15** |
| Gemini CLI | ~10 | ~7 | **~17** |
| Continue | ~12 | ~8 | **~20** |
| OpenHands | ~7 | ~5 | **~12** |
| SWE-agent | ~8 | ~5 (per bundle) | **~13** |
| Zed | ~12 | ~6 | **~18** |
| Pi Mono | ~7 | ~3 | **~10** |
| Plandex | 0 (client-server) | 0 | **0** (implicit) |

---

## 2. Architecture Comparison

### 2.1 Agent Loop Pattern

| Project | Pattern | Key Details |
|---|---|---|
| **AVA** | **Turn-based loop (~730 lines)** | Stream LLM → collect tool calls → run middleware → execute → repeat. Supports repair, output-files, structured-output, efficient-results. Extensions intercept via middleware and events. |
| Goose | Turn-based (Rust) | Async tool execution per message. No repair loop. Simple collect-and-run pattern. |
| OpenCode | Turn-based (Vercel AI SDK) | `generateText` / `streamText` loop with `maxSteps`. Clean but SDK-coupled. |
| Cline | Monolithic Task class (3547 lines) | XML/native dual tool modes. Recursive API calls. `presentAssistantMessage()` orchestrates tool execution. Approval gates on every tool. |
| Aider | Chat-and-parse | Not tool-calling. Sends messages, parses edit blocks from markdown. Architect mode: two-model pipeline (think → edit). |
| Codex CLI | Turn-based (Rust) | Responses API streaming. Hardcoded tool schemas. Parallel function calls. Full async with cancellation. |
| Gemini CLI | Sequential turn-based | One tool at a time (no parallel). 5-turn hard limit. DeclarativeTool pattern wraps tool definition + execution. |
| Continue | GUI-driven step loop | IDE sends user action → core processes → returns tool calls → IDE confirms → execute. Three-process architecture (IDE ↔ Core ↔ LLM). |
| OpenHands | Event-sourced | EventStream → Agent.step() → Actions. Controller handles Action→Observation cycle. Decoupled from execution runtime. |
| SWE-agent | ACI (Agent-Computer Interface) | Bash scripts as tools. 10 output parsing formats. History processor pipeline filters what LLM sees. |
| Zed | Thread-based | Thread accumulates messages. LLM streams → tool calls collected → executed → appended → loop. EditAgent sub-agent handles multi-file edits with StreamingDiff. |
| Pi Mono | 3-layer loop | `ai` (raw LLM) → `agent` (tool loop) → `coding-agent` (file context). Clean separation. Supports abort, configurable maxSteps. |
| Plandex | Tell/build pipeline | `tell` = planning phase (LLM generates plan). `build` = execution phase (concurrent file builds). Separated by design. |

### 2.2 Edit Strategy

| Project | Strategies | Best-in-Class Features |
|---|---|---|
| **AVA** | **8 strategies** | Fuzzy matching, line-range, search-replace, regex, block, indent-aware, AST-aware, whole-file. Configurable per-model. |
| Goose | 1 (whole-file write) | Minimal — just `write` and `edit` tools with no strategy selection. |
| OpenCode | **9 strategies** | Similar to AVA: diff, udiff, whole, search-replace, patch, block, linediff, aider-diff, aider-udiff. Benchmark harness included. |
| Cline | 1 (search-replace) | `replace_in_file` with regex. Simple but effective. |
| Aider | **13 edit formats** | Industry-leading variety: whole, diff, udiff, editor-diff, editor-whole, architect variants. Format auto-selection per model. |
| Codex CLI | 1 (apply_patch) | Unified diff format only. |
| Gemini CLI | 1 (whole-file) | `edit_file` replaces entire file content. |
| Continue | 2 (whole, diff) | `editFile` with streaming diff application. |
| OpenHands | 1 (whole-file) | `edit` tool with line-range replacement. |
| SWE-agent | 1 (windowed edit) | `edit` command with line numbers in 100-line viewing window. |
| Zed | 2 (whole, streaming diff) | **StreamingDiff** — applies changes as LLM streams. EditAgent handles multi-file. Per-hunk accept/reject in UI. |
| Pi Mono | 1 (whole-file) | `edit_file` with line-range specification. |
| Plandex | 1 (LLM-generated diff) | Tree-sitter validation + LLM fix loop for failed applies. |

### 2.3 Context / Token Management

| Project | Strategy | Key Details |
|---|---|---|
| **AVA** | **Token tracking + compaction + prune** | Tracks usage across turns. Compaction extension summarizes old messages. Prune strategy drops least-relevant. |
| Goose | MOIM context injection | Injects project context/instructions per-message. No explicit token tracking. |
| OpenCode | Minimal | Relies on Vercel AI SDK's built-in handling. No explicit compaction. |
| Cline | Sliding window | Truncates conversation history. Keeps system prompt + recent messages. Token counting per-message. |
| Aider | **Repo map + smart send** | Only sends relevant file content based on tree-sitter + PageRank ranked tags. Minimizes tokens by default. |
| Codex CLI | Rollup/compaction | Condenses older messages into summaries. Ghost snapshots for file state. |
| Gemini CLI | **1M native context** | Leverages Gemini's massive context window. Dual loop detection (heuristic + LLM) to avoid wasted tokens. |
| Continue | Context providers | Extensible context system — files, symbols, URLs, custom. Autocomplete uses prefix/suffix windowing. |
| OpenHands | **9 condenser strategies** | Recent, LLM-summarize, amortized, observation-masking, structured, hybrid, browser-turn, identity, no-op. Most sophisticated. |
| SWE-agent | History processors pipeline | Chain of filters: `DefaultHistoryProcessor`, `LastNObservations`, `ClosedWindowHistoryProcessor`. Transforms what LLM sees. |
| Zed | Thread-based accumulation | Full thread kept in context. No explicit compaction documented. |
| Pi Mono | Token counting | Tracks token usage. No explicit compaction strategy. |
| Plandex | Server-side | Context managed on server. Plans broken into subtasks to keep individual contexts small. |

### 2.4 Session Management

| Project | Format | Key Features |
|---|---|---|
| **AVA** | **JSON with DAG/branching** | Session CRUD + auto-save + archival + slug + busy state. DAG structure supports branching conversations. FTS5 recall across sessions. |
| Goose | SQLite | Relational storage. Session list/resume. |
| OpenCode | **JSONL with DAG/tree** | Drizzle ORM + SQLite. Sessions have tree structure for branching. |
| Cline | JSON (VS Code state) | Task-based. Stored in VS Code globalState. Resume support. |
| Aider | In-memory | Chat history in memory. Git commits serve as implicit session markers. |
| Codex CLI | **JSONL** | Append-only log. Supports session resume. |
| Gemini CLI | JSON | Memory save/load tools for cross-session persistence. |
| Continue | JSON | IDE-managed session state. Core is stateless between requests. |
| OpenHands | EventStream (append-only) | Event-sourced. Full replay capability. |
| SWE-agent | Trajectories (JSON) | Full trajectory logging for SWE-bench evaluation. |
| Zed | Thread (in-memory + DB) | Threads stored in database. Full message history. |
| Pi Mono | **JSONL with DAG/tree** | Tree-structured sessions with parent references. Similar to OpenCode. |
| Plandex | **PostgreSQL** | Server-side relational storage. Branch-based plan management. Full history. |

### 2.5 Permission / Safety Model

| Project | Approach | Key Details |
|---|---|---|
| **AVA** | **Middleware pipeline** | Permissions extension at priority 0. Bash parsing + arity fingerprinting. Tool middleware intercepts before execution. YOLO mode bypasses. |
| Goose | 3-layer inspection | `InspectionFilter` → `InspectionPipeline` → `InspectionResult` (Proceed/Deny/Confirm/Filter). Modular. |
| OpenCode | Tree-sitter bash parsing | Parses bash commands into AST. Checks against allowlist/denylist. |
| Cline | Per-tool approval gates | Every tool call requires user approval in VS Code. Auto-approve mode available. |
| Aider | Minimal | No sandboxing. Relies on git auto-commits for rollback. |
| Codex CLI | **OS-level sandboxing** | Seatbelt (macOS), bwrap/Landlock (Linux), seccomp. Managed network proxy. Most secure. |
| Gemini CLI | **Policy engine + safety checkers** | `AllowedToolsChecker`, `ConfirmationChecker`, `SafetyChecker`. Composable policy framework. |
| Continue | Tool policy system | per-tool allow/deny/ask. YAML-configured. |
| OpenHands | Docker isolation | All execution in Docker containers. Multiple runtime types (EventStream, E2B, modal). |
| SWE-agent | SWE-ReX | Remote execution via SWE-ReX. Isolated from host. |
| Zed | Capability-based | Tools declare required capabilities. User grants per-session. |
| Pi Mono | Minimal | Basic command filtering. No sandboxing. |
| Plandex | Server-side isolation | All execution on server. Client is display-only. |

### 2.6 Extension / Plugin System

| Project | Architecture | Key Details |
|---|---|---|
| **AVA** | **ExtensionAPI (same API for built-in + community)** | `registerTool`, `registerCommand`, `registerProvider`, `registerAgentMode`, `addToolMiddleware`, `registerHook`, `registerValidator`. Disposable pattern. Per-extension storage. Plugin scaffold CLI. |
| Goose | **MCP-native** | Extensions ARE MCP servers. No separate plugin API — everything is MCP. Simplest model. |
| OpenCode | npm packages + hooks | Plugin system with `onToolCall`, `onMessage`, `onError` hooks. Skills auto-loaded from directories. |
| Cline | VS Code extension API | Hooks system. @mentions for context injection. Not a standalone plugin API. |
| Aider | None | No plugin system. Customization via `.aider.conf.yml` and in-chat commands. |
| Codex CLI | Starlark `.rules` | Configuration-as-code. Not a full plugin system — rules modify behavior but can't add tools. |
| Gemini CLI | Extensions (multi-tool) | Extensions register multiple tools. Can define their own configuration. `INK_EXTENSION_API_KEY` pattern. |
| Continue | **YAML config + context providers** | `config.yaml` for models, context providers, slash commands. MCP for tool extensions. IDE-integrated. |
| OpenHands | Microagents | Markdown files that inject specialized instructions. Not a code-level plugin API. |
| SWE-agent | Tool bundles | Bash scripts grouped into bundles. Configurable per-run. Not installable plugins. |
| Zed | **Agent profiles + MCP** | Slash commands, context servers, tool-use providers. Profiles bundle model + tools + instructions. |
| Pi Mono | **Rich extension system (25+ events)** | `onBeforeToolCall`, `onAfterToolCall`, `onMessage`, etc. Tool registration, provider registration, skill system. Closest to AVA's model. |
| Plandex | None | No plugin system. Customization via model packs only. |

### 2.7 Provider Count & Support

| Project | Provider Count | Notable |
|---|---|---|
| **AVA** | **16 providers** | Anthropic, OpenAI, Google, Azure, AWS Bedrock, Groq, Together, Fireworks, DeepSeek, Mistral, Ollama, LM Studio, OpenRouter, LiteLLM, xAI, custom OpenAI-compat |
| Goose | **20+ providers** | All via Rust LLM SDK. Most providers of any tool. |
| OpenCode | ~10 | Via Vercel AI SDK. OpenAI, Anthropic, Google, etc. |
| Cline | **40+ providers** | Most variety. Includes niche providers. OpenRouter integration. |
| Aider | **20+ via litellm** | litellm abstracts 100+ providers. Model metadata YAML. |
| Codex CLI | 1 (OpenAI only) | Responses API only. Hardcoded to OpenAI. |
| Gemini CLI | 1 (Google only) | Gemini models only. Vertex AI support. |
| Continue | **30+ via openai-adapters** | Custom adapter layer. Autocomplete-specific providers. |
| OpenHands | **20+ via litellm** | Same litellm approach as Aider. |
| SWE-agent | ~5 via litellm | Focused on benchmark models. |
| Zed | **14 providers** | Native integrations (not litellm). Provider-specific features. |
| Pi Mono | **22 providers via 9 API protocols** | Most protocol variety: OpenAI, Anthropic, Google, Mistral, Cohere, Ollama, OpenRouter, Azure, AWS |
| Plandex | ~10 via LiteLLM sidecar | LiteLLM as separate process. 9 model roles with different providers per role. |

### 2.8 MCP Support

| Project | MCP Role | Details |
|---|---|---|
| **AVA** | **Client** (stdio, SSE, HTTP streaming) | Full MCP client with tools, resources, prompts, sampling. Reconnection + OAuth. |
| Goose | **Native** | Extensions ARE MCP servers. Most MCP-integrated tool. |
| OpenCode | Client | MCP client support. |
| Cline | Client | MCP client for tool extensions. |
| Aider | --- | No MCP support. |
| Codex CLI | --- | No MCP support. |
| Gemini CLI | Client | MCP client support. |
| Continue | Client | MCP for tool extensions. Well-integrated. |
| OpenHands | --- | No MCP support. |
| SWE-agent | --- | No MCP support. |
| Zed | **Client + Server** | Both MCP client (consume tools) and MCP server (expose Zed to other tools). Unique dual role. |
| Pi Mono | --- | No MCP support documented. |
| Plandex | --- | No MCP support. |

### 2.9 Git Integration Depth

| Project | Depth | Key Features |
|---|---|---|
| **AVA** | **Deep** | Branch create/switch, PR creation (gh), issue reading, git snapshots, checkpoints, worktrees, file watcher for .git/HEAD. |
| Goose | None | No git tools at all. |
| OpenCode | Medium | Snapshots via separate git repo (avoids polluting project history). |
| Cline | Medium | Shadow git checkpoints. Auto-commit on task completion. |
| Aider | **Deepest** | Auto-commits every edit. Commit message generation. Diff display. `.gitignore` awareness. Core to workflow. |
| Codex CLI | Medium | Ghost snapshots (invisible to user). Used for rollback. |
| Gemini CLI | None | No git integration. |
| Continue | Minimal | Basic git awareness. |
| OpenHands | None | No git integration (Docker-isolated). |
| SWE-agent | None | Operates in isolated environments. |
| Zed | Medium | Git-aware editor. Diff display. Branch management via editor. |
| Pi Mono | None | No git tools. |
| Plandex | **Deep (server-side)** | Server-side git repos. Branch-based plan management. Version control for plans. |

### 2.10 Sandbox / Isolation

| Project | Type | Details |
|---|---|---|
| **AVA** | **Docker (optional)** | Sandbox extension for Docker-based execution. Not required. |
| Goose | None | No sandboxing. |
| OpenCode | None | No sandboxing. Permission checks only. |
| Cline | None | VS Code extension — runs in user's environment. |
| Aider | None | Direct execution. Git rollback as safety net. |
| Codex CLI | **OS-level (strongest)** | Seatbelt (macOS), bwrap + Landlock (Linux), seccomp. Managed network proxy. Gold standard. |
| Gemini CLI | None | Safety checkers but no execution isolation. |
| Continue | None | IDE-managed execution. |
| OpenHands | **Docker (mandatory)** | All code runs in containers. Multiple runtime types. Core differentiator. |
| SWE-agent | **SWE-ReX (remote)** | Remote execution engine. Complete isolation. |
| Zed | None | Editor-integrated. Capability-based access control. |
| Pi Mono | None | No sandboxing. |
| Plandex | **Server-side** | All execution on server. Client never runs code. |

### 2.11 Multi-Agent / Delegation

| Project | Architecture | Details |
|---|---|---|
| **AVA** | **3-tier hierarchy (Praxis)** | Commander (Team Lead) → Senior Leads (5 domains) → Junior Devs. 13 built-in agents. `delegate_*` tools. Most structured. |
| Goose | None | Single agent only. |
| OpenCode | Flat agents | `build`, `plan`, `explore`, custom agents. No hierarchy. |
| Cline | Subagents | `spawn_subagent` tool. Flat spawning, no hierarchy. |
| Aider | Dual-model | Architect mode: strong model plans, weak model edits. Not multi-agent per se. |
| Codex CLI | Flat spawn | `spawn_agent` tool. Flat, no hierarchy. |
| Gemini CLI | None | Single agent. No delegation. |
| Continue | None | Single agent loop. |
| OpenHands | Delegator | `AgentDelegator` agent type. Can delegate to specialized agents. |
| SWE-agent | Reviewer loop | Optional reviewer agent validates output. Retry on failure. |
| Zed | Sub-agent | EditAgent as specialized sub-agent for multi-file edits. |
| Pi Mono | None | Single agent. |
| Plandex | Subtask system | Plans decomposed into subtasks. Concurrent execution. Not autonomous agents. |

### 2.12 Dual-Model / Routing Support

| Project | Support | Details |
|---|---|---|
| **AVA** | **Yes — per-role model selection** | Different models per Praxis role (Commander, Leads, Workers). Provider + model configurable per agent. |
| Goose | **Yes — Lead-Worker provider** | "Lead" model for reasoning, "Worker" model for execution. Built into provider abstraction. |
| OpenCode | Yes — per-agent | Different model per agent type (build, plan, explore). |
| Cline | Yes — manual | User can configure different models. No automatic routing. |
| Aider | **Yes — Architect mode** | Strong model (architect) generates plan, weak model (editor) applies edits. Most explicit dual-model. |
| Codex CLI | No | Single model (OpenAI only). |
| Gemini CLI | No | Single model (Gemini only). |
| Continue | Yes — per-role | Different models for chat, autocomplete, edit. Tab autocomplete uses fast model. |
| OpenHands | Yes — via litellm | Can configure different models but no built-in routing. |
| SWE-agent | Yes — reviewer | Different model for reviewer agent. |
| Zed | Yes — per-profile | Agent profiles can specify different models. |
| Pi Mono | Yes — configurable | Per-request model selection. No automatic routing. |
| Plandex | **Yes — 9 model roles** | Most granular: planner, coder, namer, committer, summarizer, auto-continue, verifier, builder, wholeFile. Different model per role. |

---

## 3. Feature Gap Analysis

Features are prioritized by how many competitors implement them:
- **P0** = 6+ competitors have it (table stakes)
- **P1** = 3–5 competitors have it (competitive advantage)
- **P2** = 1–2 competitors have it (unique differentiator to steal)

### P0 — Table Stakes (AVA has all)

| Feature | Competitors with it | AVA Status |
|---|---|---|
| File read/write/edit | 12/12 | **Done** — 8 edit strategies |
| Shell execution | 11/12 | **Done** — bash + PTY + background |
| Glob/grep search | 10/12 | **Done** — glob + grep |
| Web search/fetch | 7/12 | **Done** — DDG/Tavily/Exa + webfetch |
| MCP client | 7/12 | **Done** — stdio, SSE, HTTP streaming |
| Multi-provider support | 11/12 | **Done** — 16 providers |
| Extension/plugin system | 8/12 | **Done** — ExtensionAPI, plugin scaffold |
| Plan mode | 7/12 | **Done** — plan_enter/exit |
| Git checkpoints | 6/12 | **Done** — snapshots, worktrees |
| Context management | 9/12 | **Done** — compaction + prune |

### P1 — Competitive Advantages (AVA has most)

| Feature | Competitors with it | AVA Status | Gap? |
|---|---|---|---|
| Multi-agent/delegation | 5/12 (OpenCode, Cline, Codex, OpenHands, Plandex) | **Done** — Praxis 3-tier, most sophisticated | No |
| Dual-model routing | 5/12 (Goose, Aider, Continue, OpenCode, Plandex) | **Done** — per-role model selection | No |
| Persistent memory | 3/12 (Gemini CLI, Pi Mono *, OpenCode *) | **Done** — memory CRUD + auto-learning | No |
| Repo map / code graph | 3/12 (Aider, AVA, Zed symbols) | **Done** — PageRank repo map | No |
| Skills / auto-invoked knowledge | 5/12 (Goose, OpenCode, Codex, Gemini, OpenHands) | **Done** — auto-invoke by file globs | No |
| Session branching/DAG | 3/12 (OpenCode, Pi Mono, Plandex) | **Done** — DAG session structure | No |
| Sandbox/isolation | 4/12 (Codex, OpenHands, SWE-agent, Plandex) | **Partial** — Docker extension exists but optional | **Minor gap** |
| Browser automation | 3/12 (Cline, Gemini, OpenHands) | **Via MCP** — Puppeteer MCP server | No (design choice) |
| Autocomplete / inline edit | 3/12 (Continue, Zed, Aider watch) | **Missing** | **Gap** |
| Edit benchmarking harness | 2/12 (OpenCode, Aider) | **Missing** | **Gap** |
| Loop detection / stuck detection | 3/12 (Gemini dual-loop, OpenHands 5-scenario, SWE-agent retry) | **Partial** — doom loop extension | **Minor gap** |
| Safety policy framework | 3/12 (Gemini, Continue, Goose) | **Partial** — middleware pipeline, no declarative policy | **Minor gap** |

### P2 — Unique Differentiators Worth Stealing

| Feature | Who Has It | What It Does | AVA Status | Priority |
|---|---|---|---|---|
| **OS-level sandboxing** | Codex CLI | Seatbelt/bwrap/Landlock kernel-level isolation | Missing | Medium |
| **Streaming diff application** | Zed | Applies edits AS the LLM streams tokens, not after | Missing | High |
| **Per-hunk accept/reject** | Zed | User reviews each diff hunk individually | Missing | High |
| **Edit prediction (Zeta)** | Zed | Predicts next edit before user asks | Missing | Low |
| **Event-sourced architecture** | OpenHands | Full event replay, time-travel debugging | Different approach (DAG) | Low |
| **9 condenser strategies** | OpenHands | Most sophisticated context compaction | Partial (1 strategy) | Medium |
| **History processors pipeline** | SWE-agent | Chain of filters transforms what LLM sees | Missing | Medium |
| **Action samplers (best-of-N)** | SWE-agent | Generate N responses, pick best | Missing | Low |
| **Google Search grounding** | Gemini CLI | Real-time web grounding in responses | Different (websearch tool) | Low |
| **Agent profiles** | Zed | Bundled model + tools + instructions per profile | Similar (Praxis roles) | Low |
| **Model packs (9 roles)** | Plandex | Different model per role (9 roles) | Partial (per-agent) | Medium |
| **Concurrent build pipeline** | Plandex | Build multiple files simultaneously | Missing | Medium |
| **Voice coding** | Aider | Voice-to-code input | Missing | Low |
| **AI comment watcher** | Aider | Watches files for `# AI: do X` comments | Missing | Low |
| **Dual loop detection** | Gemini CLI | Heuristic check + LLM self-assessment | Partial (doom loop) | Medium |
| **A2A protocol** | Gemini CLI | Agent-to-Agent standard protocol | Have ACP (similar) | Low |
| **MCP server mode** | Zed | Expose own tools to other MCP clients | Missing | Medium |
| **Separate snapshot repo** | OpenCode | Git snapshots in separate repo, no project pollution | Different (in-repo) | Low |
| **3-layer inspection** | Goose | Structured Proceed/Deny/Confirm/Filter pipeline | Similar (middleware) | Low |

---

## 4. What AVA Does Better

### 4.1 Most Comprehensive Tool Suite (55+)

AVA has **55+ tools** — more than any competitor by a significant margin. The next closest is Cline with ~25. AVA's tool coverage spans 9 categories including LSP integration (9 tools), full git workflow (branch, PR, issue, worktree), persistent memory (4 tools), background shell management (3 tools), and session recall (FTS5). No other tool comes close to this breadth.

### 4.2 Richest Extension API

AVA's `ExtensionAPI` is the most complete extension surface of any competitor:
- `registerTool` — add tools (same as Pi Mono, OpenCode)
- `registerProvider` — add LLM providers (unique among non-IDE tools)
- `registerAgentMode` — add agent behaviors (unique)
- `registerValidator` — add QA checks (unique)
- `addToolMiddleware` — intercept tool calls at priority levels (unique)
- `registerHook` / `callHook` — sequential chaining pipelines (unique)
- `registerCommand` — slash commands (shared with Zed)
- Per-extension private storage (unique)
- Plugin scaffold CLI (unique among standalone tools)

Goose's MCP-native model is simpler but less capable. Pi Mono's 25+ event system is closest but lacks middleware priority, validators, and agent modes.

### 4.3 Most Structured Multi-Agent System

AVA's Praxis hierarchy is the most sophisticated delegation system:
- **3 tiers**: Commander → Senior Leads → Junior Devs (vs. flat spawning in Codex, Cline, OpenCode)
- **13 built-in agents** with domain specialization (vs. 3-4 in OpenCode, 1 delegator in OpenHands)
- **6 delegation tools** with typed interfaces (vs. generic `spawn_agent` in competitors)
- **Named roles**: Frontend Lead, Backend Lead, Tester, Reviewer, Researcher, Debugger, Explorer

No competitor has hierarchical delegation with domain specialization.

### 4.4 LSP as Agent Tools (9 Tools)

AVA exposes **9 LSP tools** directly to the agent — diagnostics, hover, go-to-definition, references, rename, code actions, completions, document symbols, workspace symbols. No other standalone tool does this. Cline and Continue access LSP indirectly through VS Code's API, but that's IDE-mediated, not agent-controlled. Zed has native LSP but doesn't expose it as discrete agent tools.

### 4.5 Session Recall Across Conversations

AVA's `recall` tool provides **FTS5 full-text search across all past sessions**. No competitor offers cross-session search. Gemini CLI has `memory_save/load` but it's key-value, not searchable. OpenHands has event replay but only within a session.

### 4.6 Desktop-Native with Full Backend

AVA is the only tool that combines:
- Desktop app (Tauri + SolidJS) — not Electron, not CLI-only
- Full backend with 55+ tools — not a thin wrapper
- Plugin ecosystem — not locked to an IDE
- Multi-provider — not locked to one LLM vendor

Zed is also a desktop app with agent tools, but it's a full IDE (much larger scope, different market).

### 4.7 Edit Strategy Breadth with Flexibility

AVA's 8 edit strategies are configurable per-model, meaning the system automatically picks the best edit format for each LLM. Only Aider (13 formats) has more variety, but Aider uses chat-and-parse (not tool calling). OpenCode has 9 strategies with a benchmark harness (AVA should add this — see backlog).

### 4.8 Unified Hooks + Middleware Architecture

AVA's dual system of hooks (sequential chaining via `callHook`) and middleware (priority-based interception via `addToolMiddleware`) is unique. Other tools have one or the other:
- Goose: inspection pipeline only
- OpenCode: hooks only
- Cline: hooks only
- Gemini CLI: safety checkers only

AVA's approach enables both cross-cutting concerns (permissions at priority 0) and extension-specific logic (formatters, validators).

---

## 5. Recommended Backlog Items

### P0 — Close Gaps That Multiple Competitors Have

| # | Item | Justification | Reference Competitors |
|---|---|---|---|
| 1 | **Streaming diff application** | Apply edits as LLM streams, not after completion. Reduces perceived latency. Zed's StreamingDiff is the gold standard. | Zed |
| 2 | **Per-hunk diff review UI** | Let users accept/reject individual changes, not all-or-nothing. Standard in Zed, expected by power users. | Zed |
| 3 | **Enhanced loop/stuck detection** | Upgrade doom loop extension to multi-signal detection: repeated tool calls, error cycling, token waste detection. Gemini uses heuristic + LLM self-check. OpenHands detects 5 stuck scenarios. | Gemini CLI, OpenHands, SWE-agent |
| 4 | **Edit strategy benchmark harness** | Test edit strategies against a corpus to find optimal strategy per model. OpenCode has this built-in. Aider does implicit benchmarking. Critical for the 8-strategy system. | OpenCode, Aider |
| 5 | **Declarative safety policy framework** | Move from code-only middleware to declarative policy rules (YAML/TOML). Gemini's `AllowedToolsChecker` + `ConfirmationChecker` pattern. Continue's tool policy system. | Gemini CLI, Continue, Goose |

### P1 — Competitive Differentiation

| # | Item | Justification | Reference Competitors |
|---|---|---|---|
| 6 | **Multiple context compaction strategies** | Add LLM-summarize, observation-masking, amortized compaction alongside current strategy. OpenHands has 9 strategies — AVA should have at least 3-4 configurable options. | OpenHands |
| 7 | **History processor pipeline** | Allow extensions to register history transforms that filter/modify what the LLM sees. SWE-agent's chain-of-processors pattern. Could be implemented as a hook pipeline on `beforeLLMCall`. | SWE-agent |
| 8 | **MCP server mode** | Expose AVA's 55+ tools as an MCP server so other tools can use them. Zed does this. Would make AVA a platform, not just a tool. | Zed |
| 9 | **Concurrent multi-file builds** | When editing multiple files, stream edits concurrently. Plandex does this with its build pipeline. AVA's `multiedit` tool runs sequentially. | Plandex |
| 10 | **Model packs / expanded role-based routing** | Extend per-agent model selection to more granular roles (summarizer, committer, namer, verifier). Plandex's 9-role system shows the value. | Plandex |
| 11 | **OS-level sandboxing option** | Add Seatbelt (macOS) and bwrap/Landlock (Linux) as alternatives to Docker. Lighter weight, no Docker dependency. | Codex CLI |

### P2 — Unique Differentiators to Build

| # | Item | Justification | Reference Competitors |
|---|---|---|---|
| 12 | **Action samplers (best-of-N)** | Generate N candidate responses, evaluate, pick best. Improves quality at compute cost. SWE-agent shows significant improvement on benchmarks. | SWE-agent |
| 13 | **AI comment watcher** | Watch project files for special comments (e.g., `// AVA: refactor this`) and auto-trigger agent. Aider's file watcher is popular for this. | Aider |
| 14 | **Voice input mode** | Voice-to-text input for the chat. Aider shows this is popular with vibe coders. Desktop app is well-suited for microphone access. | Aider |
| 15 | **Agent profiles (user-facing)** | Let users create and share bundles of (model + tools + instructions + skills). Similar to Zed's agent profiles but for AVA's Praxis system. | Zed |
| 16 | **Inline edit / autocomplete** | Tab-completion and inline edit suggestions in the desktop app's code view. Continue and Zed both offer this. Would require code editor integration. | Continue, Zed |

### Existing Backlog Items Validated by Competitor Analysis

These items are already on AVA's backlog and are confirmed as high-priority by this analysis:

| Existing Item | Validation |
|---|---|
| LSP frontend UI | 9 backend tools ready, zero frontend components. Zed shows native LSP integration is a major differentiator. |
| Image/vision E2E | OpenHands and Gemini CLI both have vision support. |
| Plugin registry API | OpenCode (npm), Goose (MCP), and Zed (extensions) all have remote plugin registries. |
| MCP tool list change notifications | Standard MCP feature. Goose and Zed both implement this. |
| Edit strategy benchmarks | Directly matches item #4 above. OpenCode has a benchmark harness. |

---

## Appendix: Raw Feature Count by Project

| Dimension | AVA | Goose | OpenCode | Cline | Aider | Codex | Gemini | Continue | OpenHands | SWE-agent | Zed | Pi Mono | Plandex |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| Total tools | 55+ | 4 | ~20 | ~25 | 0 | ~15 | ~17 | ~20 | ~12 | ~13 | ~18 | ~10 | 0 |
| Edit strategies | 8 | 1 | 9 | 1 | 13 | 1 | 1 | 2 | 1 | 1 | 2 | 1 | 1 |
| Providers | 16 | 20+ | ~10 | 40+ | 20+ | 1 | 1 | 30+ | 20+ | ~5 | 14 | 22 | ~10 |
| LSP tools | 9 | 0 | 0 | 0* | 0 | 0 | 0 | 0* | 0 | 0 | native | 0 | 0 |
| Git tools | 6 | 0 | 1 | 1 | 5 | 1 | 0 | 0 | 0 | 0 | native | 0 | 3 |
| Memory tools | 4 | 0 | 0 | 0 | 0 | 0 | 2 | 0 | 0 | 0 | 0 | 0 | 0 |
| Agent hierarchy | 3-tier | none | flat | flat | dual | flat | none | none | flat | reviewer | sub-agent | none | subtasks |
| Extension API depth | 8 methods | MCP | hooks | hooks | none | rules | multi-tool | config | microagents | bundles | profiles | 25+ events | none |
| Sandbox | Docker | none | none | none | none | OS-level | none | none | Docker | SWE-ReX | none | none | server |
| MCP support | client | native | client | client | none | none | client | client | none | none | client+server | none | none |
| Session format | JSON DAG | SQLite | JSONL DAG | JSON | memory | JSONL | JSON | JSON | EventStream | trajectory | thread DB | JSONL DAG | PostgreSQL |

\* Cline and Continue access LSP through VS Code's API, not as discrete agent tools.
