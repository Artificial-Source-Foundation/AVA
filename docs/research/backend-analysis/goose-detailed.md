# Goose: Deep Competitive Intelligence Analysis

> **Version analyzed**: v1.26.0 (by Block, parent of Square/CashApp/Tidal)
> **Language**: Rust (workspace monorepo)
> **Architecture**: Tauri-like desktop app (Electron UI + Rust backend)

---

## Table of Contents

1. [Architecture Overview](#1-architecture-overview)
2. [Extension System — The Core Innovation](#2-extension-system--the-core-innovation)
3. [Platform Extensions — In-Process Tools](#3-platform-extensions--in-process-tools)
4. [Builtin MCP Extensions — Out-of-Process Tools](#4-builtin-mcp-extensions--out-of-process-tools)
5. [Security Architecture](#5-security-architecture)
6. [Context Management](#6-context-management)
7. [Prompt Engineering](#7-prompt-engineering)
8. [Provider System](#8-provider-system)
9. [Recipe System — Task Templates](#9-recipe-system--task-templates)
10. [Subagent System](#10-subagent-system)
11. [Server & Session Architecture](#11-server--session-architecture)
12. [Unique Innovations & Clever Patterns](#12-unique-innovations--clever-patterns)
13. [Competitive Advantages vs AVA](#13-competitive-advantages-vs-ava)
14. [Weaknesses & Gaps](#14-weaknesses--gaps)

---

## 1. Architecture Overview

### Workspace Structure (8 crates)

| Crate | Purpose |
|-------|---------|
| `goose` | Core agent logic, providers, context management, tools |
| `goose-cli` | CLI entry point |
| `goose-server` | HTTP server (`goosed` binary), 23 route files via Axum |
| `goose-mcp` | Builtin MCP extension servers (computercontroller, memory, etc.) |
| `goose-acp` | Agent Communication Protocol support |
| `goose-acp-macros` | Proc macros for ACP |
| `goose-test-support` | Test utilities |
| `goose-test` | Integration tests |

### Why This Structure Matters

The Rust monorepo gives Goose significant advantages:
- **Compile-time safety**: No runtime type errors, no null pointer exceptions in the agent loop
- **Memory safety without GC**: Critical for long-running desktop agents that must not leak
- **Fearless concurrency**: `tokio::select!` + channels for async tool execution without data races
- **Single binary deployment**: The server, CLI, and all builtin extensions compile into one ~40MB binary

### Entry Points

```
CLI (goose-cli) → goosed server → Agent → ExtensionManager → MCP clients
Electron UI     → HTTP API      → Agent → ExtensionManager → MCP clients
```

The Agent struct is the centerpiece:
```rust
struct Agent {
    provider: Mutex<SharedProvider>,      // LLM connection
    extension_manager: ExtensionManager,  // All tool sources
    final_output_tool: Mutex<Option<FinalOutputTool>>,  // Structured responses
    frontend_tools: Mutex<Vec<Tool>>,     // UI-provided tools
    prompt_manager: Mutex<PromptManager>, // System prompt builder
    confirmation_tx/rx: mpsc channels,    // Approval flow
    tool_result_tx/rx: mpsc channels,     // Tool result streaming
    retry_manager: Mutex<RetryManager>,   // Recipe-based retry
    tool_inspection_manager: ToolInspectionManager, // 3-layer security
    container: Mutex<Option<DockerContainer>>,  // Sandboxed execution
}
```

**Why**: The Agent owns everything through `Mutex` wrappers because it must be `Send + Sync` for async Rust. Each subsystem is independently lockable, preventing contention during concurrent operations.

---

## 2. Extension System — The Core Innovation

Goose's most architecturally significant decision is treating **everything as an MCP extension**. This is not just protocol compliance — it's a deliberate architectural boundary that enables hot-swapping, isolation, and composability.

### ExtensionConfig Enum (7 Types)

```rust
enum ExtensionConfig {
    Stdio { cmd, args, envs, timeout, ... },     // Child process MCP server
    StreamableHttp { url, headers, timeout, ... }, // HTTP-based MCP
    Builtin { name, ... },                         // In-process via tokio duplex
    Platform { name, ... },                        // Direct agent access, in-process
    Frontend { name, ... },                        // UI-provided tools
    InlinePython { code, dependencies, ... },      // Python via uvx bridge
    SSE { url, ... },                              // Deprecated
}
```

### Why Each Type Exists

**Stdio** — The standard MCP pattern. Launches a child process that speaks MCP over stdin/stdout. This is how third-party tools (like a database explorer or GitHub integration) are connected.
- **Value**: Universal compatibility with any MCP server. Hot-swappable. Process isolation means a crashed tool doesn't take down the agent.
- **Without it**: You'd need to write Rust bindings for every tool, or run everything in-process with crash propagation.

**StreamableHttp** — HTTP-based MCP (replacing SSE). For remote tool servers.
- **Value**: Tools can run on remote machines, enabling cloud-hosted expensive operations (GPU inference, browser farms).
- **Without it**: All tools must run locally, limiting what the agent can do.

**Builtin** — In-process MCP via tokio duplex byte channels. Used for: autovisualiser, computercontroller, memory, tutorial, peekaboo.
- **Value**: Zero-latency tool calls. No serialization overhead for the most-used tools. Still speaks MCP protocol internally, so they could be moved out-of-process without API changes.
- **Without it**: Either slow IPC for hot-path tools, or a non-MCP internal API that fragments the architecture.

**Platform** — In-process extensions with direct access to Agent internals (ExtensionManager, SessionManager). Used for: developer, analyze, todo, apps, chatrecall, extensionmanager, summon, code_execution, tom.
- **Value**: These tools need to read/modify agent state (e.g., extensionmanager adds/removes other extensions). The platform boundary gives them more power than MCP allows.
- **Without it**: These tools would need backdoor APIs around MCP, creating ad-hoc coupling.

**Frontend** — Tools defined by the Electron UI and dispatched back to it.
- **Value**: The UI can expose capabilities (like file pickers, modals) as tools the agent can call. Bridges the UI/agent gap without tight coupling.
- **Without it**: Agent would need direct UI control, creating platform-specific code in the core.

**InlinePython** — Python code executed via `uvx` with MCP bridge.
- **Value**: Recipes can include Python snippets as tools without any installation. Dependencies auto-installed.
- **Without it**: Recipes are limited to existing tools or Rust-based extensions.

### Environment Variable Security (31 Disallowed Keys)

The `Envs` struct blocks dangerous environment variables from being passed to child processes:
```
PATH, LD_PRELOAD, LD_LIBRARY_PATH, PYTHONPATH, NODE_OPTIONS,
DYLD_INSERT_LIBRARIES, DYLD_LIBRARY_PATH, GOOSE_API_KEY, ...
```

**Why**: A malicious MCP server config could set `LD_PRELOAD` to inject code into every child process, or `NODE_OPTIONS` to execute arbitrary JavaScript. This is a real attack vector that most competitors ignore.

---

## 3. Platform Extensions — In-Process Tools

Platform extensions are Goose's "first-party tools" — they run in the same process as the agent and have access to agent internals. There are 9 of them:

### 3.1 Developer Extension (default enabled, unprefixed)

**Tools**: `write`, `edit`, `shell`, `tree`

| Tool | Purpose | Why It Exists |
|------|---------|---------------|
| `write` | Create/overwrite files | Atomic file creation with parent dir creation. Without it: shell `mkdir -p && cat >` is error-prone and non-atomic |
| `edit` | Find-and-replace text editing | Exact match required, unique match required. Without it: `sed` is regex-based and can make unintended changes |
| `shell` | Execute shell commands | Stdout/stderr separated, 2000 line limit with overflow to temp files. Without it: No way to run builds, tests, git |
| `tree` | Directory tree with line counts | Respects .gitignore. Without it: `find` or `ls -R` includes node_modules, .git, etc. |

**Clever Details**:
- `shell` returns **structured output** (`ShellOutput` with separate stdout/stderr fields) rather than mixed text. This lets the LLM distinguish errors from output.
- `tree` shows **line counts per file**, giving the LLM a sense of file complexity without reading them.
- `edit` requires **exact text match** and **unique match** — this prevents the common LLM failure mode of ambiguous regex replacements.
- All tools have **MCP ToolAnnotations** (read_only_hint, destructive_hint, idempotent_hint, open_world_hint) — used by the permission system.
- Tools are **unprefixed** — exposed as `write`, `edit`, `shell`, `tree` instead of `developer__write`. This reduces token usage in tool calls.

**Instructions emphasize efficiency**: "Use tree to understand structure, prefer rg for search, always read before editing, minimize unnecessary turns."

### 3.2 Analyze Extension (default enabled, unprefixed)

**Tool**: `analyze` (single tool, 3 modes)

| Mode | Trigger | Output |
|------|---------|--------|
| Directory overview | Directory path | File tree with function/class counts per file |
| File details | File path | Functions, classes, imports, call counts |
| Call graph | Any path + `focus` symbol | Incoming/outgoing call chains |

**Why It Exists**: Tree-sitter-based code analysis gives the LLM **structural understanding** without reading entire files. A single `analyze .` call can map a 10,000-file codebase.

**Supported Languages**: Go, Java, JavaScript, Kotlin, Python, Ruby, Rust, Swift, TypeScript (9 languages via tree-sitter grammars compiled in).

**Clever Details**:
- Uses **rayon** for parallel file parsing — analyzing large codebases is CPU-bound, and rayon automatically parallelizes across cores
- **Configurable depth**: `max_depth` (default 3) prevents explosion on deep directory trees
- **Call graph follow**: `follow_depth` (default 2) traces symbol references across files
- Respects `.gitignore` via `ignore::WalkBuilder`

**Without it**: The LLM would need to read dozens of files to understand a codebase structure, consuming massive context window.

### 3.3 Todo Extension (default enabled)

**Why It Exists**: Gives the agent a persistent scratch pad for tracking multi-step tasks. Without it, the agent loses track of what it was doing during long sessions.

### 3.4 Apps Extension (default enabled)

**Purpose**: Create and manage custom HTML/CSS/JS apps in sandboxed windows.

**Why It Exists**: Goose targets non-developers ("power users"). This lets the agent build small tools for users — a spreadsheet viewer, a data dashboard — without any development setup.

### 3.5 ChatRecall Extension (default DISABLED)

**Purpose**: Search past conversations and load session summaries.

**Why It Exists**: Cross-session memory. The agent can recall what was discussed in previous sessions, providing continuity.

**Why Disabled by Default**: Privacy and context pollution. Loading irrelevant past conversations wastes tokens.

### 3.6 Extension Manager (default enabled)

**Tools**: `manage_extensions` (enable/disable), `search_available_extensions`

**Why It Exists**: The agent can self-configure. If a user asks "connect to my Postgres database", the agent can search for and enable a Postgres MCP extension without the user knowing what MCP is.

### 3.7 Summon Extension (default enabled, unprefixed)

**Tools**: `load` (inject knowledge), `delegate` (run subagents)

**Source Types**: Subrecipe, Recipe, Skill, Agent, BuiltinSkill

**Why It Exists**: This is the **multi-agent orchestration layer**. The `delegate` tool spawns isolated subagents with their own conversation, extensions, and provider. The `load` tool injects recipe/skill content into the current context.

**Clever**: Subrecipes within a recipe automatically enable the `summon` extension, creating a hierarchical task system.

### 3.8 Code Execution Extension (feature-gated, default disabled)

**Purpose**: Execute tool calls through code rather than JSON, saving tokens.

**Why It Exists**: An experimental optimization — instead of the LLM generating JSON tool calls, it generates Python/JS code that calls tools programmatically. This can save 30-50% of output tokens for complex multi-tool sequences.

### 3.9 Top Of Mind (TOM) Extension (default enabled)

**Purpose**: Inject custom context via `GOOSE_MOIM_MESSAGE_TEXT` and `GOOSE_MOIM_MESSAGE_FILE` environment variables.

**Why It Exists**: Users can set environment variables to inject context into every agent turn without modifying the system prompt. Power user feature for CI/CD integration.

---

## 4. Builtin MCP Extensions — Out-of-Process Tools

These run as in-process MCP servers (via tokio duplex channels) but are architecturally separate from platform extensions.

### 4.1 ComputerController — Desktop Automation

This is Goose's most impressive extension — a full desktop automation toolkit:

| Tool | Purpose | Why |
|------|---------|-----|
| `web_scrape` | Fetch web content to cache | Agent can browse the web without a browser |
| `automation_script` | Run Shell/Batch/Ruby/PowerShell scripts | Cross-platform task automation |
| `computer_control` | Platform-specific GUI automation | Full desktop control |
| `xlsx_tool` | Excel manipulation (7 operations) | Business users work with spreadsheets |
| `pdf_tool` | PDF text/image extraction | Common document format |
| `docx_tool` | Word doc manipulation (4 modes) | Common document format |
| `cache` | Manage cached files | Support tool for web_scrape |

**macOS Computer Control (Peekaboo Integration)**:
- Auto-installs `peekaboo` CLI via Homebrew on first use
- Full GUI automation: see (annotated screenshots with element IDs), click, type, press, hotkey, paste, scroll, drag, app/window management, menu bar, clipboard
- Returns **base64-encoded annotated screenshots** as image content
- Workflow: `see --annotate` → identify element IDs → `click --on B3` → `type "text"` → verify with screenshot

**Why This Matters**: Goose can control any macOS application. It can fill out web forms, navigate complex UIs, manage files in Finder — tasks that no code-only agent can do. This targets the "non-developer power user" market.

**Without it**: The agent is limited to command-line and API operations. The vast majority of business workflows happen in GUIs.

### 4.2 Memory — Persistent Cross-Session Memory

**Tools**: `remember_memory`, `retrieve_memories`, `remove_memory_category`, `remove_specific_memory`

**Architecture**:
- **Dual scope**: Global (`~/.config/goose/memory`) and Local (`.goose/memory` in project)
- **File-based storage**: One `.txt` file per category, tags as headers, entries separated by `\n\n`
- **Auto-injection**: On startup, ALL global memories are loaded and injected into the extension's instructions, so they appear in the system prompt

**Why It Exists**: Users expect the AI to remember their preferences, conventions, and project context across sessions. Without persistent memory, every session starts from zero.

**Clever Details**:
- Working directory extracted from MCP request metadata (`agent-working-dir` header)
- Proactive memory suggestions: The extension instructions define trigger keywords ("remember", "forget") and example interaction flows
- Local memories for project-specific data, global for user preferences

### 4.3 AutoVisualiser

Auto-generates visualizations from data.

### 4.4 Tutorial

Guided tutorials for onboarding users.

### 4.5 Peekaboo (macOS only)

Screenshot/screen capture tool. Separate from ComputerController for modularity.

---

## 5. Security Architecture

Goose has a **3-layer tool inspection pipeline**, which is more sophisticated than most competitors:

### Layer 1: SecurityInspector (Highest Priority)

**37 threat patterns** with regex matching across 8 categories:

| Category | Example Patterns |
|----------|-----------------|
| FileSystemDestruction | `rm -rf /`, `dd if=/dev/zero of=/dev/sda`, `mkfs.ext4 /dev/sda` |
| RemoteCodeExecution | `curl | bash`, `python -c exec(urllib...)`, PowerShell DownloadString |
| DataExfiltration | SSH key access, `/etc/shadow` reading, history file exfiltration |
| SystemModification | crontab modification, systemd service creation, hosts file changes |
| NetworkAccess | Netcat listeners, reverse shells, SSH tunnels |
| ProcessManipulation | Killing security processes, GDB attach, ptrace injection |
| PrivilegeEscalation | NOPASSWD sudoers, SUID binary creation, Docker privileged |
| CommandInjection | Base64/hex encoded commands, eval with variables, nested substitution |

**Obfuscation Detection**: Unicode obfuscation, string concatenation patterns, encoded command execution.

**Risk Levels**: Critical (0.95 confidence), High (0.75), Medium (0.60), Low (0.45)

**Why This Matters**: An LLM can be tricked (via prompt injection or confused reasoning) into running dangerous commands. Pattern matching catches the most obvious attacks. Most competitors rely solely on user approval for dangerous operations.

### Layer 2: PermissionInspector (Medium Priority)

- Classifies tools as read-only vs write based on MCP ToolAnnotations
- Implements permission levels with user approval flow
- Uses the `confirmation_tx/rx` channels on the Agent for interactive approval

### Layer 3: RepetitionInspector (Lowest Priority)

- Detects and blocks repeated identical tool calls
- Prevents infinite loops where the LLM keeps retrying the same failing operation

**Why This Architecture**:
- **Layered defense**: Each inspector catches different failure modes
- **Priority ordering**: Security > Permission > Repetition. A security threat blocks even if permissions would allow it.
- **Confidence scores**: Allows nuanced responses (warn vs block)

---

## 6. Context Management

### Auto-Compaction

The context management system is one of Goose's most technically sophisticated components:

```
reply() → check token ratio → if > 80% threshold → trigger compaction
```

**Compaction Algorithm**:
1. Ask the LLM to summarize the conversation so far
2. If the summary itself exceeds context, progressively remove tool responses:
   - Round 1: Remove 0% of tool responses from middle-out
   - Round 2: Remove 10%
   - Round 3: Remove 20%
   - Round 4: Remove 50%
   - Round 5: Remove 100%
3. Replace original messages with summary

**Dual Visibility System**:
- Messages have `agent_visible` and `user_visible` metadata flags
- Compacted original messages become `user_visible` only (for chat history display)
- Summary messages become `agent_visible` only (for LLM context)

**Why This Matters**: Long coding sessions can easily exceed context limits. Without auto-compaction, the agent either crashes or loses early context. The progressive removal strategy is clever — it preserves the most informative content (recent and extremes) while discarding middle-of-session noise.

### Large Response Handler

Tool responses exceeding **200,000 characters** are written to temp files and replaced with a reference:
```
"The tool response was too large. It has been saved to /tmp/goose_xyz.txt. Use other tools to read it."
```

**Why**: A single `cat` of a large file could consume the entire context window. This prevents accidental context exhaustion.

### MOIM (Message-of-Information-Message) Injection

Injects contextual information (working directory, etc.) as `<info-msg>` tags into the conversation before the last assistant message.

**Why**: Provides runtime context without polluting the system prompt (which should remain cacheable).

---

## 7. Prompt Engineering

### PromptManager — Builder Pattern

The prompt construction system has several clever optimizations:

**Prompt Caching Optimization**:
- Timestamp rounded to the **hour** (`%Y-%m-%d %H:00`) instead of exact time
- Extensions sorted **alphabetically** for stable ordering
- These ensure the system prompt is identical across sessions within the same hour, enabling multi-session prompt caching (which can save 50-90% on input costs)

**Unicode Tag Sanitization**:
- All inputs (system prompt override, extras, extension instructions) are stripped of Unicode tag characters (U+E0041-U+E007F)
- These invisible Unicode characters can be used for **prompt injection** — embedding instructions that appear as blank text but are processed by the LLM
- Recipe fields are also checked for Unicode tag injection

**Goose Modes** (3 modes):
- **Auto**: Full autonomy, all tools available
- **Chat**: No tool access, conversation only (injects "no access to any tool use" instruction)
- **Approve**: Tools available but require user confirmation

**System Prompt Structure**:
```
[Base identity + extensions with instructions]
[Tool limit warnings if > 5 extensions or > 50 tools]
[Response guidelines (Markdown)]
[Additional instructions (hints, chat mode, etc.)]
```

**Hint File Loading**:
- Reads `.goosehints` and `AGENTS.md` from working directory
- Respects `.gitignore` for file discovery
- Injected as "Additional Instructions" section

---

## 8. Provider System

Goose has the most extensive provider support of any open-source agent (47 provider files):

### Provider Trait

```rust
trait Provider {
    fn complete() -> MessageStream;     // Streaming completions
    fn get_fast_model() -> Option<()>;  // Fallback for simple tasks
    fn embeddings() -> Vec<f32>;        // For RAG
    fn oauth_flow() -> Token;           // OAuth authentication
    fn session_name() -> String;        // Auto-naming sessions
    fn supports_cache_control() -> bool; // Anthropic-style caching
}
```

### LeadWorkerProvider — Dual Model System

The most innovative provider pattern:
- **Lead model**: Used for complex reasoning (e.g., Claude 3.5 Sonnet)
- **Worker model**: Used for simple tasks (e.g., Claude 3.5 Haiku)
- Automatically routes based on task complexity

**Why**: Cost optimization. Simple tasks like file reading or shell commands don't need the most powerful model. This can reduce costs by 40-60% on typical workloads.

### Canonical Model Registry

Maps provider-specific model names to canonical IDs and filters by:
- Modality (text, vision, audio)
- Tool call support
- Cost tiers

### ProviderUsage Tracking

Every LLM call tracks input tokens, output tokens, total tokens, and estimated cost. This enables:
- Session cost reporting
- Budget limits
- Model comparison

---

## 9. Recipe System — Task Templates

Recipes are Goose's **reusable task definition format** — YAML/JSON files that define:

```yaml
version: "1.0.0"
title: "Code Review"
description: "Review a PR for issues"
instructions: "System prompt for this task"
prompt: "Initial user message"
extensions:
  - type: stdio
    name: github
    cmd: github-mcp-server
settings:
  goose_provider: anthropic
  goose_model: claude-sonnet-4-20250514
  temperature: 0.3
  max_turns: 20
parameters:
  - key: pr_url
    input_type: string
    requirement: required
    description: "PR URL to review"
response:
  json_schema: { ... }  # Structured output
sub_recipes:
  - name: lint_check
    path: lint-recipe.yaml
    values: { target: "src/" }
retry:
  max_retries: 3
  success_check: "test -f review.md"
```

### Why Recipes Matter

1. **Reproducibility**: Same recipe → same agent configuration every time
2. **Sharing**: Users can share recipes without sharing API keys or full configs
3. **Automation**: Recipes can be scheduled (via `manage_schedule` platform tool)
4. **Composability**: Sub-recipes enable hierarchical task decomposition
5. **Security**: Unicode tag injection detection in recipe fields

### Sub-Recipes

When a recipe includes `sub_recipes`, the `summon` platform extension is auto-injected. This creates a hierarchical system:
```
Parent Recipe → summon.delegate() → Child Recipe as subagent
```

### Typed Parameters

Recipes support 6 parameter types: string, number, boolean, date, file, select. The `file` type imports content from a file path but **cannot have default values** (security: prevents importing sensitive user files via recipe distribution).

### Retry System

Recipe-based retry with:
- Configurable `max_retries`
- **Shell-based success checks**: e.g., `"test -f output.json"` — runs a shell command to verify the task succeeded
- **On-failure hooks**: Custom actions when retry triggers
- Conversation reset to initial state on retry

**Why**: For automated/scheduled tasks, retry is essential. The shell-based success check is clever — it lets any external condition define "success."

---

## 10. Subagent System

### Architecture

```
Parent Agent
  └── summon.delegate(task)
       └── SubagentRunParams
            └── new Agent with:
                 - Own AgentConfig
                 - Own provider (from TaskConfig)
                 - Own extensions (from TaskConfig)
                 - Own conversation
                 - CancellationToken
                 - Notification forwarding to parent
```

### How It Works

1. `delegate` tool in Summon extension creates `SubagentRunParams`
2. New `Agent` instantiated with separate config
3. Provider set from TaskConfig
4. Extensions loaded from TaskConfig
5. System prompt built from `subagent_system.md` template (includes max_turns, task_instructions, available_tools)
6. Agent runs its own reply loop
7. Events streamed back: tool requests forwarded as `LoggingMessageNotification` to parent
8. Final output extracted (either last message text or structured response schema)

### Source Types

The Summon extension can load from 5 source types:
- **Subrecipe**: Sub-recipes defined in parent recipe
- **Recipe**: Standalone recipe files
- **Skill**: Skill files (knowledge/instructions)
- **Agent**: Pre-configured agent definitions
- **BuiltinSkill**: Hardcoded skills

### Why This Matters

Subagents provide:
- **Isolation**: A failed subtask doesn't corrupt the parent's conversation
- **Specialization**: Each subagent gets exactly the tools it needs
- **Parallelism**: Multiple subagents can run concurrently
- **Token efficiency**: Subagent conversations are independent — a 100K token analysis doesn't consume the parent's context

---

## 11. Server & Session Architecture

### HTTP Server (23 Routes via Axum)

| Route Category | Purpose |
|---------------|---------|
| `agent`, `reply` | Agent interaction |
| `session` | Session CRUD |
| `config_management` | Configuration |
| `recipe` | Recipe management |
| `schedule` | Scheduled task execution |
| `prompts`, `sampling` | Prompt/model management |
| `action_required` | User approval flow |
| `dictation` | Voice input |
| `gateway` | API gateway/proxy |
| `local_inference` | Local model support |
| `mcp_app_proxy`, `mcp_ui_proxy` | MCP protocol proxying |
| `tunnel` | Remote access |
| `telemetry` | Usage tracking |
| `setup`, `status` | System status |

### Agent Event Streaming

```rust
enum AgentEvent {
    Message(Message),           // New message in conversation
    McpNotification(Notif),     // MCP extension notification
    ModelChange { from, to },   // Provider/model switch
    HistoryReplaced(Conversation), // After compaction
}
```

The `reply()` method returns a `Stream<Item = AgentEvent>` — the server can forward this directly as SSE to the frontend.

### ToolStream Pattern

During tool execution, Goose uses `tokio::select!` to multiplex:
1. MCP notifications from the extension (progress updates, logs)
2. The actual tool result

This means the UI can show **real-time progress** during long tool operations (like a build) without waiting for completion.

---

## 12. Unique Innovations & Clever Patterns

### 1. Everything-as-MCP-Extension

**The Pattern**: Even internal tools (developer, analyze, todo) implement the MCP protocol. They run in-process but through the same interface as external tools.

**Why It's Clever**: Any tool can be moved in-process ↔ out-of-process without changing the agent. Testing is uniform. The security/permission layers apply equally to all tools.

### 2. ToolAnnotations for Automated Permission

**The Pattern**: Each tool carries metadata: `read_only_hint`, `destructive_hint`, `idempotent_hint`, `open_world_hint`.

**Why It's Clever**: The permission system can make automatic decisions (read-only tools never need approval) without hardcoding tool names.

### 3. Prompt Cache Optimization

**The Pattern**: Timestamp rounded to hour, extensions sorted alphabetically.

**Why It's Clever**: Multi-session prompt caching can save 50-90% on input costs for Anthropic models. Most competitors rebuild the system prompt from scratch each time.

### 4. Progressive Context Compaction

**The Pattern**: When compaction fails because the summary is too long, progressively remove 0% → 10% → 20% → 50% → 100% of tool responses from middle-out.

**Why It's Clever**: Graceful degradation. Most competitors either fail or do a single aggressive truncation.

### 5. Dual Visibility System

**The Pattern**: Messages have separate `agent_visible` and `user_visible` flags. Compacted originals stay user-visible (for history), summaries stay agent-visible (for context).

**Why It's Clever**: The user sees the full conversation history while the agent works with a compacted version. No information loss for the user.

### 6. Unicode Tag Sanitization

**The Pattern**: All user inputs and extension instructions are stripped of Unicode tag characters (U+E0041-U+E007F).

**Why It's Clever**: These invisible characters can encode hidden instructions that the LLM processes but humans can't see. This is a real attack vector in the wild. Very few competitors implement this.

### 7. Shell-Based Success Checks for Retry

**The Pattern**: Retry configs include shell commands as success checks: `"test -f output.json && jq .status output.json | grep 'complete'"`.

**Why It's Clever**: Arbitrary success conditions without a custom API. Any UNIX-checkable state can be a retry condition.

### 8. Auto-Installing Dependencies (Peekaboo)

**The Pattern**: The `computer_control` tool auto-installs Peekaboo via Homebrew on first use.

**Why It's Clever**: Zero-setup desktop automation. The user doesn't need to know what Peekaboo is or how to install it.

### 9. Large Response Handler (200K Threshold)

**The Pattern**: Tool responses > 200K chars are dumped to temp files with a reference.

**Why It's Clever**: Prevents accidental context exhaustion from a single `cat large_file.txt`. The agent can still access the content via file reading tools.

### 10. MOIM (Message-of-Information-Message) Injection

**The Pattern**: Runtime context injected as `<info-msg>` tags into conversation, not into system prompt.

**Why It's Clever**: Keeps the system prompt stable (cacheable) while still providing dynamic context.

---

## 13. Competitive Advantages vs AVA

### Where Goose Is Ahead

| Area | Goose | AVA Status |
|------|-------|------------|
| **Desktop automation** | Full GUI control via Peekaboo (macOS), PowerShell (Windows), X11 (Linux) | No desktop automation |
| **Document handling** | Native XLSX, PDF, DOCX tools | No document tools |
| **Recipe system** | Full templating, parameters, sub-recipes, scheduling | No equivalent |
| **Persistent memory** | Dual-scope (global/local) with auto-injection | Memory module exists but details TBD |
| **Security patterns** | 37 threat patterns, 3-layer inspection | Permission system exists, unclear depth |
| **Provider breadth** | 47 provider files, LeadWorker dual-model | Fewer providers |
| **Code analysis** | Tree-sitter with call graphs (9 languages) | Codebase module exists |
| **Extension hot-swapping** | Agent can discover and enable extensions at runtime | Static tool registry |
| **Prompt caching** | Timestamp rounding, stable ordering | Not mentioned |
| **Context compaction** | Progressive middle-out with dual visibility | Compaction exists |

### Where AVA Is Ahead or Different

| Area | AVA | Goose |
|------|-----|-------|
| **Language** | TypeScript (faster iteration, larger contributor pool) | Rust (faster runtime, harder to contribute) |
| **Commander/Workers** | Hierarchical delegation with named worker types (coder, tester, reviewer, debugger, researcher) | Flat subagent system |
| **Plan mode** | Explicit plan_enter/plan_exit tools | No plan mode |
| **LSP integration** | Language Server Protocol client | Tree-sitter only |
| **Browser tool** | Puppeteer automation | Peekaboo (macOS-only for full GUI) |
| **Apply patch** | Unified diff application | Find-and-replace edit only |
| **Parallel execution** | Parallel commander module | Subagents are sequential by default |
| **Skill system** | Rich skill loading from plugins | Built-in skills only |

### Key Patterns to Adopt

1. **Progressive context compaction** — The middle-out removal with 5 progressive rounds is superior to single-shot truncation
2. **Prompt caching optimization** — Timestamp rounding and stable extension ordering are low-effort, high-value
3. **ToolAnnotations** — Auto-classifying tools as read-only/destructive enables smarter permission defaults
4. **Large response handler** — The 200K char threshold prevents context exhaustion from careless tool use
5. **Unicode tag sanitization** — Cheap defense against a real attack vector
6. **Dual visibility** — Letting users see full history while the agent works with compacted context
7. **LeadWorkerProvider** — Dual-model routing based on task complexity for cost savings

---

## 14. Weaknesses & Gaps

### Architecture Limitations

1. **No LSP integration**: Goose uses tree-sitter for static analysis only. No real-time diagnostics, no go-to-definition, no refactoring support.
2. **No unified diff/patch tool**: The `edit` tool only does find-and-replace. For large multi-hunk changes, this requires many sequential tool calls.
3. **No plan mode**: No explicit planning phase. The agent jumps straight into execution.
4. **No parallel tool execution**: Tools are executed sequentially within a turn. No batch tool like AVA's.
5. **macOS-centric desktop automation**: Peekaboo only works on macOS. Windows/Linux get basic shell automation.
6. **File-based memory**: Plain text files are not searchable at scale. No embeddings, no vector store.
7. **No git integration**: No snapshot/rollback system. Changes are permanent.
8. **Recipe system complexity**: 850 lines of recipe parsing code for what is essentially a YAML config format.

### Security Limitations

1. **Regex-based security**: Pattern matching can be bypassed by obfuscation. No semantic analysis of commands.
2. **No sandbox**: Despite having a Docker container field on Agent, it's optional. Tools run with full user permissions.
3. **Environment variable blocklist**: A blocklist approach means new dangerous variables are allowed by default.

### Developer Experience

1. **Rust barrier**: Contributing to Goose requires Rust expertise. The ecosystem is smaller than TypeScript.
2. **Compilation time**: Rust monorepo with tree-sitter grammars for 9 languages = long compile times.
3. **Platform extension complexity**: The dual MCP-client/platform-extension system has significant conceptual overhead.
