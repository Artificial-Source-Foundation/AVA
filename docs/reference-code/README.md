# Reference Code

> Cloned open-source AI coding agents for inspiration and reference

**Note:** This folder is gitignored. Clone repos locally for reference.

---

## Included Projects (13 repos, ~2+ GB)

| Project | Language | Stars | Focus |
|---------|----------|-------|-------|
| [OpenCode](https://github.com/sst/opencode) | Go | ~115k | TUI, Elm architecture, SQLite sessions, 75+ providers |
| [Zed](https://github.com/zed-industries/zed) | Rust | ~76k | GPUI framework, agent panel, Zeta edit prediction |
| [OpenHands](https://github.com/All-Hands-AI/OpenHands) | Python/React | ~65k | Docker sandboxing, cloud agent platform, SWE-bench leader |
| [Codex CLI](https://github.com/openai/codex) | TypeScript | ~63k | Minimal agent, sandbox model, React Ink TUI |
| [T3 Code](https://github.com/pingdotgg/t3code) | TypeScript | ~9.4k | Minimal web/desktop GUI for external coding agents |
| [Cline](https://github.com/cline/cline) | TypeScript | ~58k | VS Code ext, gRPC protocol, spawned fork ecosystem |
| [Gemini CLI](https://github.com/google-gemini/gemini-cli) | TypeScript | ~42k | Google's CLI, 1M token context, Search grounding |
| [Aider](https://github.com/Aider-AI/aider) | Python | ~41k | PageRank repo map, edit formats, architect mode |
| [Goose](https://github.com/block/goose) | Rust/Tauri | ~30k | **Same stack as AVA**, MCP-native, extensions as MCP servers |
| [Continue](https://github.com/continuedev/continue) | TypeScript | ~26k | IDE-agnostic core, 3-process model, 7-layer arch |
| [SWE-agent](https://github.com/SWE-agent/SWE-agent) | Python | ~14k | Research-grade agent, custom ACI, minimal design |
| [Plandex](https://github.com/plandex-ai/plandex) | Go | ~7k | Multi-file planning (shut down Oct 2025) |
| [pi-mono](https://github.com/badlogic/pi-mono) | TypeScript | — | Coding agent monorepo |

---

## Clone Commands

```bash
cd docs/reference-code

# Tier 1 — mega-popular
git clone --depth 1 https://github.com/sst/opencode.git opencode
git clone --depth 1 https://github.com/zed-industries/zed.git zed
git clone --depth 1 https://github.com/All-Hands-AI/OpenHands.git openhands
git clone --depth 1 https://github.com/openai/codex.git codex-cli
git clone --depth 1 https://github.com/pingdotgg/t3code.git t3code
GIT_LFS_SKIP_SMUDGE=1 git clone --depth 1 https://github.com/cline/cline.git
git clone --depth 1 https://github.com/google-gemini/gemini-cli.git gemini-cli

# Tier 2 — high value
git clone --depth 1 https://github.com/Aider-AI/aider.git aider
git clone --depth 1 https://github.com/block/goose.git goose
git clone --depth 1 https://github.com/continuedev/continue.git continue
git clone --depth 1 https://github.com/SWE-agent/SWE-agent.git swe-agent

# Tier 3 — supplementary
git clone --depth 1 https://github.com/plandex-ai/plandex.git plandex
git clone --depth 1 https://github.com/badlogic/pi-mono.git pi-mono
```

---

## Key Files to Study

### Goose (Rust/Tauri) — Same Stack as AVA
```
crates/
├── goose/          # Core agent logic
├── goose-cli/      # CLI interface
├── goose-mcp/      # MCP protocol
└── goose-server/   # Tauri desktop backend
ui/                 # Tauri frontend
```

### OpenCode (Go) — Elm Architecture TUI
```
packages/opencode/src/
├── tool/           # File ops: read, write, edit, glob, bash
├── agent/          # Agent loop, planning
├── provider/       # LLM providers
├── session/        # Session management (SQLite)
└── lsp/            # Language Server Protocol
```

### Cline (TypeScript) — VS Code + gRPC
```
src/
├── core/           # Agent core (~85k lines)
│   ├── Cline.ts    # Main agent class
│   ├── prompts/    # System prompts, tool descriptions
│   ├── tools/      # Tool implementations
│   └── mentions/   # @-mention handling
├── services/       # API providers, browser, MCP
└── integrations/   # VS Code, terminal, file system
```

### Aider (Python) — Repo Mapping
```
aider/
├── coders/         # Different edit formats (diff, whole, etc.)
├── repo_map.py     # Tree-sitter + PageRank codebase mapping
├── io.py           # File I/O operations
└── commands.py     # CLI commands
```

### Codex CLI (TypeScript) — Minimal Agent
```
codex-cli/src/
├── agents/         # Agent implementations
├── tools/          # Tool definitions
├── sandbox/        # Sandboxed execution
└── ui/             # React Ink terminal UI
```

### T3 Code (TypeScript) — Thin GUI Over External Agents
```
apps/
├── server/         # WebSocket server and provider/session broker
├── web/            # React/Vite client UI
└── desktop/        # Desktop packaging/runtime surface
packages/
├── contracts/      # Shared event and protocol schemas
└── shared/         # Shared runtime utilities
```

### Zed (Rust) — Native GPU Editor
```
crates/
├── gpui/           # Custom GPU-accelerated UI framework
├── agent/          # AI agent panel
├── assistant/      # AI assistant integration
├── language/       # Tree-sitter language support
└── editor/         # Core editor
```

### Continue (TypeScript) — IDE-agnostic Core
```
core/               # IDE-agnostic business logic
├── protocol/       # Message passing protocol
├── llm/            # LLM integrations
└── context/        # Context providers
extensions/
├── vscode/         # VS Code bridge
└── intellij/       # JetBrains bridge
gui/                # React webview (shared)
```

---

## What to Learn From Each

| Project | Learn |
|---------|-------|
| **Goose** | Rust+Tauri architecture, MCP-native extensions, agent-server split |
| **OpenCode** | Go TUI patterns, Elm architecture, SQLite session persistence |
| **Cline** | gRPC frontend-backend protocol, task model, MCP client |
| **Aider** | Tree-sitter + PageRank repo mapping, git-native workflow, edit formats |
| **Codex CLI** | Minimal agent design, sandbox execution, approval modes |
| **T3 Code** | Thin-client orchestration, provider-session brokering, web/desktop agent UX |
| **Zed** | GPUI (custom Rust UI), agent panel UX, Zeta edit prediction |
| **Continue** | IDE-agnostic core separation, 3-process model, context providers |
| **OpenHands** | Docker sandboxing, cloud agent platform, web UI |
| **SWE-agent** | Minimal research agent, custom ACI, SWE-bench approach |
| **Gemini CLI** | 1M token context handling, Google Search grounding |

---

## Update All

```bash
cd docs/reference-code
for dir in */; do echo "Updating $dir..." && git -C "$dir" pull 2>&1 | tail -1; done
```

---

## Content Summary

Each competitor folder contains a cloned git repository with:
- `README.md` — Project documentation
- `AUDIT.md` — AVA's competitive analysis (architecture, patterns, learnings)
- Full source code, configuration, and git history

| Folder | Files | Language | Relevance | Notes |
|--------|-------|----------|-----------|-------|
| aider/ | ~1,016 | Python | High | PageRank repo mapping, edit cascade |
| cline/ | ~1,963 | TypeScript | High | VS Code extension, gRPC protocol |
| codex-cli/ | ~2,917 | TypeScript + Rust | High | OpenAI's official CLI |
| continue/ | ~3,154 | TypeScript | High | IDE-agnostic 3-process model |
| gemini-cli/ | ~2,243 | TypeScript | Medium | Google's official CLI |
| goose/ | ~1,455 | Rust/Tauri | High | **Same stack as AVA** |
| opencode/ | ~4,133 | Go | High | Elm architecture TUI |
| openhands/ | ~2,588 | Python/React | High | Docker sandboxing |
| pi-mono/ | ~907 | TypeScript | Medium | Session DAG, auto-compaction |
| plandex/ | ~726 | Go | Medium | Multi-file planning (shut down Oct 2025) |
| swe-agent/ | ~437 | Python | Medium | Research-grade minimal agent |
| t3code/ | ~1,336 commits | TypeScript | Medium | Minimal browser/desktop shell over Codex/Claude |
| zed/ | ~74,067 | Rust | High | GPU-accelerated editor |

Last updated: 2026-04-17
