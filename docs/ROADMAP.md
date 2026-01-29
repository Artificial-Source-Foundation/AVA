# Estela Development Roadmap

> Epics, Sprints, and Tasks for building the Multi-Agent AI Coding Assistant

---

## Epic 1: Single LLM Chat (Foundation)

> Goal: Basic chat with Anthropic API, streaming responses, message persistence

### Sprint 1.1: Anthropic API Integration

| Task | Description | Priority |
|------|-------------|----------|
| **TASK-1.1.1** | Create Anthropic API client in Rust | P0 |
| **TASK-1.1.2** | Implement streaming response handling | P0 |
| **TASK-1.1.3** | Create Tauri command for chat completion | P0 |
| **TASK-1.1.4** | Handle API errors and rate limits | P1 |
| **TASK-1.1.5** | Add API key configuration (secure storage) | P0 |

### Sprint 1.2: Message Flow

| Task | Description | Priority |
|------|-------------|----------|
| **TASK-1.2.1** | Wire up MessageInput to send messages | P0 |
| **TASK-1.2.2** | Display streaming responses in MessageList | P0 |
| **TASK-1.2.3** | Save messages to SQLite | P0 |
| **TASK-1.2.4** | Load message history on session open | P1 |
| **TASK-1.2.5** | Add loading/typing indicators | P1 |

### Sprint 1.3: Session Management

| Task | Description | Priority |
|------|-------------|----------|
| **TASK-1.3.1** | Create new session functionality | P0 |
| **TASK-1.3.2** | List sessions in sidebar | P0 |
| **TASK-1.3.3** | Switch between sessions | P0 |
| **TASK-1.3.4** | Delete/archive sessions | P2 |
| **TASK-1.3.5** | Session rename functionality | P2 |

---

## Epic 2: File Tools

> Goal: Agents can read, create, and edit files

### Sprint 2.1: File Reading

| Task | Description | Priority |
|------|-------------|----------|
| **TASK-2.1.1** | Create `read_file` Tauri command | P0 |
| **TASK-2.1.2** | Create `list_directory` Tauri command | P0 |
| **TASK-2.1.3** | Create `search_files` (glob) Tauri command | P1 |
| **TASK-2.1.4** | Create `grep_content` Tauri command | P1 |
| **TASK-2.1.5** | Add file path validation & security | P0 |

### Sprint 2.2: File Writing

| Task | Description | Priority |
|------|-------------|----------|
| **TASK-2.2.1** | Create `str_replace` Tauri command | P0 |
| **TASK-2.2.2** | Create `create_file` Tauri command | P0 |
| **TASK-2.2.3** | Create `delete_file` Tauri command | P1 |
| **TASK-2.2.4** | Track file changes in database | P0 |
| **TASK-2.2.5** | Implement undo/revert for file changes | P1 |

### Sprint 2.3: Bash Execution

| Task | Description | Priority |
|------|-------------|----------|
| **TASK-2.3.1** | Create `execute_bash` Tauri command | P0 |
| **TASK-2.3.2** | Implement command timeout | P0 |
| **TASK-2.3.3** | Capture stdout/stderr | P0 |
| **TASK-2.3.4** | Add dangerous command blocklist | P0 |
| **TASK-2.3.5** | Working directory management | P1 |

---

## Epic 3: Tool Use System

> Goal: LLM can use tools via function calling

### Sprint 3.1: Tool Definition

| Task | Description | Priority |
|------|-------------|----------|
| **TASK-3.1.1** | Define tool schema format (JSON Schema) | P0 |
| **TASK-3.1.2** | Create tool registry | P0 |
| **TASK-3.1.3** | Register file tools (read, write, str_replace) | P0 |
| **TASK-3.1.4** | Register bash tool | P0 |
| **TASK-3.1.5** | Tool documentation generator | P2 |

### Sprint 3.2: Tool Execution

| Task | Description | Priority |
|------|-------------|----------|
| **TASK-3.2.1** | Parse tool calls from LLM response | P0 |
| **TASK-3.2.2** | Execute tool and capture result | P0 |
| **TASK-3.2.3** | Send tool result back to LLM | P0 |
| **TASK-3.2.4** | Handle tool execution errors | P0 |
| **TASK-3.2.5** | Tool call UI display | P1 |

---

## Epic 4: Single Agent

> Goal: One agent that can plan and execute (no hierarchy yet)

### Sprint 4.1: Agent Loop

| Task | Description | Priority |
|------|-------------|----------|
| **TASK-4.1.1** | Create agent state machine | P0 |
| **TASK-4.1.2** | Implement think → act → observe loop | P0 |
| **TASK-4.1.3** | Context window management | P0 |
| **TASK-4.1.4** | Agent system prompt | P0 |
| **TASK-4.1.5** | Max iterations / token budget | P1 |

### Sprint 4.2: Agent UI

| Task | Description | Priority |
|------|-------------|----------|
| **TASK-4.2.1** | Show agent status in UI | P0 |
| **TASK-4.2.2** | Display tool calls as they happen | P0 |
| **TASK-4.2.3** | Show files being modified | P1 |
| **TASK-4.2.4** | Cancel agent execution | P1 |
| **TASK-4.2.5** | Agent activity log | P2 |

---

## Epic 5: Commander + Operators

> Goal: Hierarchical agent system with task delegation

### Sprint 5.1: Commander Agent

| Task | Description | Priority |
|------|-------------|----------|
| **TASK-5.1.1** | Commander system prompt (planning only) | P0 |
| **TASK-5.1.2** | Task decomposition logic | P0 |
| **TASK-5.1.3** | File assignment to operators | P0 |
| **TASK-5.1.4** | Commander tool: `delegate_task` | P0 |
| **TASK-5.1.5** | Commander tool: `request_info` | P1 |

### Sprint 5.2: Operator Agents

| Task | Description | Priority |
|------|-------------|----------|
| **TASK-5.2.1** | Operator system prompt (execution only) | P0 |
| **TASK-5.2.2** | Operator spawning from Commander | P0 |
| **TASK-5.2.3** | Operator file boundaries (can only edit assigned files) | P0 |
| **TASK-5.2.4** | Operator result reporting | P0 |
| **TASK-5.2.5** | Operator context (task description + file content) | P0 |

### Sprint 5.3: Result Aggregation

| Task | Description | Priority |
|------|-------------|----------|
| **TASK-5.3.1** | Collect operator results | P0 |
| **TASK-5.3.2** | Commander reviews results | P0 |
| **TASK-5.3.3** | Handle operator failures | P0 |
| **TASK-5.3.4** | Re-delegation on failure | P1 |
| **TASK-5.3.5** | Final summary generation | P1 |

---

## Epic 6: Parallel Operators

> Goal: Multiple operators working simultaneously

### Sprint 6.1: Concurrency

| Task | Description | Priority |
|------|-------------|----------|
| **TASK-6.1.1** | Async operator execution | P0 |
| **TASK-6.1.2** | File locking (prevent conflicts) | P0 |
| **TASK-6.1.3** | Concurrent API calls | P0 |
| **TASK-6.1.4** | Rate limit handling across operators | P1 |
| **TASK-6.1.5** | Max parallel operators config | P1 |

### Sprint 6.2: Conflict Resolution

| Task | Description | Priority |
|------|-------------|----------|
| **TASK-6.2.1** | Detect file conflicts | P0 |
| **TASK-6.2.2** | Queue conflicting operations | P0 |
| **TASK-6.2.3** | Merge strategies | P2 |
| **TASK-6.2.4** | Conflict notification to user | P1 |

---

## Epic 7: Validator Agent

> Goal: QA gate before task completion

### Sprint 7.1: Validation

| Task | Description | Priority |
|------|-------------|----------|
| **TASK-7.1.1** | Validator system prompt | P0 |
| **TASK-7.1.2** | Run linter on changed files | P0 |
| **TASK-7.1.3** | Run type checker | P0 |
| **TASK-7.1.4** | Run tests (if applicable) | P1 |
| **TASK-7.1.5** | Return PASS / FIXABLE / FAIL | P0 |

### Sprint 7.2: Fix Loop

| Task | Description | Priority |
|------|-------------|----------|
| **TASK-7.2.1** | On FIXABLE: send issues back to operator | P0 |
| **TASK-7.2.2** | Operator fixes and resubmits | P0 |
| **TASK-7.2.3** | Max fix attempts limit | P1 |
| **TASK-7.2.4** | Escalate to Commander on repeated failure | P1 |

---

## Epic 8: Project Context

> Goal: Agents understand the codebase

### Sprint 8.1: Codebase Indexing

| Task | Description | Priority |
|------|-------------|----------|
| **TASK-8.1.1** | Scan project structure | P0 |
| **TASK-8.1.2** | Detect project type (language, framework) | P1 |
| **TASK-8.1.3** | Read package.json / Cargo.toml etc. | P1 |
| **TASK-8.1.4** | Index key files (entry points, configs) | P1 |
| **TASK-8.1.5** | Generate project summary for agents | P0 |

### Sprint 8.2: Smart Context

| Task | Description | Priority |
|------|-------------|----------|
| **TASK-8.2.1** | Relevant file selection for context | P0 |
| **TASK-8.2.2** | Context window optimization | P0 |
| **TASK-8.2.3** | Semantic search for related code | P2 |
| **TASK-8.2.4** | Import/dependency tracking | P2 |

---

## Epic 9: Configuration & Settings

> Goal: User can configure the application

### Sprint 9.1: Settings UI

| Task | Description | Priority |
|------|-------------|----------|
| **TASK-9.1.1** | Settings page/modal | P1 |
| **TASK-9.1.2** | API key management | P0 |
| **TASK-9.1.3** | Model selection | P1 |
| **TASK-9.1.4** | Theme settings (dark/light) | P2 |
| **TASK-9.1.5** | Working directory selection | P1 |

---

## Priority Legend

| Priority | Meaning |
|----------|---------|
| **P0** | Must have - core functionality |
| **P1** | Should have - important but not blocking |
| **P2** | Nice to have - can defer |

---

## Suggested Order

1. **Epic 1** - Single LLM Chat (get basics working)
2. **Epic 2** - File Tools (agents need to read/write)
3. **Epic 3** - Tool Use System (connect LLM to tools)
4. **Epic 4** - Single Agent (working autonomous agent)
5. **Epic 5** - Commander + Operators (hierarchy)
6. **Epic 7** - Validator (quality gate)
7. **Epic 6** - Parallel Operators (performance)
8. **Epic 8** - Project Context (smarter agents)
9. **Epic 9** - Configuration (polish)

---

## Current Focus

**Next Up: Epic 1, Sprint 1.1** - Get Anthropic API working with streaming
