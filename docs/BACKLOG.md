# Backlog

> Active development tracking for Estela

---

## Current Status

| Epic | Status |
|------|--------|
| Epic 1: Single LLM Chat | 🟡 Next Up |
| Epic 2: File Tools | ⬜ Planned |
| Epic 3: Tool Use System | ⬜ Planned |
| Epic 4: Single Agent | ⬜ Planned |
| Epic 5: Commander + Operators | ⬜ Planned |
| Epic 6: Parallel Operators | ⬜ Planned |
| Epic 7: Validator Agent | ⬜ Planned |
| Epic 8: Project Context | ⬜ Planned |
| Epic 9: Configuration | ⬜ Planned |

**Full roadmap**: See `docs/ROADMAP.md`

---

## Phase 0: Foundation ✅ COMPLETE

- [x] Scaffold Tauri + SolidJS project
- [x] Configure Tailwind CSS
- [x] Add Tauri plugins (SQL, Shell, FS)
- [x] Create SQLite database schema
- [x] Set up frontend structure
- [x] Create basic UI shell
- [x] Set up Rust backend structure

---

## Epic 1: Single LLM Chat 🟡 IN PROGRESS

### Sprint 1.1: Anthropic API Integration

- [ ] **TASK-1.1.1**: Create Anthropic API client in Rust
  - HTTP client with reqwest
  - Streaming SSE handling
  - Message format (Claude messages API)

- [ ] **TASK-1.1.2**: Implement streaming response handling
  - Parse SSE events
  - Yield tokens as they arrive
  - Handle completion

- [ ] **TASK-1.1.3**: Create Tauri command for chat
  - `send_message` command
  - Stream events to frontend
  - Return full response

- [ ] **TASK-1.1.4**: Handle API errors
  - Rate limits (429)
  - Auth errors (401)
  - Server errors (500+)
  - Retry logic

- [ ] **TASK-1.1.5**: API key configuration
  - Secure storage (keyring or encrypted file)
  - Settings UI for key entry
  - Validate key on save

### Sprint 1.2: Message Flow

- [ ] **TASK-1.2.1**: Wire MessageInput to backend
- [ ] **TASK-1.2.2**: Display streaming in MessageList
- [ ] **TASK-1.2.3**: Persist messages to SQLite
- [ ] **TASK-1.2.4**: Load history on session open
- [ ] **TASK-1.2.5**: Loading/typing indicators

### Sprint 1.3: Session Management

- [ ] **TASK-1.3.1**: Create new session
- [ ] **TASK-1.3.2**: List sessions in sidebar
- [ ] **TASK-1.3.3**: Switch sessions
- [ ] **TASK-1.3.4**: Delete/archive sessions
- [ ] **TASK-1.3.5**: Rename sessions

---

## Epic 2: File Tools ⬜ PLANNED

### Sprint 2.1: File Reading
- [ ] `read_file` command
- [ ] `list_directory` command
- [ ] `search_files` (glob) command
- [ ] `grep_content` command
- [ ] Security validation

### Sprint 2.2: File Writing
- [ ] `str_replace` command
- [ ] `create_file` command
- [ ] `delete_file` command
- [ ] Track changes in DB
- [ ] Undo/revert functionality

### Sprint 2.3: Bash Execution
- [ ] `execute_bash` command
- [ ] Timeout handling
- [ ] stdout/stderr capture
- [ ] Dangerous command blocklist

---

## Quick Links

| Document | Description |
|----------|-------------|
| `ROADMAP.md` | Full epic/sprint/task breakdown |
| `VISION.md` | Architecture and design |
| `architecture/` | Technical specifications |

---

## Decisions Made

### DECISION-1: Project Name
**Estela** - Spanish for "star trail" or "wake"

### DECISION-2: Tech Stack
- Tauri 2.0 + SolidJS + SQLite
- Tailwind CSS v4
- Rust backend

### DECISION-3: Agent Hierarchy
- Commander (plans) → Operators (execute) → Validator (verify)
- Commander never writes code
- Parallel operators for performance
