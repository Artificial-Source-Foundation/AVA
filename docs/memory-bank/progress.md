# Progress Log

> Quick status overview - detailed history in [sessions/archive.md](./sessions/archive.md)

---

## Current Session

**Session 34** (2026-02-07)
- ✅ **Sidebar fix** — Changed from `margin-left: -260px` to `width: 0` + `overflow: hidden` (WebKitGTK content bleed fix)
- ✅ **Noise texture removal** — Removed `#root::after` overlay that blocked clicks in WebKitGTK
- ✅ **Sidebar animation** — Re-added `transition: width 120ms ease`
- ✅ **Settings scroll lag fix** — GPU layer promotion (`transform: translateZ(0)`), replaced all `transition-all` with `transition-colors`
- ✅ **Biome lint fixes** — `node:http` imports, shadow vars, void returns, banned types, assign-in-expressions
- ✅ **A11y fixes** — Added `role` attributes, changed interactive `<div>` to `<button>`, fixed label associations
- ✅ **TypeScript fix** — ChatBubble missing `role` prop in DesignSystemPreview
- ✅ **All pre-commit hooks pass** (biome, oxlint, tsc, commitlint)
- ✅ **Docs updated** — Frontend README + design-system.md with WebKitGTK notes
- Commits: 2bb8b86, 9a75f1a, 605ac7e

**Session 33** (2026-02-05)
- ✅ **MVP Sprint 1-7** — Settings, tool approval, design tokens, component polish, team model, team UI, integration
- ✅ **Tauri hardening** — CSP, scoped FS, deferred window show, release profile, window state persistence

**Session 32** (2026-02-05)
- ✅ **Vision Alignment** — Defined Estela as "The Obsidian of AI Coding"
  - Desktop-first, multi-provider, Obsidian-style plugins, dev team UX
  - Target: experienced devs + vibe coders + plugin creators
  - Phase priority: Desktop App → Plugin Ecosystem → CLI → Editor → Agent Network
- ✅ **Documentation Overhaul**
  - Rewrote: VISION.md, CLAUDE.md, projectbrief.md, ROADMAP.md
  - Rewrote: architecture/README.md, components.md, data-flow.md, database-schema.md
  - Rewrote: frontend/README.md, docs/README.md
  - Deleted 60+ stale files: archive/ (50+ old plugin-era docs), gap analyses, stale architecture
  - Moved completed epics (19, 20, 21, 26) to development/completed/
  - Consolidated analysis reports into docs/research/ (cline, opencode, gemini-cli)
  - New naming: Commander→Team Lead, Worker→Senior Lead, Operator→Junior Dev

**Session 31** (2026-02-05)
- ✅ **IDE Layout Redesign** — Transformed from chat-app layout to VS Code/Cursor-inspired IDE layout
  - Activity Bar (48px, left edge) — 4 activity icons + settings, toggles contextual sidebar
  - Contextual Sidebar — switches content based on activity (Sessions, Explorer, Agents, Memory)
  - Chat always visible — promoted from tab to permanent main content
  - Bottom Panel — resizable, collapsible, tabbed (Terminal/Activity/Changes)
  - Keyboard shortcuts: Ctrl+B (sidebar), Ctrl+`/Ctrl+J (bottom panel)
  - **New files:** `stores/layout.ts`, `layout/ActivityBar.tsx`, `layout/MainArea.tsx`, `layout/BottomPanel.tsx`, `layout/SidebarPanel.tsx`, `sidebar/SidebarSessions.tsx`, `sidebar/SidebarExplorer.tsx`, `sidebar/SidebarAgents.tsx`, `sidebar/SidebarMemory.tsx`
  - **Rewritten:** `layout/AppShell.tsx` (3-column layout with @corvu/resizable)
  - **Deleted:** `layout/MainContent.tsx`, `layout/TabBar.tsx`, `layout/Sidebar.tsx`
  - **Modified:** All panel components (compact prop), `session.ts` (removed TabId/activeTab), `index.css` (horizontal resize handle)
  - Fixed nested button HTML warning in TerminalPanel

**Session 30** (2026-02-05)
- ✅ **Epic 26: Gemini CLI Feature Parity** (337 tests, 16 files)
  - Sprint 1: Policy Engine + Message Bus (76 tests)
  - Sprint 2: Session Resume + TOML Commands (90 tests)
  - Sprint 3: Trusted Folders + Extension System (96 tests)
  - Sprint 4: Chat Compression Enhancement (45 tests)
  - New modules: `policy/`, `bus/`, `session/resume.ts`, `session/file-storage.ts`, `commands/toml.ts`, `trust/`, `extensions/`, `context/strategies/`
- ✅ **Epic 25 Sprint 1: ACP Agent** — Already existed at `cli/src/acp/agent.ts`
- ✅ **Epic 25 Sprint 2: ACP Polish & IDE Features** (97 tests, 7 source + 5 test files)
  - `packages/core/src/acp/types.ts` — Protocol types, error codes, transport abstraction
  - `packages/core/src/acp/session-store.ts` — Session persistence (ACP → SessionManager bridge)
  - `packages/core/src/acp/terminal.ts` — Editor terminal bridge with poll fallback
  - `packages/core/src/acp/mcp-bridge.ts` — MCP server forwarding from editor configs
  - `packages/core/src/acp/mode.ts` — Plan/agent mode switching with tool restrictions
  - `packages/core/src/acp/error-handler.ts` — Error tracking, disconnect detection, emergency saves
  - `cli/src/acp/agent.ts` — Refactored to integrate all ACP modules (session persistence, mode switching, MCP bridge, error handling)
  - Fixed: Mock type assertions for generic `AcpTransport.request<T>`, unused import cleanup
- ✅ **Epic 25 Sprint 3: A2A Server** (97 tests, 7 source + 5 test files)
  - `packages/core/src/a2a/types.ts` — A2A protocol v0.3.0 types
  - `packages/core/src/a2a/agent-card.ts` — Agent discovery at `/.well-known/agent.json`
  - `packages/core/src/a2a/task.ts` — TaskManager with async generator streaming
  - `packages/core/src/a2a/streaming.ts` — SSE writer (framework-agnostic)
  - `packages/core/src/a2a/auth.ts` — Bearer token auth (timing-safe)
  - `packages/core/src/a2a/server.ts` — HTTP server using native `http` module
  - `packages/core/src/a2a/index.ts` — Barrel exports
  - Fixed: Bearer token empty string edge case (`'' ?? null` → truthiness check)
- ✅ **Memory bank & docs update**

**Session 29** (2026-02-05)
- ✅ **UI Modernization - 8 Phases Complete**
  - Phase 1: Glass/depth/blur/shadow tokens, ambient gradient mesh, glass utility classes
  - Phase 2: Spring physics animations (solid-motionone) on Button, Toggle, Toast, Dialog
  - Phase 3: Glassmorphism applied to Sidebar, Card, Dialog, Toast
  - Phase 4: Resizable panels (@corvu/resizable) with drag-to-resize sidebar
  - Phase 5: CodeEditorPanel with CodeMirror 6, One Dark theme, file tabs
  - Phase 6: Component polish - ChatBubble slide-up, Badge pulse, Select spring, Input focus-glow
  - Phase 7: Welcome state + StatusBar integration
  - Phase 8: Documentation update for docs/frontend/
- ✅ **TabBar & Panel System Integration**
  - TabBar now rendered in MainContent (was unused before)
  - All 6 tabs wired to their panel components
  - StatusBar rendered at bottom of main content area
- ✅ **New Files Created:**
  - `src/lib/motion.ts` - Spring presets + useReducedMotion hook
  - `src/components/panels/CodeEditorPanel.tsx` - CodeMirror code viewer
- ✅ **Dependencies Added:**
  - solid-motionone, @corvu/resizable, solid-codemirror
  - @codemirror/state, @codemirror/view, @codemirror/lang-javascript
  - @codemirror/lang-json, @codemirror/theme-one-dark

**Session 28** (2026-02-05)
- ✅ **Comprehensive Gemini CLI Analysis** (~139KB documentation)
  - `root-configs.md` (~14KB) - Monorepo, build system, CI/CD
  - `vscode-extension.md` (~23KB) - IDE integration, 20+ IDEs, MCP discovery
  - `a2a-server.md` (~27KB) - Agent-to-Agent REST protocol, agent cards
  - `core.md` (~40KB) - Tools, hooks, policy engine, compression, PTY
  - `cli.md` (~35KB) - React/Ink TUI, commands, extensions, services
  - `COMPARISON.md` - Feature gap analysis vs Estela
- ✅ **Key Findings:**
  - Policy engine with priority-based rules, wildcards, regex
  - Message bus for decoupled tool/UI communication
  - Extension system (GitHub, local, linked)
  - Session resume with browser UI
  - Chat compression with LLM summarization
  - PTY shell with 300K line xterm buffer
  - Model availability service with fallback
  - TOML custom commands
  - Trusted folders security
- ✅ **Estela Advantages Identified:**
  - Browser automation (Puppeteer) - not in Gemini CLI
  - 8 fuzzy edit strategies vs basic
  - Batch tool (25 parallel calls)
  - Apply patch (unified diff)
  - Desktop app (Tauri vs CLI-only)
- ✅ **A2A & ACP Protocol Research**
  - Full A2A spec analysis (v0.3.0) - agent cards, tasks, SSE streaming
  - Full ACP spec analysis - JSON-RPC over stdio, session lifecycle
  - Gemini CLI implementation patterns documented
  - Created Epic 25: ACP & A2A Protocols (5 sprints, ~4,800 lines)
  - Research doc: `docs/analysis/gemini-cli/A2A-ACP-RESEARCH.md`
  - Sprint plan: `docs/development/epics/25-acp-a2a-protocols.md`

**Session 27** (2026-02-04)
- ✅ Verified all OpenCode Feature Parity already implemented
- ✅ Batch tool (25 parallel calls) - `batch.ts`
- ✅ Multi-edit tool - `multiedit.ts`
- ✅ Apply patch tool - `apply-patch/`
- ✅ Code search (Exa) - `codesearch.ts`
- ✅ 8 fuzzy edit strategies - `edit-replacers.ts`
- ✅ LSP (5 languages: TS, Python, Go, Rust, Java)
- ✅ Call hierarchy support - `call-hierarchy.ts`
- ✅ Doom loop detection - agent executor
- ✅ Skill system - `skill.ts`
- **ALL FEATURE PARITY WORK COMPLETE**

**Session 26** (2026-02-04)
- ✅ All 5 Cline sprints complete
- ✅ Focus Chain (~400 lines) - markdown task tracking
- ✅ Slash Commands (~600 lines) - registry + builtins
- ✅ Persistent Approvals (~400 lines) - cross-session
- ✅ MCP OAuth (~400 lines) - PKCE support
- Commits: f09133e, b46c8f4

**Session 25** (2026-02-04)
- ✅ Wired AgentExecutor to frontend (useAgent hook)
- ✅ Added Agent/Chat mode toggle in MessageInput
- ✅ Added Plan/Act mode toggle with visual indicator
- ✅ Built AgentActivityPanel with tool activity tracking
- ✅ Added doom loop detection UI warning
- ✅ Fixed type alignment (StreamError/MessageError 'api' type)
- ✅ All TypeScript errors fixed, linting passes

**Session 26** (2026-02-04)
- ✅ **Sprint 1 Complete: Security & Safety** (~750 lines)
  - Quote-aware shell parsing (state machine for quote context)
  - Chained command validation (validates each segment of pipes/chains)
  - Unicode separator detection (U+2028, U+2029, U+0085)
  - Backtick/command substitution detection
  - Redirect detection and control
  - Subshell extraction and recursive validation
  - Integration into bash tool
  - 55 passing tests
- ✅ **Sprint 2 Complete: Tool Approval UI** (~350 lines)
  - ToolApprovalDialog with keyboard shortcuts (Enter/Esc)
  - Checkbox component for "always allow"
  - Risk level indicators (low/medium/high/critical)
  - Arguments preview with truncation
  - Integration with useAgent hook
- ✅ **Sprint 3 Complete: Edit Reliability** (~300 lines)
  - Unicode normalization for patches (normalize.ts)
  - Smart quotes → straight quotes, em dashes → hyphens
  - UnicodeNormalizedReplacer integrated into edit pipeline
  - 41 passing tests
- ✅ **Sprint 4 Complete: UX Polish**
  - Virtual scrolling with @tanstack/solid-virtual for MessageList
  - Added "warning" variant to Button component
  - Auto-scroll with streaming awareness
- ✅ **Sprint 5 Complete: Reference Tool Analysis**
  - Comprehensive Cline codebase analysis
  - Identified 35+ tools, MCP integration, approval system
  - Documented feature gaps and enhancement opportunities
- **ALL 5 SPRINTS COMPLETE** - 96 passing tests total

**Session 24** (2026-02-04)
- ✅ Implemented Epic 19: Tool Hooks & MVP Polish (~2,500 lines)
- ✅ Implemented Epic 20: Browser, Plan Mode & Safety (~2,000 lines)
- ✅ Implemented Epic 21: Provider & Intelligence (~1,500 lines)
- ✅ All TypeScript errors fixed, linting passes
- 🟡 Deferred: Priority 2/3 providers (AWS Bedrock, Azure, Vertex, LM Studio)
- 🟡 Deferred: Full LSP client integration (using CLI-based diagnostics)
- 🟡 Deferred: Tree-sitter integration (using regex-based bash analysis)

---

## Milestones

| Date | Milestone | Notes |
|------|-----------|-------|
| 2025-01-28 | Project scaffold | Tauri + SolidJS + SQLite |
| 2025-01-29 | Epic 1 complete | Multi-provider LLM streaming |
| 2025-01-30 | Epic 2 complete | File tools (7 tools) |
| 2025-02-02 | Epic 3 complete | ACP monorepo + OAuth |
| 2025-02-02 | Epic 4 complete | Safety (permissions, SIGKILL, locks) |
| 2026-02-02 | Epic 5 complete | Context (token tracking, compaction) |
| 2026-02-02 | Epic 6 complete | DX (Tool.define, diffs, git) |
| 2026-02-02 | Epic 7 complete | Platform (MCP integration) |
| 2026-02-03 | Epic 8 complete | Agent loop (~1,900 lines) |
| 2026-02-03 | Epic 9 complete | Commander (~1,000 lines) |
| 2026-02-03 | Epic 10 complete | Parallel execution (~1,400 lines) |
| 2026-02-03 | Epic 11 complete | Validator (~1,000 lines) |
| 2026-02-03 | Epic 12 complete | Codebase understanding (~1,200 lines) |
| 2026-02-03 | Epic 13 complete | Config (~1,150 lines) |
| 2026-02-03 | Epic 14 complete | Memory (~1,400 lines) |
| 2026-02-03 | Epic 15 complete | Feature comparison doc |
| 2026-02-03 | Epic 16 complete | OpenCode features (~1,660 lines) |
| 2026-02-03 | Epic 17 complete | Missing tools (~3,738 lines) |
| **2026-02-04** | **Epic 19 complete** | **Tool Hooks & MVP Polish (~2,500 lines)** |
| **2026-02-04** | **Epic 20 complete** | **Browser, Plan Mode & Safety (~2,000 lines)** |
| **2026-02-04** | **Epic 21 complete** | **Provider & Intelligence (~1,500 lines)** |
| **2026-02-05** | **Epic 26 complete** | **Gemini CLI Feature Parity (337 tests, 16 files)** |
| **2026-02-05** | **Epic 25 Sprint 1+3** | **ACP Agent + A2A Server (97 tests)** |

---

## What Works

### Foundation (Epics 1-3)
- ✅ Streaming chat (OpenRouter, Anthropic)
- ✅ 7 file tools (glob, read, grep, create, write, delete, bash)
- ✅ ACP monorepo with platform abstraction
- ✅ OAuth for 4 providers

### Infrastructure (Epics 4-7)
- ✅ Permission system with 13 built-in rules
- ✅ SIGKILL escalation, file locking
- ✅ Token tracking and context compaction
- ✅ Session state with checkpoints
- ✅ defineTool() with Zod validation
- ✅ Diff tracking and git snapshots
- ✅ MCP protocol client and registry

### Agent System (Epics 8-12)
- ✅ Autonomous agent loop with recovery
- ✅ Configurable limits (turns, time, retries)
- ✅ Activity streaming via callbacks
- ✅ Commander with 5 built-in workers
- ✅ Workers as delegate_* tools
- ✅ Recursion prevention
- ✅ Parallel batch execution with semaphore
- ✅ File conflict detection (reader-writer)
- ✅ DAG task scheduler with dependencies
- ✅ Activity multiplexing for parallel workers
- ✅ Validation pipeline (syntax, types, lint, test)
- ✅ Self-review validator (LLM code review)
- ✅ Codebase indexer with symbol extraction
- ✅ Dependency graph with cycle detection
- ✅ PageRank-based file ranking
- ✅ Repo map generation with token budgets

### Settings & Memory (Epics 13-14)
- ✅ Settings schema with Zod validation
- ✅ SettingsManager with reactive updates
- ✅ API key storage via ICredentialStore
- ✅ Settings export/import (JSON)
- ✅ Episodic memory (session summaries)
- ✅ Semantic memory (learned facts)
- ✅ Procedural memory (patterns)
- ✅ Vector similarity search (cosine)
- ✅ Memory consolidation (decay, merge, promote)

### Enhancement (Epics 16-17)
- ✅ Metadata streaming for progressive updates
- ✅ Enhanced binary file detection
- ✅ CorrectedError for user feedback
- ✅ Session forking from checkpoints
- ✅ Instruction injection (AGENTS.md, CLAUDE.md)
- ✅ Typo suggestions when file not found
- ✅ Background task scheduler
- ✅ Fuzzy edit tool (7 replacer strategies)
- ✅ Directory listing (ls) with tree view
- ✅ Todo management (todoread, todowrite)
- ✅ LLM-to-user questions
- ✅ Web search (Tavily, Exa)
- ✅ Web fetch with HTML-to-Markdown
- ✅ Task tool for subagent spawning

### Tool Hooks & MVP Polish (Epic 19) - NEW
- ✅ Hook system (PreToolUse, PostToolUse, TaskStart, TaskComplete, TaskCancel)
- ✅ Hook discovery (~/.estela/hooks/, .estela/hooks/)
- ✅ Hook protocol (JSON stdin/stdout, 30s timeout)
- ✅ Content sanitization (markdown fences, model-specific fixes)
- ✅ attempt_completion tool for task signaling
- ✅ Enhanced system prompts with rules and capabilities
- ✅ requires_approval flag for bash tool

### Browser, Plan Mode & Safety (Epic 20) - NEW
- ✅ Browser tool (Puppeteer: launch, click, type, scroll, screenshot)
- ✅ WebP screenshots, console log capture
- ✅ Plan mode with read-only tool restrictions
- ✅ Doom loop detection (3x repeated calls)
- ✅ Auto-approval system with path-aware checking
- ✅ Yolo mode for unrestricted operation
- ✅ Safe commands list for bash auto-approval

### Provider & Intelligence (Epic 21) - NEW
- ✅ 7 new LLM providers (Mistral, Groq, DeepSeek, xAI, Cohere, Together, Ollama)
- ✅ Model-specific prompt variants (Claude XML, GPT native, Gemini)
- ✅ Bash command analysis (regex-based, tree-sitter ready)
- ✅ LSP diagnostics parsing (tsc, pyright CLI)
- ✅ Subagent system for specialized task spawning

### Frontend Integration (Epic 22) - IN PROGRESS
- ✅ useAgent hook with full AgentExecutor integration
- ✅ Event streaming for tool activity tracking
- ✅ Agent/Chat mode toggle in MessageInput
- ✅ Plan/Act mode toggle with visual indicator
- ✅ Doom loop detection UI warning
- ✅ AgentActivityPanel with tool timeline
- ✅ Settings page (Providers, Agents, MCP Servers, Keybindings)
- ✅ Team panel UI (TeamPanel + TeamMemberChat)
- ✅ Settings persistence (localStorage)
- ✅ Tool approval "always allow" persistence
- ✅ Sidebar fix (width-based toggle for WebKitGTK)
- ✅ Settings scroll performance (GPU promotion, transition-colors)
- ⬜ **LLM connection** — App can't talk to providers yet
- ⬜ Session management UI
- ⬜ Tool approval dialog wired to real execution

### Gemini CLI Feature Parity (Epic 26) - NEW
- ✅ Policy engine with priority rules, wildcards, regex
- ✅ Message bus with pub/sub, correlation IDs, tool confirmation
- ✅ Session resume by ID with search
- ✅ File-based session persistence
- ✅ TOML custom command system
- ✅ Trusted folders with per-folder security levels
- ✅ Extension system (install, enable, disable, reload)
- ✅ Chat compression: reverse token budget, split-point detection, verified summarize

### ACP & A2A Protocols (Epic 25) - NEW
- ✅ ACP Agent (already existed at `cli/src/acp/agent.ts`)
- ✅ A2A Server with HTTP endpoints (native `http` module)
- ✅ Agent card discovery at `/.well-known/agent.json`
- ✅ Task lifecycle with async generator streaming
- ✅ SSE streaming (framework-agnostic)
- ✅ Bearer token auth with timing-safe comparison
- ⬜ ACP Polish (Sprint 2)
- ⬜ A2A Client (Sprint 4)
- ⬜ Integration & Polish (Sprint 5)

### Dev Tooling
- ✅ Biome, Oxlint, ESLint
- ✅ Lefthook, commitlint
- ✅ Vitest, Knip
- ✅ CI/CD (GitHub Actions)

---

## What's Left

| Epic | Status | Description |
|------|--------|-------------|
| 1-17 | ✅ Done | All core epics complete |
| 19 | ✅ Done | Tool Hooks & MVP Polish |
| 20 | ✅ Done | Browser, Plan Mode & Safety |
| 21 | ✅ Done | Provider & Intelligence |
| **22** | **🟡 WIP** | **Tauri Desktop GUI (useAgent wired, UI toggles done)** |
| 23 | ⬜ | Cloud Sync (optional) |
| 24 | ⬜ | Plugin System (optional) |
| **25** | **🟡 WIP** | **ACP & A2A Protocols (Sprint 1+3 done, 97 tests)** |
| **26** | **✅ Done** | **Gemini CLI Feature Parity (337 tests, 16 files)** |

### Deferred Features (Lower Priority)
- Priority 2/3 providers: AWS Bedrock, Azure OpenAI, Google Vertex, LM Studio
- Full LSP client (vscode-languageclient) - currently using CLI-based diagnostics
- Tree-sitter parsing - currently using regex-based bash analysis
- plan_enter/plan_exit tools - plan mode works via registry check

---

## Code Stats

| Module | Lines | Location |
|--------|-------|----------|
| Agent | ~1,900 | `packages/core/src/agent/` |
| Commander | ~1,000 | `packages/core/src/commander/` |
| Parallel | ~1,400 | `packages/core/src/commander/parallel/` |
| Validator | ~1,000 | `packages/core/src/validator/` |
| Codebase | ~1,200 | `packages/core/src/codebase/` |
| Config | ~1,150 | `packages/core/src/config/` |
| Memory | ~1,400 | `packages/core/src/memory/` |
| MCP | ~950 | `packages/core/src/mcp/` |
| Context | ~1,450 | `packages/core/src/context/` |
| DX | ~1,200 | `packages/core/src/diff/`, `git/` |
| Safety | ~1,100 | `packages/core/src/permissions/` |
| Instructions | ~300 | `packages/core/src/instructions/` |
| Scheduler | ~350 | `packages/core/src/scheduler/` |
| Question | ~370 | `packages/core/src/question/` |
| Tools | ~3,100 | `packages/core/src/tools/` |
| **Hooks** | **~1,100** | `packages/core/src/hooks/` |
| **Prompts** | **~1,000** | `packages/core/src/agent/prompts/` |
| **Browser** | **~800** | `packages/core/src/tools/browser/` |
| **Providers (new)** | **~1,500** | `packages/core/src/llm/providers/` |
| **LSP** | **~400** | `packages/core/src/lsp/` |
| **Treesitter** | **~600** | `packages/core/src/codebase/treesitter/` |
| **Policy Engine** | **~800** | `packages/core/src/policy/` |
| **Message Bus** | **~400** | `packages/core/src/bus/` |
| **Session Resume** | **~400** | `packages/core/src/session/resume.ts` |
| **TOML Commands** | **~350** | `packages/core/src/commands/toml.ts` |
| **Trusted Folders** | **~400** | `packages/core/src/trust/` |
| **Extensions** | **~600** | `packages/core/src/extensions/` |
| **Chat Compression** | **~500** | `packages/core/src/context/strategies/` |
| **A2A Server** | **~1,000** | `packages/core/src/a2a/` |
| **Total Core** | **~29,500** | |

---

## Session Archive

For detailed session logs, see [sessions/archive.md](./sessions/archive.md).
