# Backlog

> Active development tracking for the Multi-Agent AI Coding Assistant

---

## Current Status

| Phase | Status |
|-------|--------|
| Phase 0: Planning | ✅ Complete |
| Phase 1: Foundation | 🟡 Ready to Start |
| Phase 2-7 | ⬜ Planned |

---

## Phase 0: Planning ✅ COMPLETE

- [x] Define tech stack (Tauri 2.0 + SolidJS)
- [x] Document architecture vision
- [x] Plan project structure
- [x] Design database schema
- [x] Define agent hierarchy
- [x] Archive OpenCode plugin documentation

---

## Phase 1: Foundation 🟡 READY

> Set up Tauri + SolidJS project with basic UI

### Tasks

- [ ] **TASK-1**: Initialize Tauri + SolidJS project
  - `npm create tauri-app@latest [name] -- --template solid-ts`
  - Configure Tailwind CSS
  - Set up project structure

- [ ] **TASK-2**: Basic UI shell
  - AppShell component
  - Sidebar navigation
  - TabBar for multi-session
  - StatusBar for agent status

- [ ] **TASK-3**: SQLite integration
  - Add `tauri-plugin-sql`
  - Create database migrations
  - Session CRUD operations

- [ ] **TASK-4**: Single LLM chat (no agents)
  - Anthropic API client
  - Message streaming
  - Basic chat UI

---

## Phase 2: Single Agent (Planned)

- [ ] File editing tools (str_replace, create_file)
- [ ] File reading tools
- [ ] Bash execution with safety guards
- [ ] Streaming text UI component

---

## Phase 3: Commander + Operator (Planned)

- [ ] Commander agent logic
- [ ] Operator spawning
- [ ] Task delegation protocol
- [ ] Result aggregation

---

## Phase 4: Parallel Operators (Planned)

- [ ] Multiple concurrent operators
- [ ] File locking/boundaries
- [ ] Validator agent
- [ ] Error recovery

---

## Phase 5: Documentation System (Planned)

- [ ] Auto-documentation before compaction
- [ ] Documentation reader for agents
- [ ] Session summaries

---

## Phase 6: LSP Integration (Planned)

- [ ] Rust LSP client
- [ ] Multi-language support
- [ ] Diagnostics tool for agents

---

## Phase 7: Polish (Planned)

- [ ] Multi-provider support (OpenAI, Google)
- [ ] Custom tools system
- [ ] Plugin architecture
- [ ] Performance optimization

---

## Decisions Made

### DECISION-1: Project Name

**Estela** - Spanish/Portuguese for "star trail" or "wake" (like a comet's trail)

**Status**: Decided

---

## Notes

- Previous OpenCode plugin work archived in `docs/archive/opencode-plugin-era/`
- Tech stack: Tauri 2.0 + SolidJS + SQLite + Rust LSP
- Architecture: Commander + Operators + Validator
